package com.ap.transmission.btc;

import android.content.Context;
import android.os.Process;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public final class AppLog {
  private static final String TAG = "AppLog";
  private static final long KEEP_MS = 2L * 24 * 60 * 60 * 1000;
  private static final long MAX_DAY_BYTES = 50L * 1024 * 1024;
  private static Thread thread;

  private AppLog() {
  }

  public static synchronized void start(Context ctx) {
    if (thread != null) return;
    File dir = ctx.getExternalFilesDir("logs");
    if (dir == null) dir = new File(ctx.getFilesDir(), "logs");
    if (!dir.isDirectory() && !dir.mkdirs()) {
      Log.e(TAG, "Cannot create " + dir);
      return;
    }
    File logDir = dir;
    thread = new Thread(() -> run(logDir), "AppLog");
    thread.setDaemon(true);
    thread.start();
  }

  private static void run(File dir) {
    SimpleDateFormat day = new SimpleDateFormat("yyyy-MM-dd", Locale.US);
    while (true) {
      java.lang.Process p = null;
      try {
        p = new ProcessBuilder("logcat", "-v", "year", "-T", "1",
            "--pid=" + Process.myPid()).redirectErrorStream(true).start();
        BufferedReader in = new BufferedReader(new InputStreamReader(p.getInputStream(),
            StandardCharsets.UTF_8));
        String current = null;
        OutputStream out = null;
        long written = 0;
        try {
          for (String line; (line = in.readLine()) != null; ) {
            String today = day.format(new Date());
            if (!today.equals(current)) {
              if (out != null) out.close();
              current = today;
              File f = new File(dir, "app-" + today + ".log");
              written = f.length();
              out = new FileOutputStream(f, true);
              cleanup(dir);
            }
            if (written > MAX_DAY_BYTES) continue;
            byte[] b = (line + "\n").getBytes(StandardCharsets.UTF_8);
            out.write(b);
            out.flush();
            written += b.length;
          }
        } finally {
          if (out != null) out.close();
        }
      } catch (IOException ex) {
        Log.e(TAG, "logcat reader failed", ex);
      } finally {
        if (p != null) p.destroy();
      }
      try {
        Thread.sleep(5000);
      } catch (InterruptedException ex) {
        return;
      }
    }
  }

  private static void cleanup(File dir) {
    File[] files = dir.listFiles();
    if (files == null) return;
    long cutoff = System.currentTimeMillis() - KEEP_MS;
    for (File f : files) {
      if (f.getName().startsWith("app-") && f.lastModified() < cutoff && !f.delete()) {
        Log.w(TAG, "Cannot delete " + f);
      }
    }
  }
}
