package org.qtproject.example.appTeleLoc;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import org.qtproject.qt.android.bindings.QtActivity;

public class TeleLocActivity extends QtActivity {
    private static final String TAG = "TeleLocActivity";

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.d(TAG, "@@@ [Java Activity] Главное окно запущенно. Стартуем службу связи...");

        try {
            Context appContext = getApplicationContext();
            Intent serviceIntent = new Intent(appContext, TeleLocService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                appContext.startForegroundService(serviceIntent);
            } else {
                appContext.startService(serviceIntent);
            }
            Log.d(TAG, "@@@ [Java Activity] Служба TeleLocService успешно запущена.");
        } catch (Exception e) {
            Log.e(TAG, "!!! [Java Activity] Ошибка запуска службы: " + e.getMessage());
        }
    }
}
