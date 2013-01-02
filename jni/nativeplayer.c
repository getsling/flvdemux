
#include <jni.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <android/log.h>


// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>


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


#define ANDROID_MODULE "nativeplayer"
#define ANDROID_LOG(...) \
    __android_log_print(ANDROID_LOG_VERBOSE, ANDROID_MODULE, __VA_ARGS__)
 
void report_exceptional_condition(int i){
    ANDROID_LOG("exceptional condition %d %s",i,strerror( errno ));
} 
 
typedef struct
{
  void *address;
 
  unsigned long count_bytes;
  unsigned long write_offset_bytes;
  unsigned long read_offset_bytes;
} ring_buffer;
 
//Warning order should be at least 12 for Linux
void
ring_buffer_create ( ring_buffer *buffer, unsigned long order)
{
  char path[] = "/sdcard/ rbXXXXXX";
  int file_descriptor;
  void *address;
  int status;
 
  file_descriptor = mkstemp (path);
  if (file_descriptor < 0)
    report_exceptional_condition (1);  
  status = unlink (path);
  if (status)
    report_exceptional_condition (2);

  buffer->count_bytes = 1UL << order;
  buffer->write_offset_bytes = 0;
  buffer->read_offset_bytes = 0;
 
  status = ftruncate (file_descriptor, buffer->count_bytes);
  if (status)
    report_exceptional_condition (3);
 
  buffer->address = mmap (NULL, buffer->count_bytes << 1, PROT_NONE,
                          MAP_ANON | MAP_PRIVATE, -1, 0);
 
  if (buffer->address == MAP_FAILED)
    report_exceptional_condition (4);
  
  address =
    mmap (buffer->address, buffer->count_bytes, PROT_READ | PROT_WRITE,
          MAP_FIXED | MAP_SHARED, file_descriptor, 0);
 
  if (address != buffer->address)
    report_exceptional_condition (5);
  
  address = mmap (buffer->address + buffer->count_bytes,
                  buffer->count_bytes, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_SHARED, file_descriptor, 0);
 
  if (address != buffer->address + buffer->count_bytes)
    report_exceptional_condition (6);
  status = close (file_descriptor);
  if (status)
    report_exceptional_condition (7);
}
 
void
ring_buffer_free ( ring_buffer *buffer)
{
  int status;
 
  status = munmap (buffer->address, buffer->count_bytes << 1);
  if (status)
    report_exceptional_condition (0);
}
 
void *
ring_buffer_write_address ( ring_buffer *buffer)
{
  // ****** void pointer arithmetic is a constraint violation
  return buffer->address + buffer->write_offset_bytes;
}
 
void
ring_buffer_write_advance ( ring_buffer *buffer,
                           unsigned long count_bytes)
{
  buffer->write_offset_bytes += count_bytes;
}
 
void *
ring_buffer_read_address ( ring_buffer *buffer)
{
  return buffer->address + buffer->read_offset_bytes;
}
 
void
ring_buffer_read_advance ( ring_buffer *buffer,
                          unsigned long count_bytes)
{
  buffer->read_offset_bytes += count_bytes;
 
  if (buffer->read_offset_bytes >= buffer->count_bytes)
    {
      buffer->read_offset_bytes -= buffer->count_bytes;
      buffer->write_offset_bytes -= buffer->count_bytes;
    }
}
 
unsigned long
ring_buffer_count_bytes ( ring_buffer *buffer)
{
  return buffer->write_offset_bytes - buffer->read_offset_bytes;
}
 
unsigned long
ring_buffer_count_free_bytes ( ring_buffer *buffer)
{
  return buffer->count_bytes - ring_buffer_count_bytes (buffer);
}
 
void
ring_buffer_clear ( ring_buffer *buffer)
{
  buffer->write_offset_bytes = 0;
  buffer->read_offset_bytes = 0;
}
typedef struct Decoder{
    int stopped;
    ring_buffer* inputbuffer;
    ring_buffer* outputbuffer;
    JNIEnv* env;
    jobject thiz;
    jmethodID pull;
    jmethodID push;
    jbyte* databuffer;
    jbyte* audiobuffer;
} Decoder;


//always try to fill whatever is left in decoder->datasrc

void pull_data(Decoder *decoder){
	JNIEnv* env = decoder->env;
	if(!decoder->databuffer){
		ANDROID_LOG("decoder has no java input audiobuffer, creating one");
		decoder->databuffer = (*env)->NewByteArray(env,AUDIO_INBUF_SIZE);
	}
    ANDROID_LOG("ring_buffer_count_free_bytes %d",ring_buffer_count_free_bytes(decoder->inputbuffer));
    //TODO wait conditional
    while(ring_buffer_count_free_bytes(decoder->inputbuffer)<AUDIO_INBUF_SIZE);
    ANDROID_LOG("yay no endless loop");
    int len = (*env)->CallIntMethod(env, decoder->thiz, decoder->pull, decoder->databuffer, 0, AUDIO_INBUF_SIZE);
	if(len > 0){
        (*env)->GetByteArrayRegion(env, decoder->databuffer, 0, len,ring_buffer_write_address(decoder->inputbuffer));
        ring_buffer_write_advance(decoder->inputbuffer,len);
    }
}

