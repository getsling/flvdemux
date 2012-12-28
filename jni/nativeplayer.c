
#include <jni.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>


// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;

// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLEffectSendItf bqPlayerEffectSend;
static SLMuteSoloItf bqPlayerMuteSolo;
static SLVolumeItf bqPlayerVolume;

// aux effect on the output mix, used by the buffer queue player
static const SLEnvironmentalReverbSettings reverbSettings =
    SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;

// URI player interfaces
static SLAndroidSimpleBufferQueueItf decBuffQueueItf;
static SLObjectItf uriPlayerObject = NULL;
static SLPlayItf uriPlayerPlay;
static SLSeekItf uriPlayerSeek;
static SLMuteSoloItf uriPlayerMuteSolo;
static SLVolumeItf uriPlayerVolume;


// pointer and size of the next player buffer to enqueue, and number of remaining buffers
static short *nextBuffer;
static unsigned nextSize;
static int nextCount;



#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 32768
#define AUDIO_REFILL_THRESH 4096
#define AUDIO_OUTBUF_SIZE 65536
#define MAX_PUSH_ATTEMPTS 100

#define NUM_EXPLICIT_INTERFACES_FOR_PLAYER 2
/* Number of decoded samples produced by one AAC frame; defined by the standard */
#define SAMPLES_PER_AAC_FRAME 1024
/* Size of the encoded AAC ADTS buffer queue */
#define NB_BUFFERS_IN_ADTS_QUEUE 2 // 2 to 4 is typical

/* Size of the decoded PCM buffer queue */
#define NB_BUFFERS_IN_PCM_QUEUE 3  // 2 to 4 is typical
/* Size of each PCM buffer in the queue */
#define BUFFER_SIZE_IN_BYTES   (2*sizeof(short)*SAMPLES_PER_AAC_FRAME)

/* Local storage for decoded PCM audio data */
int8_t pcmData[NB_BUFFERS_IN_PCM_QUEUE * BUFFER_SIZE_IN_BYTES];
uint8_t aacData[AUDIO_INBUF_SIZE];


#define ANDROID_MODULE "nativeplayer"
#define ANDROID_LOG(...) \
    __android_log_print(ANDROID_LOG_VERBOSE, ANDROID_MODULE, __VA_ARGS__)

typedef struct Decoder{
	int stopped;
	JNIEnv* env;
	jobject thiz;
	//java callbacks
	jmethodID pull;
	jmethodID push;
	//java buffer for audio output
	jbyte* audiobuffer;
	//java buffer for audio input
	jbyte* databuffer;

    //c buffer for audio input
    uint8_t* datasrc;
    uint8_t* datasrc_ptr;
    int datasrc_size;

    //c buffer for audio output
    uint8_t* datasink;
    uint8_t* datasink_ptr;

	//timer for interrupt on blocking read
	clock_t block_start;
} Decoder;



//always try to fill whatever is left in decoder->datasrc

void pull_data(Decoder *decoder){
	JNIEnv* env = decoder->env;
	if(!decoder->databuffer){
		ANDROID_LOG("decoder has no java input audiobuffer, creating one");
		decoder->databuffer = (*env)->NewByteArray(env,AUDIO_INBUF_SIZE);
	}
    if(!decoder->datasrc){
        ANDROID_LOG("decoder has no c input audiobuffer, creating one");
        decoder->datasrc = calloc(1,AUDIO_INBUF_SIZE);
        decoder->datasrc_size = 0;
        decoder->datasrc_ptr = decoder->datasrc;
    }
    if(decoder->datasrc_size > 0){
        memmove(decoder->datasrc, decoder->datasrc_ptr, decoder->datasrc_size);
        decoder->datasrc_ptr = decoder->datasrc;
	}
    int max_len = AUDIO_INBUF_SIZE-decoder->datasrc_size;
    int len = (*env)->CallIntMethod(env, decoder->thiz, decoder->pull, decoder->databuffer, 0, max_len);
	if(len > 0){
        (*env)->GetByteArrayRegion(env, decoder->databuffer, 0, len,decoder->datasrc_ptr);
        decoder->datasrc_size +=len;
    }
}

