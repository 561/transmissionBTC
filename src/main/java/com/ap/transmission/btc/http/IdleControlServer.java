package com.ap.transmission.btc.http;

import static com.ap.transmission.btc.Utils.err;
import static com.ap.transmission.btc.Utils.info;

import com.ap.transmission.btc.Utils;
import com.ap.transmission.btc.torrent.RpcClient;
import com.ap.transmission.btc.torrent.Transmission;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class IdleControlServer implements java.io.Closeable {
  public static final int PORT = 9093;
  private static final String TAG = IdleControlServer.class.getName();
  private final Transmission transmission;
  private final RpcClient rpc;
  private volatile ServerSocket serverSocket;

  public IdleControlServer(Transmission transmission) {
    this.transmission = transmission;
    this.rpc = new RpcClient(transmission.getPrefs());
  }

  public void start() throws IOException {
    final ServerSocket ss = new ServerSocket(PORT);
    serverSocket = ss;
    Thread t = new Thread(() -> {
      while (!ss.isClosed()) {
        try {
          final Socket s = ss.accept();
          Thread h = new Thread(() -> handle(s), "idle-control-conn");
          h.setDaemon(true);
          h.start();
        } catch (IOException ex) {
          break;
        }
      }
    }, "idle-control");
    t.setDaemon(true);
    t.start();
  }

  @Override
  public void close() {
    ServerSocket s = serverSocket;
    serverSocket = null;
    if (s != null) Utils.close(s);
  }

  /** Returns "true" or "false" for a file of a torrent, null when the torrent or file is unknown. */
  private String deleteFile(String query) throws Exception {
    int id = -1;
    int index = -1;

    for (String pair : query.split("&")) {
      if (pair.startsWith("id=")) id = parseInt(pair.substring(3));
      else if (pair.startsWith("file=")) index = parseInt(pair.substring(5));
    }

    if (id < 0 || index < 0) return null;
    JSONObject args = new JSONObject().put("ids", new JSONArray().put(id))
        .put("fields", new JSONArray().put("files").put("downloadDir"));
    JSONArray torrents = rpc.call("torrent-get", args).getJSONObject("arguments")
        .getJSONArray("torrents");
    if (torrents.length() == 0) return null;
    JSONObject torrent = torrents.getJSONObject(0);
    JSONArray files = torrent.getJSONArray("files");
    if (index >= files.length()) return null;

    File root = new File(torrent.getString("downloadDir")).getCanonicalFile();
    File file = new File(root, files.getJSONObject(index).getString("name")).getCanonicalFile();
    if (!file.getPath().startsWith(root.getPath() + File.separator)) return null;
    boolean deleted = false;

    for (File candidate : new File[]{file, new File(file.getPath() + ".part")}) {
      if (candidate.isFile() && candidate.delete()) deleted = true;
    }

    info(TAG, "Delete %s: %s", file, deleted);
    return String.valueOf(deleted);
  }

  private static int parseInt(String value) {
    try {
      return Integer.parseInt(value);
    } catch (NumberFormatException ex) {
      return -1;
    }
  }

  private void handle(Socket s) {
    try {
      s.setSoTimeout(5000);
      BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.ISO_8859_1));
      String line = in.readLine();
      if (line == null) return;
      String[] parts = line.split(" ");
      String method = parts[0];
      String target = parts.length > 1 ? parts[1] : "/";
      int q = target.indexOf('?');
      String path = q >= 0 ? target.substring(0, q) : target;
      String query = q >= 0 ? target.substring(q + 1) : "";
      String bodyOverride = null;

      for (String h = in.readLine(); h != null && !h.isEmpty(); h = in.readLine()) {
        // skip headers
      }

      int status = 200;

      if ("OPTIONS".equals(method)) {
        status = 204;
      } else if ("POST".equals(method)) {
        switch (path) {
          case "/idle/run":
            transmission.setIdleOverride(Transmission.OVERRIDE_RUN);
            break;
          case "/idle/pause":
            transmission.setIdleOverride(Transmission.OVERRIDE_PAUSE);
            break;
          case "/idle/auto":
            transmission.setIdleOverride(Transmission.OVERRIDE_AUTO);
            break;
          case "/file/delete":
            String result = deleteFile(query);
            status = result == null ? 403 : 200;
            bodyOverride = "{\"deleted\":" + "true".equals(result) + "}";
            break;
          default:
            status = 404;
        }
      } else if (!"GET".equals(method) || !"/idle/status".equals(path)) {
        status = 404;
      }

      String text = bodyOverride != null ? bodyOverride : status == 200 ? transmission.getIdleStatusJson() : "";
      byte[] body = text.getBytes(StandardCharsets.UTF_8);
      String head = "HTTP/1.1 " + status + (status == 200 ? " OK" : status == 204 ? " No Content" : status == 403 ? " Forbidden" : " Not Found")
          + "\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *"
          + "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: *"
          + "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + body.length + "\r\n\r\n";
      OutputStream out = s.getOutputStream();
      out.write(head.getBytes(StandardCharsets.ISO_8859_1));
      out.write(body);
      out.flush();
    } catch (Throwable ex) {
      err(TAG, ex, "Idle control request failed");
    } finally {
      Utils.close(s);
    }
  }
}
