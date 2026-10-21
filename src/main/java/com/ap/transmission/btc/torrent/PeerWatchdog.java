package com.ap.transmission.btc.torrent;

import static com.ap.transmission.btc.Utils.err;

import android.util.Base64;

import com.ap.transmission.btc.Prefs;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

class PeerWatchdog {
  private static final String TAG = PeerWatchdog.class.getName();
  private static final int STATUS_DOWNLOAD = 4;
  static final long CHECK_PERIOD_SEC = 20;
  private static final long NO_PEERS_MS = 60_000;
  private static final long RETRY_MS = 60_000;

  private final Transmission transmission;
  private final Prefs prefs;
  private final Map<Integer, Long> noPeersSince = new HashMap<>();
  private final Map<Integer, Long> lastReannounce = new HashMap<>();
  private String sessionId = "";

  PeerWatchdog(Transmission transmission, Prefs prefs) {
    this.transmission = transmission;
    this.prefs = prefs;
  }

  void check() {
    if (!transmission.isRunning() || transmission.isSuspended() || !prefs.isRpcEnabled()) {
      noPeersSince.clear();
      return;
    }

    try {
      JSONObject args = new JSONObject().put("fields",
          new JSONArray().put("id").put("status").put("peersConnected"));
      JSONArray torrents = call("torrent-get", args).getJSONObject("arguments")
          .getJSONArray("torrents");
      long now = System.currentTimeMillis();
      JSONArray stalled = new JSONArray();

      for (int i = 0; i < torrents.length(); i++) {
        JSONObject t = torrents.getJSONObject(i);
        int id = t.getInt("id");

        if (t.getInt("status") != STATUS_DOWNLOAD || t.getInt("peersConnected") > 0) {
          noPeersSince.remove(id);
          continue;
        }

        long since = noPeersSince.computeIfAbsent(id, k -> now);
        long last = lastReannounce.getOrDefault(id, 0L);
        if (now - since >= NO_PEERS_MS && now - last >= RETRY_MS) {
          stalled.put(id);
          lastReannounce.put(id, now);
        }
      }

      if (stalled.length() > 0) {
        call("torrent-reannounce", new JSONObject().put("ids", stalled));
      }
    } catch (Exception ex) {
      err(TAG, ex, "Peer watchdog check failed");
    }
  }

  private JSONObject call(String method, JSONObject args) throws Exception {
    byte[] body = new JSONObject().put("method", method).put("arguments", args).toString()
        .getBytes(StandardCharsets.UTF_8);

    for (int attempt = 0; attempt < 2; attempt++) {
      URL url = new URL("http://127.0.0.1:" + prefs.getRpcPort() + "/transmission/rpc");
      HttpURLConnection c = (HttpURLConnection) url.openConnection();
      try {
        c.setConnectTimeout(5000);
        c.setReadTimeout(15000);
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setRequestProperty("X-Transmission-Session-Id", sessionId);
        if (prefs.isRpcAuthEnabled()) {
          String cred = prefs.getRpcUsername() + ":" + prefs.getRpcPassword();
          c.setRequestProperty("Authorization", "Basic "
              + Base64.encodeToString(cred.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP));
        }
        try (OutputStream os = c.getOutputStream()) {
          os.write(body);
        }

        if (c.getResponseCode() == 409) {
          sessionId = c.getHeaderField("X-Transmission-Session-Id");
          continue;
        }

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
