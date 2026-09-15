package org.qtproject.example.appTeleLoc;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class TeleLocWakeReceiver extends BroadcastReceiver {
    private static final String TAG = "TeleLocBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        Log.d(TAG, "Ресивер поймал: " + action + ". Запускаем службу...");

        // Создаем интент на службу через полное имя класса
        Intent serviceIntent = new Intent();
        serviceIntent.setClassName("org.qtproject.example.appTeleLoc", "org.qtproject.example.appTeleLoc.TeleLocService");

        try {
            // Запускаем как ОБЫЧНУЮ службу (не foreground). В Direct Boot это разрешено!
            context.startService(serviceIntent);
            Log.d(TAG, "Обычный старт TeleLocService выполнен.");
        } catch (Exception e) {
            Log.e(TAG, "Ошибка старта службы: " + e.getMessage());
        }
    }
}
