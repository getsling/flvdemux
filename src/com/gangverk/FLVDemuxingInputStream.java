package com.gangverk;
/*

FLVDemuxingInputStream based almost entirely on trevlovett's FlashAACInputStream
Gangverk's version adds capability for MP3, Metadata and corrects a bug that some AAC players could not live with

http://codeartisan.tumblr.com/post/11943952404/playing-flv-wrapped-aac-streams-from-android
http://code.google.com/p/aacdecoder-android/source/browse/trunk/decoder/src/com/spoledge/aacdecoder/FlashAACInputStream.java?spec=svn11&r=11

*/

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.UnsupportedEncodingException;

import android.util.Log;


public class FLVDemuxingInputStream extends InputStream{

private static final String LOG_TAG = "DemuxingInputStream";
private DataInputStream dis = null;
private int countInBackBuffer = 0;
private int backBufferLen = 65536;
private byte[] backBuffer = new byte[backBufferLen];
private int readBufferLen = 65536;
private byte[] readBuffer = new byte[readBufferLen];
private byte[] metaBuffer = new byte[1024];
private static final int AUDIO_CODEC_MASK = 0xf0;
private static final int AUDIO_CODEC_AAC = 10 << 4;
private static final int AUDIO_CODEC_MP3 = 2 << 4;

private int _aacProfile;
private int _sampleRateIndex;
private int _channelConfig;

public FLVDemuxingInputStream(InputStream istream) throws IOException {
	this.dis = new DataInputStream(istream);
	// Check that stream is a Flash Video stream
	if ((char)dis.readByte() != 'F' || (char)dis.readByte() != 'L' || (char)dis.readByte() != 'V')
		throw new IOException("The file is not a FLV file.");

	dis.readByte(); //version not used
	byte exists = dis.readByte();

	if ((exists != 5) && (exists != 4))
		throw new IOException("No Audio Stream");

	dis.readInt(); // data offset of header. ignoring
}

// don't use-- efficiency is not good
@Override
public int read() throws IOException {
	byte[] b = new byte[1];
	read(b, 0, 1);
	return ((int)b[0]) & 0xFF;
}

// Reads a frame at a time.  If the entire frame cannot be accomodated by b,
// function saves the remainder in backBuffer for use in next call.
// returns: number of bytes read into b

@Override
public int read(byte[] b, int off, int len) throws IOException {
	if (off < 0 || len < 0 || b.length - off < len)
		throw new IndexOutOfBoundsException();

	if (len > readBufferLen) 
		throw new IndexOutOfBoundsException("len exceeds readBufferLen");


	if (countInBackBuffer > 0) {
		if (countInBackBuffer >= len) {
			System.arraycopy(backBuffer, 0, b, off, len);

			// move the remainder in the backBuffer to the top
			if (countInBackBuffer > len) 
				System.arraycopy(backBuffer, len, backBuffer, 0, countInBackBuffer - len);

			countInBackBuffer -= len;
			return len;
		}
		else {
			System.arraycopy(backBuffer, 0, b, off, countInBackBuffer);
		}
	}

	int remaining = len - countInBackBuffer;
	int readBytes = 0;
	int b_off = off + countInBackBuffer;

	countInBackBuffer = 0;

	while (true) {
		readBytes = readFrame(readBuffer);
		remaining -= readBytes;

		if (remaining <= 0) {
			System.arraycopy(readBuffer, 0, b, b_off, readBytes + remaining);
			if (remaining < 0)  {
				System.arraycopy(readBuffer, readBytes + remaining, backBuffer, 0, Math.abs(remaining));
				countInBackBuffer = Math.abs(remaining);
			}
			return len;
		}
		else if (remaining > 0) {
			System.arraycopy(readBuffer, 0, b, b_off, readBytes);
			b_off += readBytes;
		}
	}
}

// reads FLV Tag data
private int readFrame(byte[] buf) throws IOException {
	dis.readInt(); // PreviousTagSize0 skipping

	byte tagType = dis.readByte();
	while (tagType != 8) {
		if(tagType == 0x12){
			try {
				int dataSize = (int)readNext3Bytes();
				long ts = readNext3Bytes();
				final int timeStamp = (int) (ts | (dis.readByte() << 24));
				dis.skipBytes(3);
				dis.readFully(metaBuffer, 0, dataSize);
				Log.d(LOG_TAG, "Got metadata, ignoring for now");
				dis.skipBytes(4);
			} catch (UnsupportedEncodingException e) {
				e.printStackTrace();
			}				
		}else{
			long skip = readNext3Bytes() + 11;
			dis.skipBytes((int)skip);
		}
		tagType = dis.readByte();
	}
	long dataSize = readNext3Bytes() - 1;
	dis.readInt(); //timeStamps
	readNext3Bytes(); //streamID
	byte audioHeader = dis.readByte(); //audioHeader
	if (dataSize == 0){
		return 0;
	}
		
	if((audioHeader & AUDIO_CODEC_MASK) == AUDIO_CODEC_AAC){
		return fillAACBuffer(buf, (int)dataSize);
	}else{
		dis.readFully(buf, 0, (int)dataSize);
		return (int) dataSize;
	}
}

// puts a complete AAC frame with ADTS header in buf, ready for playing, returns size of frame in bytes
private int fillAACBuffer(byte[] bytes, int dataSize) throws IOException {
	dis.readFully(bytes,6,dataSize);
	if(bytes[6] == 0 && dataSize > 2){
		
		int bits = ((bytes[7] & 0xff)*256 + (bytes[8] & 0xff)) << 16;
		_aacProfile = readBits(bits, 5) - 1;
		bits <<= 5;
		_sampleRateIndex = readBits(bits, 4);
		bits <<= 4;
		_channelConfig = readBits(bits, 4);
		return 0;
		
	}else{
	
		dataSize -= 1;

		// see http://wiki.multimedia.cx/index.php?title=ADTS for format spec
		long bits = 0;
		bits = writeBits(bits, 12, 0xFFF);
		bits = writeBits(bits, 3, 0);
		bits = writeBits(bits, 1, 1);

		bytes[0] = (byte)(bits >> 8);
		bytes[1] = (byte)(bits);

		bits = 0;
		bits = writeBits(bits, 2, _aacProfile);
		bits = writeBits(bits, 4, _sampleRateIndex);
		bits = writeBits(bits, 1, 0);
		bits = writeBits(bits, 3, _channelConfig); 
		bits = writeBits(bits, 4, 0);
		bits = writeBits(bits, 2, (dataSize + 7) & 0x1800);

		bytes[2] = (byte)(bits >> 8);
		bytes[3] = (byte)(bits);

		bits = 0;
		bits = writeBits(bits, 11, (dataSize + 7) & 0x7FF);
		bits = writeBits(bits, 11, 0x7FF);
		bits = writeBits(bits, 2, 0);
		bytes[4] = (byte)(bits >> 16);
		bytes[5] = (byte)(bits >> 8);
		bytes[6] = (byte)(bits);
		bytes[7+dataSize] = 0;
		return dataSize + 7;
	}
		
}


private int readBits(int x, int length) {
	int r = (int)(x >> (32 - length));
	return r;
}

public long writeBits(long x, int length, int value) {
	long mask = 0xffffffffL >> (32 - length);
	x = (x << length) | (value & mask);
	return x;
}

private long readNext3Bytes() throws IOException {
	return dis.readUnsignedByte() * 256 * 256 + dis.readUnsignedByte() * 256 + dis.readUnsignedByte();
}

// simple unit test to see if it can read from a file or network stream
public static void main(String args[]) {
    try {

        File f = new File("radio.flv");
        FileInputStream fis = new FileInputStream(f);
        FLVDemuxingInputStream dis = new FLVDemuxingInputStream(fis);
       
       
        File outFile = new File("radio.dat");
        FileOutputStream out = new FileOutputStream(outFile);

        int read = 0;
        byte[] bytes = new byte[1024];
 
        while ((read = dis.read(bytes)) != -1) {
                out.write(bytes, 0, read);
        }
        dis.close();
        out.flush();
        out.close();

    } catch (Exception e) {
        System.out.println(e);
        e.printStackTrace();
    }
}

}
