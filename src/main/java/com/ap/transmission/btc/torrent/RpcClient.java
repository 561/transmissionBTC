package com.ap.transmission.btc.torrent;

import android.util.Base64;

import com.ap.transmission.btc.Prefs;

import org.json.JSONObject;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class RpcClient {
  private final Prefs prefs;
  private String sessionId = "";

  public RpcClient(Prefs prefs) {
    this.prefs = prefs;
  }

  public synchronized JSONObject call(String method, JSONObject args) throws Exception {
    byte[] body = new JSONObject().put("method", method).put("arguments", args).toString()
        .getBytes(StandardCharsets.UTF_8);

    for (int attempt = 0; attempt < 2; attempt++) {
      URL url = new URL("http://127.0.0.1:" + prefs.getRpcPort() + "/transmission/rpc");
      HttpURLConnection c = (HttpURLConnection) url.openConnection();
      try {
        c.setConnectTimeout(5000);
        c.setReadTimeout(60000);
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setUseCaches(false);
        c.setRequestProperty("Connection", "close");
        c.setRequestProperty("Content-Type", "application/json");
        c.setFixedLengthStreamingMode(body.length);
        c.setRequestProperty("X-Transmission-Session-Id", sessionId);
        if (prefs.isRpcAuthEnabled()) {
          String cred = prefs.getRpcUsername() + ":" + prefs.getRpcPassword();
          c.setRequestProperty("Authorization", "Basic "
              + Base64.encodeToString(cred.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP));
        }
        try (OutputStream os = c.getOutputStream()) {
          os.write(body);
        }

        int code = c.getResponseCode();
        if (code == 409) {
          sessionId = c.getHeaderField("X-Transmission-Session-Id");
          InputStream es = c.getErrorStream();
          if (es != null) es.close();
          continue;
        }
        if (code != 200) throw new java.io.IOException("RPC HTTP " + code);

        try (InputStream is = c.getInputStream()) {
          java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
          byte[] buf = new byte[8192];
          for (int n; (n = is.read(buf)) > 0; ) out.write(buf, 0, n);
          return new JSONObject(out.toString("UTF-8"));
        }
      } finally {
        c.disconnect();
      }
    }

    throw new java.io.IOException("Session id negotiation failed");
  }
}
