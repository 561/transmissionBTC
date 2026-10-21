package com.ap.transmission.btc.receivers;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.PowerManager;

import com.ap.transmission.btc.services.TransmissionService;
import com.ap.transmission.btc.torrent.Transmission;

/**
 * @author Andrey Pavlenko
 */
public class IdleStateReceiver extends BroadcastReceiver {
  private static IdleStateReceiver instance;
  private static volatile boolean dreaming;
  private static volatile boolean screenOff;

  public static synchronized void register(Context context) {
    if (instance != null) return;
    PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
    screenOff = (pm != null) && !pm.isInteractive();
    dreaming = false;
    IntentFilter intentFilter = new IntentFilter();
    intentFilter.addAction(Intent.ACTION_DREAMING_STARTED);
    intentFilter.addAction(Intent.ACTION_DREAMING_STOPPED);
    intentFilter.addAction(Intent.ACTION_SCREEN_OFF);
    intentFilter.addAction(Intent.ACTION_SCREEN_ON);
    context.registerReceiver(instance = new IdleStateReceiver(), intentFilter);
  }

  public static synchronized void unregister(Context context) {
    if (instance == null) return;
    context.unregisterReceiver(instance);
    instance = null;
  }

  public static boolean isIdle() {
    return dreaming || screenOff;
  }

  @Override
  public void onReceive(Context context, Intent intent) {
    String action = intent.getAction();
    if (action == null) return;

    switch (action) {
      case Intent.ACTION_DREAMING_STARTED:
        dreaming = true;
        break;
      case Intent.ACTION_DREAMING_STOPPED:
        dreaming = false;
        break;
      case Intent.ACTION_SCREEN_OFF:
        screenOff = true;
        break;
      case Intent.ACTION_SCREEN_ON:
        screenOff = false;
        break;
      default:
        return;
    }

    Transmission tr = TransmissionService.getTransmission();
    if ((tr != null) && tr.isRunning()) tr.updateAutoSuspend();
  }
}
