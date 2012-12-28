package com.gangverk;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.channels.FileChannel;
import java.util.ArrayList;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;


/*
 * AudioPlayer feeds an AudioTrack with data
 */
public class AudioPlayer {

	private static final String LOG_TAG = "AudioPlayer";
	private AudioTrack audioTrack;
	private InputStream dataSource;
	private Thread decodeThread;
	private int decoder;
	public AudioPlayer(){
		audioTrack = new AudioTrack(
				AudioManager.STREAM_MUSIC,
				44100, //Todo support input
				AudioFormat.CHANNEL_OUT_STEREO, //Todo support mono
				AudioFormat.ENCODING_PCM_16BIT,
				88200,
				AudioTrack.MODE_STREAM );
		audioTrack.pause();
	}
	public void setDataSource(InputStream dataSource){
		//init decoder
		this.dataSource = dataSource;
		this.decoder = initDecoder();
	}
	public void prepare(){
		
	}
	public void start(){
		//start thread to feed audioTrack via jni
		decodeThread = new Thread(new Runnable(){
			@Override
			public void run(){
				int res = startDecoding(decoder,86018);	
				Log.d(LOG_TAG,String.format("startDecoding returns %d",res));
			}
		});
		decodeThread.start();
		
	}
	public void stop() {
		stopDecoding(decoder);
	}
	private int pushAudio(byte[] audio, int offset, int len){
		//Todo: handle stop, pause etc
		int pushed =  audioTrack.write(audio, offset, len);
		Log.d(LOG_TAG, String.format("pushAudio %d %d",len,pushed));
		if(audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PAUSED){
			Log.d(LOG_TAG,"Starting playback");
			audioTrack.play();
		}
		return pushed;
	}
	private int pullData(byte[]data, int offset, int len){
		try {
			int read =  dataSource.read(data, offset, len);
			Log.d(LOG_TAG, String.format("dataSource returned %d bytes",read));
			return read;
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		return -1;
	}
	private native static void init();
	private native int initDecoder();
	private native int startDecoding(int decoder, int codec_id);
	private native int stopDecoding(int decoder);
	private static boolean libLoaded = false;
	public static synchronized void loadLibrary(Context context) {

		if(!libLoaded){
			/*
			// This is a trick to transparently utilize jni CPU optimizations
			// We detect cpu capabilities (if it has neon, vfp or nothing (vanilla)).
			// All the ffmpeg libraries are included in the package but the
			// one we actually use has to be named libffmpeg.so . So we run the
			// detection, find the correct version of the library and copy it to
			// package root dir where we load it.
			SystemUtils sysUtils = new SystemUtils();

			String load_version = "vanilla";
			if(sysUtils.has_iset_info) {
				if(sysUtils.supportsInstructionSet(SystemUtils.InstructionSet.neon)) {
					load_version = "neon";
				} else if(sysUtils.supportsInstructionSet(SystemUtils.InstructionSet.vfp)) {
					load_version = "vfp";
				}
			}
			Log.d("FFMPEGPlayer",String.format("Loaded version: %s",load_version));
			String ffmpeg_libname = "ffmpeg" + load_version;

			PackageManager pm = context.getPackageManager();
			try {
				String pkgDir = pm.getApplicationInfo(context.getPackageName(), 0).dataDir;
				try {
					File dstFFmpegLibFile = new File(pkgDir + "/libffmpeg.so");
					File srcFFmpegLibFile = new File(pkgDir + "/lib/lib" + ffmpeg_libname + ".so");
					SystemUtils.copy(srcFFmpegLibFile,dstFFmpegLibFile);

					System.load(dstFFmpegLibFile.getAbsolutePath());
					System.loadLibrary("ffinterface" );
				} catch (IOException e) {
					e.printStackTrace();
				}
			} catch (NameNotFoundException e1) {
				e1.printStackTrace();
			}
			init();*/
			System.loadLibrary("nativeplayer");
			libLoaded = true;
		}
	}
	
	
}
