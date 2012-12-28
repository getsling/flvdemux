package com.gangverk;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.math.BigInteger;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.Socket;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLEncoder;
import java.net.UnknownHostException;
import java.nio.channels.FileChannel;
import java.security.KeyManagementException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.UnrecoverableKeyException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

import org.apache.http.HttpEntity;
import org.apache.http.HttpResponse;
import org.apache.http.HttpVersion;
import org.apache.http.StatusLine;
import org.apache.http.client.ClientProtocolException;
import org.apache.http.client.HttpClient;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.conn.ClientConnectionManager;
import org.apache.http.conn.scheme.PlainSocketFactory;
import org.apache.http.conn.scheme.Scheme;
import org.apache.http.conn.scheme.SchemeRegistry;
import org.apache.http.conn.ssl.SSLSocketFactory;
import org.apache.http.impl.client.DefaultHttpClient;
import org.apache.http.impl.conn.tsccm.ThreadSafeClientConnManager;
import org.apache.http.params.BasicHttpParams;
import org.apache.http.params.HttpConnectionParams;
import org.apache.http.params.HttpParams;
import org.apache.http.params.HttpProtocolParams;
import org.apache.http.protocol.HTTP;
import org.json.JSONException;
import org.json.JSONObject;

import android.content.Context;
import android.graphics.Shader.TileMode;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Environment;
import android.view.View;

public class SystemUtils {
	public boolean has_iset_info = false;
	private ArrayList<InstructionSet> supportedInstructionSets;
	// TODO: go back to 8/10 secs
	private static final int HTTP_CONNECTION_TIMEOUT = 80000;
	private static final int HTTP_SOCKET_TIMEOUT = 100000;

	public enum InstructionSet {
		neon, vfp, vfpv3
	}

	public SystemUtils() {
		supportedInstructionSets = new ArrayList<InstructionSet>();
		String commandLine = "cat /proc/cpuinfo";
		Process process;
		try {
			process = Runtime.getRuntime().exec(commandLine);
			BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(process.getInputStream()));
			String curLine;
			while((curLine = bufferedReader.readLine()) != null) {
				if(curLine.contains("Features")) {
					String[] isets = curLine.split(" ");
					for(String iset : isets) {
						try {
							supportedInstructionSets.add(InstructionSet.valueOf(iset));
						} catch (IllegalArgumentException iae) {
						}
					}
					has_iset_info = true;
					break;
				}
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	/**
	 * Indicates whether given instruction set is supported by the current architecture
	 * @param iset The instruction set
	 * @return true if instruction set is supported, false otherwise.
	 */
	public boolean supportsInstructionSet(InstructionSet iset) {
		return supportedInstructionSets.contains(iset);
	}
	/**
	 *  Avoid ssl exception on some phones (e.g. Atli's phone)
	 * see: http://stackoverflow.com/questions/2642777/trusting-all-certificates-using-httpclient-over-https
	 *
	 */
	private static class MySSLSocketFactory extends SSLSocketFactory {
		SSLContext sslContext = SSLContext.getInstance("TLS");

		public MySSLSocketFactory(KeyStore truststore) throws NoSuchAlgorithmException, KeyManagementException, KeyStoreException, UnrecoverableKeyException {
			super(truststore);

			TrustManager tm = new X509TrustManager() {
				public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
				}

				public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
				}

				public X509Certificate[] getAcceptedIssuers() {
					return null;
				}
			};

			sslContext.init(null, new TrustManager[] { tm }, null);
		}

		@Override
		public Socket createSocket(Socket socket, String host, int port, boolean autoClose) throws IOException, UnknownHostException {
			return sslContext.getSocketFactory().createSocket(socket, host, port, autoClose);
		}

		@Override
		public Socket createSocket() throws IOException {
			return sslContext.getSocketFactory().createSocket();
		}
	}

	/**
	 * Returns an httpClient that doesn't verify ssl hostname. Only needs to be used
	 * in android <2.2 phones, otherwise DefaultHttpClient should be used
	 * @return
	 */
	public static DefaultHttpClient getSSLFreeHttpClient() {
		try {
			KeyStore trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
			trustStore.load(null, null);

			SSLSocketFactory sf = new MySSLSocketFactory(trustStore);
			sf.setHostnameVerifier(SSLSocketFactory.ALLOW_ALL_HOSTNAME_VERIFIER);

			HttpParams params = new BasicHttpParams();
			HttpProtocolParams.setVersion(params, HttpVersion.HTTP_1_1);
			HttpProtocolParams.setContentCharset(params, HTTP.UTF_8);

			SchemeRegistry registry = new SchemeRegistry();
			registry.register(new Scheme("http", PlainSocketFactory.getSocketFactory(), 80));
			registry.register(new Scheme("https", sf, 443));

			ClientConnectionManager ccm = new ThreadSafeClientConnManager(params, registry);

			return new DefaultHttpClient(ccm, params);
		} catch (Exception e) {
			return new DefaultHttpClient();
		}
	}