void push_audio(Decoder* decoder, jbyte* decoded_output, int decoded){
	JNIEnv* env = decoder->env;
	jint len,pushed = 0,attempts = 0;
    ANDROID_LOG("push decoded of len %d",decoded);
	if(!decoder->audiobuffer){
		ANDROID_LOG("decoder has no java output audiobuffer, creating one");
		decoder->audiobuffer = (*env)->NewByteArray(env,BUFFER_SIZE_IN_BYTES);
	}
    ANDROID_LOG("before copy");
	(*env)->SetByteArrayRegion(env, decoder->audiobuffer, 0, decoded, decoded_output);
    ANDROID_LOG("copied to java array");
	while(!decoder->stopped && pushed < decoded){
		len = (*env)->CallIntMethod(env, decoder->thiz, decoder->push, decoder->audiobuffer+pushed, 0,decoded-pushed);
        ANDROID_LOG("sent to java");
		if(len < 0 || attempts > MAX_PUSH_ATTEMPTS){
			decoder->stopped = 1;
			break;
		}
        ANDROID_LOG("pushed %d bytes",len);
		pushed = pushed + len;
		attempts = attempts+1;
	}
}


//-----------------------------------------------------------------
/* Callback for AndroidBufferQueueItf through which we supply ADTS buffers */
SLresult AndroidBufferQueueCallback(
        SLAndroidBufferQueueItf caller,
        void *pCallbackContext,        /* input */
        void *pBufferContext,          /* input */
        void *pBufferData,             /* input */
        SLuint32 dataSize,             /* input */
        SLuint32 dataUsed,             /* input */
        const SLAndroidBufferItem *pItems,/* input */
        SLuint32 itemsLength           /* input */)
{
    ANDROID_LOG("We need more aac");
    Decoder* decoder = (Decoder*)pCallbackContext;
    if(decoder->datasrc_size < AUDIO_REFILL_THRESH){
        ANDROID_LOG("refilling from java");
        pull_data(decoder);
    }
    unsigned char* frame = decoder->datasrc_ptr;
    unsigned framelen = ((frame[3] & 3) << 11) | (frame[4] << 3) | (frame[5] >> 5);
    SLresult result = (*caller)->Enqueue(caller, NULL, frame, framelen, NULL, 0);
    decoder->datasrc_ptr+=framelen;
    decoder->datasrc_size-=framelen;

    return SL_RESULT_SUCCESS;
}

//-----------------------------------------------------------------
/* Callback for decoding buffer queue events */
void DecPlayCallback(
        SLAndroidSimpleBufferQueueItf queueItf,
        void *pContext)
{
    ANDROID_LOG("We have more pcm");
    Decoder *decoder = (Decoder*)pContext;
    ANDROID_LOG("FAKED PUSH TO JAVA");
    //push_audio(decoder,decoder->datasink_ptr,BUFFER_SIZE_IN_BYTES);

    ANDROID_LOG("pushed the pcm to java");
    /* Re-enqueue the now empty buffer */
    SLresult result;
    result = (*queueItf)->Enqueue(queueItf, decoder->datasink_ptr, BUFFER_SIZE_IN_BYTES);
    ANDROID_LOG("enqueued an empty buffer");
    /* Increase data pointer by buffer size, with circular wraparound */
    decoder->datasink_ptr += BUFFER_SIZE_IN_BYTES;
    if (decoder->datasink_ptr >= decoder->datasink + (NB_BUFFERS_IN_PCM_QUEUE * BUFFER_SIZE_IN_BYTES)) {
        decoder->datasink_ptr = decoder->datasink;
    }
}





static void log_error(char* str,int err){
	char buf[1024];
	av_strerror(err, buf, sizeof(buf));
	ANDROID_LOG("%s: %s",str,buf);
}

/****************************************************************************************************
 * FUNCTIONS - JNI
 ****************************************************************************************************/
JNIEXPORT void JNICALL Java_com_gangverk_AudioPlayer_init
(JNIEnv *env, jclass cls){

	ANDROID_LOG("done registering");
}


JNIEXPORT jint JNICALL Java_com_gangverk_AudioPlayer_initDecoder
	(JNIEnv *env, jobject thiz){
	//TODO USE A STATIC SEMAPHORE
	//Create the cside object
	ANDROID_LOG("init decoder");
	Decoder *decoder = (Decoder*)calloc( 1, sizeof(struct Decoder));
	return (jint)decoder;
}

