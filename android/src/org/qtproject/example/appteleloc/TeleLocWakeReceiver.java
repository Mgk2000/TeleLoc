package org.qtproject.example.appTeleLoc;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;
import org.qtproject.example.appTeleLoc.TeleLocService;

public class TeleLocWakeReceiver extends BroadcastReceiver {
    private static final String TAG = "TeleLocWakeReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        Log.d(TAG, "@@@ РЕСИВЕР: Система включилась! Поднимаю фоновую службу...");
        Intent serviceIntent = new Intent(context, TeleLocService.class);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent);
            } else {
                context.startService(serviceIntent);
            }
            Log.d(TAG, "@@@ РЕСИВЕР: Команда на запуск службы отправлена успешно.");
        } catch (Exception e) {
            Log.e(TAG, "@@@ РЕСИВЕР ОШИБКА запуска службы: " + e.getMessage());
        }
    }
}