	static public void copy(String src, String dst) throws IOException {
		copy(new File(src), new File(dst));
	}

	/**
	 * Copies from one file to another, creating destination file if required.
	 * @param src File to copy data from.
	 * @param dst File to copy data to.
	 * @throws IOException
	 */
	static public void copy(File src, File dst) throws IOException {
		FileChannel inChannel = new FileInputStream(src).getChannel();
		FileChannel outChannel = new FileOutputStream(dst).getChannel();

		try {
			inChannel.transferTo(0, inChannel.size(), outChannel);
		} finally {
			if(inChannel != null) {
				inChannel.close();
			}
			if(outChannel != null) {
				outChannel.close();
			}
		}
	}

	/**
	 * Reads a stream and writes it into a string. Closes inputStream when done.
	 * @param inputStream The stream to read
	 * @return A string, containing stream data
	 * @throws java.io.IOException
	 */
	public static String stringFromStream(InputStream inputStream) throws java.io.IOException{
		String encoding = "UTF-8";
		StringBuilder builder = new StringBuilder();
		BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream, encoding));
		String line;
		while((line = reader.readLine()) != null) {
			builder.append(line);
		}
		reader.close();
		return builder.toString();
	}

	/**
	 * Copies bytes between streams. Closes both streams when done.
	 * @param inStream The stream to copy from
	 * @param outStream The stream to copy to
	 * @return The amount of bytes copied
	 * @throws IOException
	 */
	public static int copyInputStreamToOutputStream(InputStream inStream, OutputStream outStream) throws IOException {
		BufferedInputStream bis = new BufferedInputStream(inStream);
		BufferedOutputStream bos = new BufferedOutputStream(outStream);
		int totalLen = 0;

		int bufSize = 1024*4;
		byte[] buffer = new byte[bufSize];
		int len;
		while((len = bis.read(buffer,0,bufSize)) > -1) {
			bos.write(buffer,0,len);
			totalLen += len;
		}
		bis.close();
		bos.close();
		return totalLen;
	}

	/**
	 * Merges two String arrays
	 * @param first array previous array
	 * @param other array second array
	 * @return concatenated array
	 */
	public static String[] mergeStringArrays(String[] first, String[] second) {
		String[] result = new String[first.length + second.length];
		System.arraycopy(first, 0, result, 0, first.length);
		System.arraycopy(second, 0, result, first.length, second.length);
		return result;
	}

	/**
	 * Checks if there is wifi or mobile connection available 
	 * @param context The application context
	 * @return true if there is network connection available
	 */
	public static boolean isNetworkConnection(Context context) {
		ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
		NetworkInfo activeNetwork = cm.getActiveNetworkInfo();
		return activeNetwork != null && activeNetwork.isConnected();
	}
	public static URLConnection openConnection(String url) throws IOException{
		URL urlToGet = new URL(url);
		URLConnection connection = urlToGet.openConnection();
		connection.setConnectTimeout(HTTP_CONNECTION_TIMEOUT);
		connection.setReadTimeout(HTTP_SOCKET_TIMEOUT);
		return connection;
	}
	public static String curl(String url){
		return curl(url,null);
	}
	public static String curl(String url, List<String> cookies){
		try {
			URLConnection connection = openConnection(url);
			if(cookies != null){
				StringBuilder cookieValue = new StringBuilder();
				for (String cookie : cookies) {
					if(cookieValue.length() > 0){
						cookieValue.append("; ");
					}
					cookieValue.append(cookie.split(";", 2)[0]);
				}
				connection.addRequestProperty("Cookie", cookieValue.toString());
			}
			BufferedReader in = new BufferedReader(new InputStreamReader(connection.getInputStream()),1024);
			String inputLine;
			StringBuffer input = new StringBuffer();
			while ((inputLine = in.readLine()) != null){ 
				input.append(inputLine);
			}
			in.close();
			return input.toString();
		} catch (MalformedURLException e) {
			e.printStackTrace();
		} catch (IOException e) {
			e.printStackTrace();
		}
		return null;
	}

	/**
	 * Get the default http params (to prevent infinite http hangs)
	 * @return reasonable default for a HttpClient
	 */
	public static void setHttpTimeoutParams(DefaultHttpClient httpClient) {
		HttpParams httpParameters = new BasicHttpParams();
		// connection established timeout
		HttpConnectionParams.setConnectionTimeout(httpParameters, HTTP_CONNECTION_TIMEOUT);
		// socket timeout
		HttpConnectionParams.setSoTimeout(httpParameters, HTTP_SOCKET_TIMEOUT);
		httpClient.setParams(httpParameters);
	}

	/**
	 * Converts a json dict with string-string key-value pairs into a suitable GET querystring
	 * @param postDict
	 * @return
	 */
	public static String jsonToQueryParms(JSONObject postDict) {
		StringBuilder queryString = new StringBuilder();
		Iterator<?> jsonKeys = postDict.keys();
		int i = 0;
		while(jsonKeys.hasNext()) {
			try {
				String key = (String)jsonKeys.next();
				String value = postDict.getString(key);
				queryString.append(String.format("%s%s=%s",(i > 0 ? "&" : ""), key, URLEncoder.encode(value,"UTF-8")));
				i++;
			} catch (JSONException jExp) {
			} catch (UnsupportedEncodingException uee) {
			}
		}
		return queryString.toString();
	}
	
	/**
	 * Fixes background not tiling issue
	 * http://stackoverflow.com/questions/4336286/tiled-drawable-sometimes-stretches
	 * @param view
	 */
	public static void fixBackgroundRepeat(View view) {
	    Drawable bg = view.getBackground();
	    if (bg != null) {
	        if (bg instanceof BitmapDrawable) {
	            BitmapDrawable bmp = (BitmapDrawable) bg;
	            bmp.mutate(); // make sure that we aren't sharing state anymore
	            bmp.setTileModeXY(TileMode.REPEAT, TileMode.REPEAT);
	        }
	    }
	}
	
	/**
	 * Create hash from input using the provided hash type
	 * @param s
	 * @param hashType e.g. MD5
	 * @return Hashed output
	 */
	public static String cryptoHash(String s, String hashType) {
		// see http://www.androidsnippets.com/create-a-md5-hash-and-dump-as-a-hex-string
		String outHash = null;
		try {
			MessageDigest digestInstance = java.security.MessageDigest.getInstance(hashType);
			// digest length is in bytes, we need in hex (1 byte = 2 hex symbols) so multiply by 2
			String formatString = String.format(Locale.US,"%%%ds",digestInstance.getDigestLength()*2); 
			digestInstance.update(s.getBytes());
			BigInteger hash = new BigInteger(1, digestInstance.digest());
			// method above strips leading zeros so we need to zero pad:
			if("MD5".equals(hashType)) {
				outHash = String.format(formatString, hash.toString(16)).replace(' ', '0');
			} else if("SHA512".equals(hashType)) {
				outHash = String.format(formatString, hash.toString(16)).replace(' ', '0');
			}
				
		} catch (NoSuchAlgorithmException e) {
			e.printStackTrace();
		}

		return outHash;
	}
	
	/**
	 * URL to Json
	 * @param url The url of the json
	 * @param headers Header-keys
	 * @param headerValues Header-values
	 * @return
	 */
	public static String readJSONFeed(String url, String[] headers, String[] headerValues) {
		StringBuilder builder = new StringBuilder();
		HttpClient client = new DefaultHttpClient();
		HttpGet httpGet = new HttpGet(url);
		if(headers != null) {
			for(int i = 0; i<headers.length ;i++) {
				httpGet.setHeader(headers[i], headerValues[i]);
			}
		}
		try {
			HttpResponse response = client.execute(httpGet);
			StatusLine statusLine = response.getStatusLine();
			int statusCode = statusLine.getStatusCode();
			if(statusCode == HttpURLConnection.HTTP_OK) {
				HttpEntity entity = response.getEntity();
				InputStream content = entity.getContent();
				BufferedReader reader = new BufferedReader(new InputStreamReader(content));
				String line;
				while((line = reader.readLine()) != null) {
					builder.append(line);
				}
			} else {
				return null;
			}
		} catch (ClientProtocolException e) {
			e.printStackTrace();
		} catch (IOException e) {
			e.printStackTrace();
		}
		return builder.toString();
	}
	
	public static File getDiskCacheDir(Context context, String uniqueName) {

		// Check if media is mounted or storage is built-in, if so, try and use external cache dir
		// otherwise use internal cache dir
		String storageState = Environment.getExternalStorageState();
		String cachePath = null;
		File externalCacheDir = context.getExternalFilesDir(null);
		if(externalCacheDir != null && storageState.equals(Environment.MEDIA_MOUNTED)) {
			cachePath = externalCacheDir.getPath();
		} else {
			cachePath = context.getFilesDir().getPath();
		}
		return new File(cachePath + File.separator + uniqueName + File.separator);
	}
}