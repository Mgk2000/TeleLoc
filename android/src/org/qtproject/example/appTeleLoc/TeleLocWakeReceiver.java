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
        Log.w(TAG, "@@@rec 2 Ресивер поймал: " + action + ". Запускаем службу...");

        // Получаем контекст безопасного хранилища (Device Protected Storage)
        Context secureContext = context.createDeviceProtectedStorageContext();

        // СБОРКА ИНТЕНТА НАПРЯМУЮ ЧЕРЕЗ КЛАСС (Убирает ошибку not found)
        Intent serviceIntent = new Intent(secureContext, TeleLocService.class);

        try {
            // Запускаем службу
            secureContext.startService(serviceIntent);
            Log.d(TAG, "@@@rec Обычный старт TeleLocService выполнен.");
        } catch (Exception e) {
            Log.e(TAG, "@@@rec Ошибка старта службы: " + e.getMessage());
        }
    }
}
