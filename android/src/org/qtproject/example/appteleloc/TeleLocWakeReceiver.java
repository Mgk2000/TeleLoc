package org.qtproject.example.appTeleLoc;

import android.app.AlarmManager;
import android.app.PendingIntent;
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
        if (intent != null && "org.qtproject.example.appTeleLoc.WAKE_UP_ACTION".equals(intent.getAction())) {
            Log.d(TAG, "@@@ СИСТЕМНЫЙ БУДИЛЬНИК: Сработал вечный страж Android. Проверяю службу...");
            
            try {
                Intent serviceIntent = new Intent(context, TeleLocService.class);
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    context.startForegroundService(serviceIntent);
                } else {
                    context.startService(serviceIntent);
                }
                
                // ЗАКОЛЬЦОВЫВАНИЕ: Перевзводим AlarmManager на следующую минуту вперед
                AlarmManager alarm = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
                if (alarm != null) {
                    int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
                        ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE 
                        : PendingIntent.FLAG_UPDATE_CURRENT;
                    
                    PendingIntent pendingIntent = PendingIntent.getBroadcast(context, 0, intent, flags);
                    long nextTrigger = System.currentTimeMillis() + 10000; // Еще через 60 секунд
                    
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                        alarm.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, nextTrigger, pendingIntent);
                    } else {
                        alarm.setExact(AlarmManager.RTC_WAKEUP, nextTrigger, pendingIntent);
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Ошибка вечного цикла будильника: " + e.getMessage());
            }
        }
    }
}
