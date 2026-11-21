package com.ap.transmission.btc.torrent;

import static com.ap.transmission.btc.Utils.err;
import static com.ap.transmission.btc.Utils.warn;

import com.ap.transmission.btc.Prefs;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

class PeerWatchdog {
  private static final String TAG = PeerWatchdog.class.getName();
  private static final int STATUS_DOWNLOAD = 4;
  static final long CHECK_PERIOD_SEC = 20;
  private static final long NO_PEERS_MS = 60_000;
  private static final long RETRY_MS = 60_000;
  private static final long STALL_RESTART_MS = 15 * 60_000;
  private static final long RESTART_GAP_MS = 60 * 60_000;
  private static final int LEAK_CHECKS = 3;
  private static long lastRestart;

  private final Transmission transmission;
  private final Prefs prefs;
  private final Map<Integer, Long> noPeersSince = new HashMap<>();
  private final Map<Integer, Long> lastReannounce = new HashMap<>();
  private final RpcClient rpc;
  private long stallSince;
  private int leakChecks;

  PeerWatchdog(Transmission transmission, Prefs prefs) {
    this.transmission = transmission;
    this.prefs = prefs;
    this.rpc = new RpcClient(prefs);
  }

  void check() {
    if (!transmission.isRunning() || transmission.isSuspended() || !prefs.isRpcEnabled()) {
      noPeersSince.clear();
      stallSince = 0;
      leakChecks = 0;
      return;
    }

    if (checkSocketLeak()) return;

    try {
      JSONObject args = new JSONObject().put("fields",
          new JSONArray().put("id").put("status").put("peersConnected").put("trackerStats"));
      JSONArray torrents = rpc.call("torrent-get", args).getJSONObject("arguments")
          .getJSONArray("torrents");
      long now = System.currentTimeMillis();
      JSONArray stalled = new JSONArray();
      int downloading = 0;
      boolean connected = false;
      boolean trackerHasPeers = false;

      for (int i = 0; i < torrents.length(); i++) {
        JSONObject t = torrents.getJSONObject(i);
        int id = t.getInt("id");

        if (t.getInt("status") == STATUS_DOWNLOAD) {
          downloading++;
          if (t.getInt("peersConnected") > 0) connected = true;
          else if (trackerPeers(t) > 0) trackerHasPeers = true;
        }

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

      if (downloading > 0 && !connected && trackerHasPeers) {
        if (stallSince == 0) stallSince = now;
        if (now - stallSince >= STALL_RESTART_MS && restart(String.format(java.util.Locale.ROOT,
            "%d downloading torrents without peers for %d min while trackers report peers",
            downloading, (now - stallSince) / 60_000))) {
          return;
        }
      } else {
        stallSince = 0;
      }

      if (stalled.length() > 0) {
        rpc.call("torrent-reannounce", new JSONObject().put("ids", stalled));
      }
    } catch (Exception ex) {
      err(TAG, ex, "Peer watchdog check failed");
    }
  }

  private boolean checkSocketLeak() {
    int[] s = transmission.getPeerSocketStats();
    if (s == null || s[1] <= 0) return false;
    int sockets = s[0], limit = s[1], peers = s[2], handshakes = s[3];
    int leaked = sockets - peers - handshakes;

    if (sockets >= limit * 9 / 10 && leaked >= limit / 2) {
      leakChecks++;
      warn(TAG, "Peer sockets near the limit: sockets=%d/%d peers=%d handshakes=%d (check %d)",
          sockets, limit, peers, handshakes, leakChecks);
    } else {
      leakChecks = 0;
    }

    return leakChecks >= LEAK_CHECKS && restart(String.format(java.util.Locale.ROOT,
        "socket counter at %d/%d with only %d peers and %d handshakes", sockets, limit, peers,
        handshakes));
  }

  private static int trackerPeers(JSONObject t) {
    JSONArray stats = t.optJSONArray("trackerStats");
    if (stats == null) return 0;
    int max = 0;
    for (int i = 0; i < stats.length(); i++) {
      JSONObject s = stats.optJSONObject(i);
      if (s != null && s.optBoolean("lastAnnounceSucceeded")) {
        max = Math.max(max, s.optInt("lastAnnouncePeerCount"));
      }
    }
    return max;
  }

  private boolean restart(String reason) {
    long now = System.currentTimeMillis();
    synchronized (PeerWatchdog.class) {
      if (lastRestart != 0 && now - lastRestart < RESTART_GAP_MS) return false;
      lastRestart = now;
    }
    warn(TAG, "Engine looks stuck: %s", reason);
    transmission.restartEngine(reason);
    return true;
  }
}
