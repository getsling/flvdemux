package com.gangverk;

import java.io.File;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLConnection;
import java.util.Properties;

import com.gangverk.NanoHTTPD.Response;


import android.media.MediaPlayer;
import android.os.Bundle;
import android.app.Activity;
import android.util.Log;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.EditText;

public class PlayUrlActivity extends Activity {

	private Button playButton;
	private Button stopButton;
	private EditText urlText;
	
	private NanoHTTPD server = null;
	private MediaPlayer player = null;
	
	private static final int HTTP_CONNECTION_TIMEOUT = 80000;
	private static final int HTTP_SOCKET_TIMEOUT = 100000;
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_play_url);

		OnClickListener clickListener = new OnClickListener() {
			@Override
			public void onClick(View v) {
				if(v == playButton){
					if(server != null){
						stop();
					}
					start(urlText.getText().toString());
				}else if (v == stopButton){
					stop();
				}
			}		
		};

		urlText = (EditText)findViewById(R.id.urltext);
		playButton = (Button) findViewById(R.id.playbutton);
		stopButton = (Button) findViewById(R.id.stopbutton);
		urlText.setText("http://208.92.55.49/KROQFM?streamtheworld_user=1&SRC=CBS&DIST=CBS&TGT=Android");
		playButton.setOnClickListener(clickListener);
		stopButton.setOnClickListener(clickListener);
	}
	
	private void start(final String url){
		
		try {
			server = new NanoHTTPD(8080, null) {
				@Override
				public Response serve( String uri, String method, Properties header, Properties parms, Properties files ){

					Response res = null;
					try {
						URL urlToGet = new URL(url);
						URLConnection connection = urlToGet.openConnection();
						connection.setConnectTimeout(HTTP_CONNECTION_TIMEOUT);
						connection.setReadTimeout(HTTP_SOCKET_TIMEOUT);
						
						FLVDemuxingInputStream in = new FLVDemuxingInputStream(connection.getInputStream());
						res = new Response( HTTP_OK, "application/octet-stream", in);
						res.addHeader( "Accept-Ranges", "bytes");
						res.addHeader( "Connection", "close");
					} catch (MalformedURLException e) {
						// TODO Auto-generated catch block
						e.printStackTrace();
					} catch (IOException e1) {
						// TODO Auto-generated catch block
						e1.printStackTrace();
					}
			        return res;
				}
			};
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		player = new MediaPlayer();
		
		try {
			player.setDataSource("http://localhost:8080");
			player.prepare();
	    	player.start();
		} catch (IllegalArgumentException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (SecurityException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (IllegalStateException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		
	}
	private void stop(){
		player.stop();
		server.stop();	
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		// Inflate the menu; this adds items to the action bar if it is present.
		getMenuInflater().inflate(R.menu.activity_play_url, menu);
		return true;
	}

}
