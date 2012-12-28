
#include <jni.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>


#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 32768
#define AUDIO_REFILL_THRESH 4096
#define AUDIO_OUTBUF_SIZE 65536
#define MAX_PUSH_ATTEMPTS 100

#define AACD_MODULE "ffmpegplayer"
#ifndef AACD_LOGLEVEL_ERROR
	#define AACD_LOGLEVEL_ERROR "error"
#endif
#ifdef AACD_LOGLEVEL_TRACE
#define AACD_TRACE(...) \
    __android_log_print(ANDROID_LOG_VERBOSE, AACD_MODULE, __VA_ARGS__)
#else
#define AACD_TRACE(...) //
#endif

#ifdef AACD_LOGLEVEL_DEBUG
#define AACD_DEBUG(...) \
    __android_log_print(ANDROID_LOG_DEBUG, AACD_MODULE, __VA_ARGS__)
#else
#define AACD_DEBUG(...) //
#endif

#ifdef AACD_LOGLEVEL_INFO
#define AACD_INFO(...) \
    __android_log_print(ANDROID_LOG_INFO, AACD_MODULE, __VA_ARGS__)
#else
#define AACD_INFO(...) //
#endif

#ifdef AACD_LOGLEVEL_WARN
#define AACD_WARN(...) \
    __android_log_print(ANDROID_LOG_WARN, AACD_MODULE, __VA_ARGS__)
#else
#define AACD_WARN(...) //
#endif

#ifdef AACD_LOGLEVEL_ERROR
#define AACD_ERROR(...) \
    __android_log_print(ANDROID_LOG_ERROR, AACD_MODULE, __VA_ARGS__)
#else
#error "Idiot, AACD_LOGLEVEL_ERROR is not defined"
#define AACD_ERROR(...) //
#endif

#define FFMPEG_LOG(...)  __android_log_print(ANDROID_LOG_WARN, "FFMPEG", __VA_ARGS__)

void ff_log_callback(void* ptr, int level, const char* fmt, va_list vl){
	char line[1024];
	vsnprintf(line, sizeof(line), fmt, vl);
	FFMPEG_LOG(line);
}


typedef struct Decoder{
	int stopped;
	JNIEnv* env;
	jobject thiz;
	//java callbacks
	jmethodID pull;
	jmethodID push;
	//buffer for audio output
	jbyte* audiobuffer;
	//buffer for audio input
	jbyte* databuffer;

	//timer for interrupt on blocking read
	clock_t block_start;
} Decoder;


//Simple version just outputs whatever is available in buffer, if buffer is empty it fetches a new buffer first
int pull_data(Decoder *decoder, uint8_t *buf, int buf_size ){
	JNIEnv* env = decoder->env;
	if(!decoder->databuffer){
		AACD_DEBUG("decoder has no audiobuffer, creating one");
		decoder->databuffer = (*env)->NewByteArray(env,AUDIO_INBUF_SIZE);
	}
	int len = (*env)->CallIntMethod(env, decoder->thiz, decoder->pull, decoder->databuffer, 0, buf_size);
	(*env)->GetByteArrayRegion(env, decoder->databuffer, 0, len,buf);
	return len;
}

void push_audio(Decoder* decoder, jbyte* decoded_output, int decoded){
	JNIEnv* env = decoder->env;
	jint len,pushed = 0,attempts = 0;
	if(!decoder->audiobuffer){
		AACD_DEBUG("decoder has no audiobuffer, creating one");
		decoder->audiobuffer = (*env)->NewByteArray(env,AUDIO_OUTBUF_SIZE);
	}
	(*env)->SetByteArrayRegion(env, decoder->audiobuffer, 0, decoded, decoded_output);

	while(!decoder->stopped && pushed < decoded){
		len = (*env)->CallIntMethod(env, decoder->thiz, decoder->push, decoder->audiobuffer+pushed, 0,decoded-pushed);
		if(len < 0 || attempts > MAX_PUSH_ATTEMPTS){
			decoder->stopped = 1;
			break;
		}
		pushed = pushed + len;
		attempts = attempts+1;
	}
}


