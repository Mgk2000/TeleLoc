package org.qtproject.example.appteleloc;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import org.json.JSONObject;

public class TeleLocService extends Service {
    private static final String TAG = "TeleLocService";
    private static final String CHANNEL_ID = "TeleLocVoipChannel";
    private ServerSocket m_serverSocket;
    private boolean m_isRunning = false;

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        startServerThread();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = createVoipNotification("Рация активна в фоне", null);
        startForeground(1, notification);
        return START_STICKY;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Входящие вызовы TeleLoc", NotificationManager.IMPORTANCE_HIGH);
            channel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
            channel.enableLights(true);
            channel.enableVibration(true);
            
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification createVoipNotification(String text, PendingIntent fullScreenIntent) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O 
            ? new Notification.Builder(this, CHANNEL_ID) 
            : new Notification.Builder(this);

        builder.setContentTitle("TeleLoc Рация")
               .setContentText(text)
               .setSmallIcon(android.R.drawable.ic_menu_call)
               .setCategory(Notification.CATEGORY_CALL)
               .setPriority(Notification.PRIORITY_HIGH)
               .setOngoing(true);

        if (fullScreenIntent != null) {
            builder.setFullScreenIntent(fullScreenIntent, true);

            // СТРОГАЯ ОФИЦИАЛЬНАЯ VoIP-СПЕЦИФИКАЦИЯ ANDROID 14 ДЛЯ ПРОБИТИЯ СНА
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                Intent hangupIntent = new Intent(this, TeleLocService.class);
                PendingIntent declinePendingIntent = PendingIntent.getService(this, 1, hangupIntent, PendingIntent.FLAG_IMMUTABLE);
                
                android.app.Person incomingCaller = new android.app.Person.Builder()
                    .setName(text.replace("Входящий вызов от ", ""))
                    .setImportant(true)
                    .build();

                // Обертка в CallStyle заставляет ядро Android зажечь экран и вывести QML окно
                builder.setStyle(Notification.CallStyle.forIncomingCall(
                    incomingCaller, declinePendingIntent, fullScreenIntent));
            }
        }

        return builder.build();
    }

    private void startServerThread() {
        m_isRunning = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    // Короткий, чистый TCP-сервер будильника на порту 28500
                    m_serverSocket = new ServerSocket(28500);
                    Log.d(TAG, "Java TCP-будильник запущен на порту 28500");

                    while (m_isRunning) {
                        Socket clientSocket = m_serverSocket.accept();
                        BufferedReader reader = new BufferedReader(new InputStreamReader(clientSocket.getInputStream(), "UTF-8"));
                        
                        // Читаем одну строку до символа \n и мгновенно отпускаем сетевую плату девайса
                        String rawData = reader.readLine();
                        clientSocket.close();

                        if (rawData != null && !rawData.trim().isEmpty()) {
                            try {
                                JSONObject obj = new JSONObject(rawData.trim());
                                String type = obj.optString("type");
                                String callerName = obj.optString("name");

                                if ("incoming_call".equals(type)) {
                                    triggerFullScreenCall(callerName);
                                }
                            } catch (Exception e) {
                                Log.e(TAG, "Ошибка парсинга: " + e.getMessage());
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Критическая ошибка сервера: " + e.getMessage());
                }
            }
        }).start();
    }

    private void triggerFullScreenCall(String callerName) {
        // Аппаратно зажигаем дисплей смартфона на максимальную яркость на 8 секунд
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                android.os.PowerManager.WakeLock wl = pm.newWakeLock(
                    android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK | android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP, "TeleLoc::Wake");
                wl.acquire(8000);
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка экрана: " + e.getMessage());
        }

        // Включаем физический вибромотор смартфона на 500 мс
        try {
            android.os.Vibrator v = (android.os.Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(android.os.VibrationEffect.createOneShot(500, android.os.VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(500);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка вибратора: " + e.getMessage());
        }

        // Формируем Intent на запуск нашего главного QML/C++ окна QtActivity
        Intent callIntent = new Intent();
        callIntent.setClassName(this, "org.qtproject.qt.android.QtActivity");
        callIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK 
                          | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT 
                          | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        callIntent.putExtra("caller_name", callerName);

        int pendingFlags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
            ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE 
            : PendingIntent.FLAG_UPDATE_CURRENT;

        PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(
            this, 0, callIntent, pendingFlags);

        // Обновляем плашку уведомления, пихая туда CallStyle-контракт
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            Notification notification = createVoipNotification("Входящий вызов от " + callerName, fullScreenPendingIntent);
            manager.notify(1, notification);
        }
    }

    @Override
    public void onDestroy() {
        m_isRunning = false;
        try {
            if (m_serverSocket != null) m_serverSocket.close();
        } catch (Exception e) { e.printStackTrace(); }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