void push_audio(Decoder* decoder){
	JNIEnv* env = decoder->env;
	jint len,pushed = 0,attempts = 0;
    ANDROID_LOG("push decoded");
	if(!decoder->audiobuffer){
		ANDROID_LOG("decoder has no java output audiobuffer, creating one");
		decoder->audiobuffer = (*env)->NewByteArray(env,AUDIO_OUTBUF_SIZE);
	}
    //TODO wait conditional
    while(ring_buffer_count_bytes(decoder->outputbuffer)<AUDIO_OUTBUF_SIZE);

	(*env)->SetByteArrayRegion(env, decoder->audiobuffer, 0, AUDIO_OUTBUF_SIZE, ring_buffer_read_address(decoder->outputbuffer));
	while(!decoder->stopped && pushed < AUDIO_OUTBUF_SIZE){
		len = (*env)->CallIntMethod(env, decoder->thiz, decoder->push, decoder->audiobuffer+pushed, 0,AUDIO_OUTBUF_SIZE-pushed);
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
// Callback for AndroidBufferQueueItf through which we supply ADTS buffers
SLresult AndroidBufferQueueCallback(
        SLAndroidBufferQueueItf caller,
        void *pCallbackContext,       
        void *pBufferContext,         
        void *pBufferData,          
        SLuint32 dataSize,        
        SLuint32 dataUsed,         
        const SLAndroidBufferItem *pItems,
        SLuint32 itemsLength          )
{
    ANDROID_LOG("We need more aac");
    Decoder* decoder = (Decoder*)pCallbackContext;
    //TODO wait conditional
    while(ring_buffer_count_bytes(decoder->inputbuffer)<7);
    unsigned char* frame = (unsigned char*)ring_buffer_read_address(decoder->inputbuffer);
    unsigned framelen = ((frame[3] & 3) << 11) | (frame[4] << 3) | (frame[5] >> 5);
    //TODO wait conditional
    while(ring_buffer_count_bytes(decoder->inputbuffer)<framelen);
    SLresult result = (*caller)->Enqueue(caller, NULL, frame, framelen, NULL, 0);
    ring_buffer_read_advance(decoder->inputbuffer,framelen);

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
    ring_buffer_write_advance(decoder->outputbuffer,BUFFER_SIZE_IN_BYTES);
    //TODO wait conditional
    while(ring_buffer_count_free_bytes(decoder->outputbuffer) < BUFFER_SIZE_IN_BYTES);
    SLresult result = (*queueItf)->Enqueue(queueItf,ring_buffer_write_address(decoder->outputbuffer), BUFFER_SIZE_IN_BYTES);
    ANDROID_LOG("enqueued an empty buffer");   
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

    ring_buffer ring_input;
    ring_buffer ring_output;
    ring_buffer_create(&ring_input,16);
    ring_buffer_create(&ring_output,16);

	Decoder *decoder = (Decoder*)d;
    decoder->inputbuffer = &ring_input;
    decoder->outputbuffer = &ring_output;
    decoder->env = env;
    decoder->thiz = thiz;
    decoder->stopped = 0;
    decoder->push = push;
    decoder->pull = pull;

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
    for(i = 0 ; i < NB_BUFFERS_IN_PCM_QUEUE ; i++) {

        result = (*decBuffQueueItf)->Enqueue(decBuffQueueItf, ring_buffer_write_address(decoder->outputbuffer), BUFFER_SIZE_IN_BYTES);
        if(SL_RESULT_SUCCESS != result){
            retval = -13;
            goto fail;
        }
        ring_buffer_write_advance(decoder->outputbuffer,BUFFER_SIZE_IN_BYTES);
    }


    /* Initialize the callback for the Android buffer queue of the encoded data */
    result = (*aacBuffQueueItf)->RegisterCallback(aacBuffQueueItf, AndroidBufferQueueCallback, decoder);
    if(SL_RESULT_SUCCESS != result){
        retval = -7;
        goto fail;
    }

    /* Enqueue the content of our encoded data before starting to play,
       we don't want to starve the player initially */

	pull_data(decoder);
    //TODO make this more robust, we are assuming enough data to fill NB_BUFFERS_IN_ADTS_QUEUE
    for (i=0 ; i < NB_BUFFERS_IN_ADTS_QUEUE ; i++) {

        unsigned char* frame = (unsigned char*)ring_buffer_read_address(decoder->inputbuffer);
        unsigned framelen = ((frame[3] & 3) << 11) | (frame[4] << 3) | (frame[5] >> 5);
        result = (*aacBuffQueueItf)->Enqueue(aacBuffQueueItf, NULL,
                frame, framelen, NULL, 0);
        if(SL_RESULT_SUCCESS != result){
            retval = -8;
            goto fail;
        }
        ring_buffer_read_advance(decoder->inputbuffer,framelen);
    }	    
    /* ------------------------------------------------------ */
    /* Start decoding */
    result = (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
    if(SL_RESULT_SUCCESS != result){
        retval = -9;
        goto fail;
    }
    //Hang around to perform push pull from java as possible
    //TODO use wait conditionals
    while(1){

        pull_data(decoder);
        
        push_audio(decoder);
        
    }

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