static int interrupt_cb(void *ctx)
{
	Decoder *decoder = (Decoder*)ctx;
	clock_t block_stop = clock();
	if(decoder->stopped){
		return 1;
	}/*
	if(decoder->block_start < 0 || decoder->block_start > block_stop){ //we hit wraparound
		return 0;
	}
	if(((block_stop - decoder->block_start)/CLOCKS_PER_SEC) > HTTP_TIMEOUT){ //timeout
		AACD_ERROR("We hit timeout on blocking read");
		decoder->stopped = 2;
		return 1;
	}*/
	return 0;
}

static int ff_lockmgr(void **mutex, enum AVLockOp op) 
{ 
   pthread_mutex_t** pmutex = (pthread_mutex_t**) mutex;
   switch (op) {
   case AV_LOCK_CREATE: 
      *pmutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t)); 
       pthread_mutex_init(*pmutex, NULL);
       break;
   case AV_LOCK_OBTAIN:
       pthread_mutex_lock(*pmutex);
       break;
   case AV_LOCK_RELEASE:
       pthread_mutex_unlock(*pmutex);
       break;
   case AV_LOCK_DESTROY:
       pthread_mutex_destroy(*pmutex);
       free(*pmutex);
       break;
   }
   return 0;
}

static void log_error(char* str,int err){
	char buf[1024];
	av_strerror(err, buf, sizeof(buf));
	AACD_ERROR("%s: %s",str,buf);
}

/****************************************************************************************************
 * FUNCTIONS - JNI
 ****************************************************************************************************/
JNIEXPORT void JNICALL Java_com_gangverk_AudioPlayer_init
(JNIEnv *env, jclass cls){
	av_log_set_callback(ff_log_callback);
	av_lockmgr_register(ff_lockmgr);
	av_register_all();
	avformat_network_init();

	AACD_DEBUG("done registering");
}

