flvdemux
========

This is a little demo app wrapping the FLVDemuxingInputStream

FLVDemuxingInputStream.java is based almost entirely on trevlovett's FlashAACInputStream

Gangverk simply added MP3 and Metadata support and fixed a bug that was causing problems for some AAC players

Please note that AAC will not work on API Level < 14 and even on 14+ there is a bug that causes the MediaPlayer to buffer forever before starting playback.
