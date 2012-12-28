package com.gangverk;

public class NativeAudio {
	
	public NativeAudio(){
		createEngine();
	}
	public boolean setDataSource(String url){
		return createUriAudioPlayer(url);
	}
	public void prepare(){
		
	}
	public void start(){
		setPlayingUriAudioPlayer(true);
	}
	public void stop(){
		setPlayingUriAudioPlayer(false);
	}
	
    /** Native methods, implemented in jni folder */
    public static native void createEngine();
    public static native void createBufferQueueAudioPlayer();
    public static native boolean createUriAudioPlayer(String uri);
    public static native void setPlayingUriAudioPlayer(boolean isPlaying);
    public static native void setChannelMuteUriAudioPlayer(int chan, boolean mute);
    public static native void setChannelSoloUriAudioPlayer(int chan, boolean solo);
    public static native int getNumChannelsUriAudioPlayer();
    public static native void setVolumeUriAudioPlayer(int millibel);
    public static native void setMuteUriAudioPlayer(boolean mute);
    public static native void shutdown();

    /** Load jni .so on initialization */
    static {
         System.loadLibrary("native-audio-jni");
    }
}