/*
 * Class:     com_raplayers_Decoder
 * Method:    nativeStart
 * Signature: (Lcom/spoledge/aacdecoder/Decoder/Info;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_com_gangverk_AudioPlayer_initDecoder
	(JNIEnv *env, jobject thiz){
	//TODO USE A STATIC SEMAPHORE
	//Create the cside object
	AACD_DEBUG("init decoder");
	Decoder *decoder = (Decoder*)av_calloc( 1, sizeof(struct Decoder));
	return (jint)decoder;
}

JNIEXPORT jint JNICALL Java_com_gangverk_AudioPlayer_startDecoding
(JNIEnv *env, jobject thiz,jint d, jint codec_id){
	//Endless loop that decodes until stopped
	//Fetch the callback methods (pushAudio and next)
	int err,i;
	jint retval=-1;
	jclass cls = (*env)->GetObjectClass(env, thiz);
	jmethodID push = (*env)->GetMethodID(env, cls, "pushAudio", "([BII)I");
	jmethodID pull = (*env)->GetMethodID(env,cls, "pullData","([BII)I");


	AACD_DEBUG("cached method ids");
	if (push == 0 || pull == 0) return -1;

	Decoder *decoder = (Decoder*)d;
	decoder->env = env;
	decoder->thiz = thiz;
	decoder->stopped = 0;
	decoder->push = push;
	decoder->pull = pull;
	decoder->block_start = -1;
	decoder->audiobuffer = NULL;
	decoder->databuffer = NULL;

	AVCodecContext* codecCtx = NULL;
	AVCodec* codec = NULL;
	AVFrame *decoded_frame = NULL;
	AVPacket packet;
	uint8_t inbuf[AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];
	uint8_t outbuf[AUDIO_OUTBUF_SIZE];

	AVIOInterruptCB interruptor =  { interrupt_cb, decoder };
	
	av_init_packet(&packet);

	codec = avcodec_find_decoder(CODEC_ID_AAC);
	if(codec == NULL){
		goto fail;
	}
	
	codecCtx = avcodec_alloc_context3(codec);

	err = avcodec_open2(codecCtx,codec,NULL);
	if(err<0){
		log_error("failed to open codec",err);
		goto fail;
	}
	//Output buffer we fill up with frames and send to Java
	int decoded = 0,len;
	packet.data = inbuf;
	packet.size = pull_data(decoder, packet.data, AUDIO_INBUF_SIZE);
	AACD_DEBUG("first bytes %d %d %d",packet.data[0], packet.data[1], packet.data[2]);

	int got_frame = 0;
	while(packet.size > 0 && !decoder->stopped){
		if (!decoded_frame) {
        	if (!(decoded_frame = avcodec_alloc_frame())) {
            	log_error("out of memory",-1);
            	goto fail;
        	}
    	} else {
        	avcodec_get_frame_defaults(decoded_frame);
		}
		len = avcodec_decode_audio4(codecCtx,decoded_frame,&got_frame,&packet);
		if(len < 0){ 
			log_error("decode error",-1);
			goto fail;
		}
		if(got_frame){
			int data_size = av_samples_get_buffer_size(NULL,codecCtx->channels,
														decoded_frame->nb_samples,
														codecCtx->sample_fmt,1);
			
			if(decoded + data_size > AUDIO_OUTBUF_SIZE){
				push_audio(decoder,outbuf,decoded);
				decoded = 0;
			}
			memcpy(outbuf+decoded,decoded_frame->data[0],data_size);
			decoded+=data_size;
		}
		packet.size -=len;
		packet.data +=len;
		packet.dts = packet.pts = AV_NOPTS_VALUE;
		if(packet.size < AUDIO_REFILL_THRESH){
			memmove(inbuf, packet.data, packet.size);
			packet.data = inbuf;
            len = pull_data(decoder, packet.data + packet.size, AUDIO_INBUF_SIZE - packet.size);
            if (len > 0){
                packet.size += len;
			}
		}
	}
	AACD_DEBUG("Stopped decoding");
	//if we are here and not stopped then the file is complete
	//need to throw the rest of the data at pushAudioBytes
	if(decoded>0 && !decoder->stopped){
		AACD_DEBUG("Still have valid bytes flushing to audio");
		push_audio(decoder,outbuf,decoded);		
	}
	if(decoder->stopped != 1){
		//always send poison at the end so the next guy in the chain knows to stop
		(*env)->CallIntMethod(env, decoder->thiz, decoder->push, decoder->audiobuffer, -1);
		AACD_DEBUG("Poisoned flow");
	}

fail:
	//Inform the caller whether we stopped in error or whether this is just normal runout?
	if(decoder->stopped == 1 || (decoder->stopped == 0 && err == AVERROR_EOF)){
		AACD_DEBUG("decoder stopped normally returning 0");
		retval = 0;
	}
	AACD_DEBUG("Cleaning up ffmpeg");
	if(codecCtx){
		avcodec_close(codecCtx);
	}
	if(decoded_frame){
		av_free(decoded_frame);
	}

	if(decoder->audiobuffer){
		(*env)->DeleteLocalRef(env, decoder->audiobuffer);
	}
	if(decoder->databuffer){
		(*env)->DeleteLocalRef(env, decoder->databuffer);
	}
	av_free(decoder);
	AACD_DEBUG("...done");

	return retval;
}

JNIEXPORT void JNICALL Java_com_gangverk_AudioPlayer_stopDecoding
  (JNIEnv *env, jobject thiz, jint d){
	AACD_DEBUG("stopDecoding() called, should happen momentarily...");
	Decoder *decoder = (Decoder*)d;
	decoder->stopped = 1;

}