JNIEXPORT jint JNICALL Java_com_gangverk_AudioPlayer_startDecoding
(JNIEnv *env, jobject thiz,jint d, jint codec_id){
	//Endless loop that decodes until stopped
	//Fetch the callback methods (pushAudio and next)
	jclass cls = (*env)->GetObjectClass(env, thiz);
	jmethodID push = (*env)->GetMethodID(env, cls, "pushAudio", "([BII)I");
	jmethodID pull = (*env)->GetMethodID(env,cls, "pullData","([BII)I");
    jint retval = 1;

	ANDROID_LOG("cached method ids");
	if (push == 0 || pull == 0) return -1;

	Decoder *decoder = (Decoder*)d;
	decoder->env = env;
	decoder->thiz = thiz;
	decoder->stopped = 0;
	decoder->push = push;
	decoder->pull = pull;
	decoder->block_start = -1;
	decoder->audiobuffer = (*env)->NewByteArray(env,BUFFER_SIZE_IN_BYTES);
	decoder->databuffer = (*env)->NewByteArray(env,AUDIO_INBUF_SIZE);
    decoder->datasrc = NULL;
    decoder->datasrc_ptr = NULL;
    decoder->datasrc_size = 0;
    decoder->datasink = calloc(NB_BUFFERS_IN_PCM_QUEUE,BUFFER_SIZE_IN_BYTES);
    decoder->datasink_ptr = decoder->datasink;

    SLresult result;
    SLObjectItf sl = NULL;
    SLEngineItf EngineItf;
    /* Objects this application uses: one audio player */
    SLObjectItf  player = NULL;
    /* Interfaces for the audio player */
    SLPlayItf                     playItf;
    /*   to retrieve the PCM samples */
    SLAndroidSimpleBufferQueueItf decBuffQueueItf;
    /*   to queue the AAC data to decode */
    SLAndroidBufferQueueItf       aacBuffQueueItf;

    SLboolean required[NUM_EXPLICIT_INTERFACES_FOR_PLAYER];
    SLInterfaceID iidArray[NUM_EXPLICIT_INTERFACES_FOR_PLAYER];


    result = slCreateEngine( &sl, 0, NULL, 0, NULL, NULL);
    if(SL_RESULT_SUCCESS != result){
        retval = -10;
        goto fail;
    }
    /* Realizing the SL Engine in synchronous mode. */
    result = (*sl)->Realize(sl, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS != result){
        retval = -11;
        goto fail;
    }
    /* Get the SL Engine Interface which is implicit */
    result = (*sl)->GetInterface(sl, SL_IID_ENGINE, (void*)&EngineItf);
    if(SL_RESULT_SUCCESS != result){
        retval = -12;
        goto fail;
    }

    /* Initialize arrays required[] and iidArray[] */
    unsigned int i;
    for (i=0 ; i < NUM_EXPLICIT_INTERFACES_FOR_PLAYER ; i++) {
        required[i] = SL_BOOLEAN_FALSE;
        iidArray[i] = SL_IID_NULL;
    }

    /* ------------------------------------------------------ */
    /* Configuration of the player  */

    /* Request the AndroidSimpleBufferQueue interface */
    required[0] = SL_BOOLEAN_TRUE;
    iidArray[0] = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    /* Request the AndroidBufferQueue interface */
    required[1] = SL_BOOLEAN_TRUE;
    iidArray[1] = SL_IID_ANDROIDBUFFERQUEUESOURCE;

    /* Setup the data source for queueing AAC buffers of ADTS data */
    SLDataLocator_AndroidBufferQueue loc_srcAbq = {
            SL_DATALOCATOR_ANDROIDBUFFERQUEUE /*locatorType*/,
            NB_BUFFERS_IN_ADTS_QUEUE          /*numBuffers*/};
    SLDataFormat_MIME format_srcMime = {
            SL_DATAFORMAT_MIME         /*formatType*/,
            SL_ANDROID_MIME_AACADTS    /*mimeType*/,
            SL_CONTAINERTYPE_RAW       /*containerType*/};
    SLDataSource decSource = {&loc_srcAbq /*pLocator*/, &format_srcMime /*pFormat*/};

    /* Setup the data sink, a buffer queue for buffers of PCM data */
    SLDataLocator_AndroidSimpleBufferQueue loc_destBq = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE/*locatorType*/,
            NB_BUFFERS_IN_PCM_QUEUE                /*numBuffers*/ };

    /*    declare we're decoding to PCM, the parameters after that need to be valid,
          but are ignored, the decoded format will match the source */
    SLDataFormat_PCM format_destPcm = { /*formatType*/ SL_DATAFORMAT_PCM, /*numChannels*/ 1,
            /*samplesPerSec*/ SL_SAMPLINGRATE_8, /*pcm.bitsPerSample*/ SL_PCMSAMPLEFORMAT_FIXED_16,
            /*/containerSize*/ 16, /*channelMask*/ SL_SPEAKER_FRONT_LEFT,
            /*endianness*/ SL_BYTEORDER_LITTLEENDIAN };
    SLDataSink decDest = {&loc_destBq /*pLocator*/, &format_destPcm /*pFormat*/};

    /* Create the audio player */
    result = (*EngineItf)->CreateAudioPlayer(EngineItf, &player, &decSource, &decDest,
            NUM_EXPLICIT_INTERFACES_FOR_PLAYER,
            iidArray, required);
    if(SL_RESULT_SUCCESS != result){
        retval = -1;
        goto fail;
    }


    /* Realize the player in synchronous mode. */
    result = (*player)->Realize(player, SL_BOOLEAN_FALSE);
    if(SL_RESULT_SUCCESS != result){
        retval = -2;
        goto fail;
    }

    /* Get the play interface which is implicit */
    result = (*player)->GetInterface(player, SL_IID_PLAY, (void*)&playItf);
    if(SL_RESULT_SUCCESS != result){
        retval = -3;
        goto fail;
    }

    /* Get the buffer queue interface which was explicitly requested */
    result = (*player)->GetInterface(player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, (void*)&decBuffQueueItf);
    if(SL_RESULT_SUCCESS != result){
        retval = -4;
        goto fail;
    }
    /* Get the Android buffer queue interface which was explicitly requested */
    result = (*player)->GetInterface(player, SL_IID_ANDROIDBUFFERQUEUESOURCE, (void*)&aacBuffQueueItf);
    if(SL_RESULT_SUCCESS != result){
        retval = -5;
        goto fail;
    }

    /* ------------------------------------------------------ */
    /* Initialize the callback and its context for the buffer queue of the decoded PCM */

    result = (*decBuffQueueItf)->RegisterCallback(decBuffQueueItf, DecPlayCallback, decoder);
    if(SL_RESULT_SUCCESS != result){
        retval = -6;
        goto fail;
    }

    /* Enqueue buffers to map the region of memory allocated to store the decoded data */
    //printf("Enqueueing initial empty buffers to receive decoded PCM data");
    for(i = 0 ; i < NB_BUFFERS_IN_PCM_QUEUE ; i++) {
        //printf(" %d", i);
        result = (*decBuffQueueItf)->Enqueue(decBuffQueueItf, decoder->datasink_ptr, BUFFER_SIZE_IN_BYTES);
        if(SL_RESULT_SUCCESS != result){
            retval = -13;
            goto fail;
        }
        decoder->datasink_ptr += BUFFER_SIZE_IN_BYTES;
        if (decoder->datasink_ptr >= decoder->datasink + (NB_BUFFERS_IN_PCM_QUEUE * BUFFER_SIZE_IN_BYTES)) {
            decoder->datasink_ptr = decoder->datasink;
        }
    }
    //printf("\n");

    /* Initialize the callback for the Android buffer queue of the encoded data */
    result = (*aacBuffQueueItf)->RegisterCallback(aacBuffQueueItf, AndroidBufferQueueCallback, decoder);
    if(SL_RESULT_SUCCESS != result){
        retval = -7;
        goto fail;
    }

    /* Enqueue the content of our encoded data before starting to play,
       we don't want to starve the player initially */
    //printf("Enqueueing initial full buffers of encoded ADTS data");

	pull_data(decoder);
    for (i=0 ; i < NB_BUFFERS_IN_ADTS_QUEUE ; i++) {
        unsigned char* frame = decoder->datasrc_ptr;
        unsigned framelen = ((frame[3] & 3) << 11) | (frame[4] << 3) | (frame[5] >> 5);
        result = (*aacBuffQueueItf)->Enqueue(aacBuffQueueItf, NULL /*pBufferContext*/,
                frame, framelen, NULL, 0);
        if(SL_RESULT_SUCCESS != result){
            retval = -8;
            goto fail;
        }
        decoder->datasrc_ptr += framelen;
        decoder->datasrc_size -= framelen;
    }	    
    /* ------------------------------------------------------ */
    /* Start decoding */
    //printf("Starting to decode\n");
    result = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
    if(SL_RESULT_SUCCESS != result){
        retval = -9;
        goto fail;
    }
    //Hang around to perform push pull from java as possible
    
    /* ------------------------------------------------------ */
    /* End of decoding */
    return retval;
fail:

    return retval;
}

JNIEXPORT void JNICALL Java_com_gangverk_AudioPlayer_stopDecoding
  (JNIEnv *env, jobject thiz, jint d){
	ANDROID_LOG("stopDecoding() called, should happen momentarily...");
	Decoder *decoder = (Decoder*)d;
	decoder->stopped = 1;

}

