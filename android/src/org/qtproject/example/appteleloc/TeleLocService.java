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
                    m_serverSocket = new ServerSocket(28500);
                    Log.d(TAG, "Java TCP-будильник запущен на порту 28500");

                    while (m_isRunning) {
                        Socket clientSocket = m_serverSocket.accept();
                        String remoteIp = clientSocket.getInetAddress().getHostAddress();
                        
                        BufferedReader reader = new BufferedReader(new InputStreamReader(clientSocket.getInputStream(), "UTF-8"));
                        String rawData = reader.readLine();
                        clientSocket.close();

                        if (rawData != null && !rawData.trim().isEmpty()) {
                            try {
                                JSONObject obj = new JSONObject(rawData.trim());
                                String type = obj.optString("type");
                                String callerName = obj.optString("name");

                                if ("incoming_call".equals(type)) {
                                    // ИСПРАВЛЕНО: Передаем ОБА параметра (Имя и IP)
                                    triggerFullScreenCall(callerName, remoteIp);
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

     private void triggerFullScreenCall(String callerName, String callerIp) {
        // 1. Аппаратно зажигаем дисплей смартфона
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                android.os.PowerManager.WakeLock wl = pm.newWakeLock(
                    android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK | android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP, "TeleLoc::Wake");
                wl.acquire(10000);
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка экрана: " + e.getMessage());
        }

        // 2. Включаем физический вибромотор
        try {
            android.os.Vibrator v = (android.os.Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(android.os.VibrationEffect.createOneShot(600, android.os.VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(600);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка вибратора: " + e.getMessage());
        }

        // 3. Формируем Intent на запуск нашего главного QML/C++ окна QtActivity
        Intent callIntent = new Intent();
        callIntent.setClassName(this, "org.qtproject.qt.android.QtActivity");
        callIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK 
                          | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT 
                          | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                          
        callIntent.putExtra("caller_name", callerName);
        callIntent.putExtra("caller_ip", callerIp); 

        try {
            // СНАЧАЛА принудительно поднимаем весь таск приложения на передний план ОС, 
            // если оно уже свернуто в списке запущенных!
            android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null) {
                java.util.List<android.app.ActivityManager.RunningTaskInfo> tasks = am.getRunningTasks(10);
                for (android.app.ActivityManager.RunningTaskInfo task : tasks) {
                    if (task.baseActivity != null && task.baseActivity.getPackageName().equals(getPackageName())) {
                        // Поднимаем существующее окно рации из фона на самый верх экрана
                        am.moveTaskToFront(task.id, android.app.ActivityManager.MOVE_TASK_WITH_HOME);
                        break;
                    }
                }
            }
            
            // Затем дублируем запуск интента
            startActivity(callIntent);
            Log.d(TAG, "Java форсированно подняла задачу QtActivity на передний план");
        } catch (Exception e) {
            Log.e(TAG, "Ошибка подъема окна: " + e.getMessage());
        }

        int pendingFlags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
            ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE 
            : PendingIntent.FLAG_UPDATE_CURRENT;

        PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(this, 0, callIntent, pendingFlags);

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
