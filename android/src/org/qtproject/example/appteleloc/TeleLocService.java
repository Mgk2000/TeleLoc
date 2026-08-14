package org.qtproject.example.appTeleLoc;

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
    private static final String CHANNEL_ID_SILENT = "TeleLocKeepAliveChannel";
    private static final String CHANNEL_ID_VOIP = "TeleLocVoipChannel";
    // СТАТИЧЕСКИЙ АВТОЗАПУСК: Поднимает службу из Java при создании окна Qt
    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "@@@ JAVA СЛУЖБА: onCreate() запущена");
        createNotificationChannel();
        startServerThread();
        
        // ХОТ-ФИКС ДЛЯ SAMSUNG И XIAOMI: Взводим вечный системный будильник-страж!
        scheduleStickyAlarm();
    }

    // Метод, который регистрирует в недрах Android автономный перезапуск рации
    private void scheduleStickyAlarm() {
        try {
            Context context = getApplicationContext();
            Intent restartIntent = new Intent(context, TeleLocWakeReceiver.class);
            restartIntent.setAction("org.qtproject.example.appTeleLoc.WAKE_UP_ACTION");
            int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
                ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE 
                : PendingIntent.FLAG_UPDATE_CURRENT;

            PendingIntent pendingIntent = PendingIntent.getBroadcast(context, 0, restartIntent, flags);
            android.app.AlarmManager alarm = (android.app.AlarmManager) context.getSystemService(Context.ALARM_SERVICE);

            if (alarm != null) {
                long triggerTime = System.currentTimeMillis() + 60000; // 60 секунд
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    alarm.setExactAndAllowWhileIdle(android.app.AlarmManager.RTC_WAKEUP, triggerTime, pendingIntent);
                } else {
                    alarm.setExact(android.app.AlarmManager.RTC_WAKEUP, triggerTime, pendingIntent);
                }
                Log.d(TAG, "@@@ СЛУЖБА СТРАЖ: Вечный AlarmManager взведен.");
            }
        } catch (Exception e) {
            Log.e(TAG, "Не удалось взвести цикличный AlarmManager: " + e.getMessage());
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
       Log.d(TAG, "@@@ JAVA СЛУЖБА: onStartCommand() вызвана");
        // Будильник-страж при старте подвязывается строго к ТИХОМУ каналу
        Notification notification = createVoipNotification("Рация активна в фоне", null, false);
        startForeground(1, notification);
        return START_STICKY;    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                // 1. СОЗДАЕМ ТИХИЙ КАНАЛ ДЛЯ БУДИЛЬНИКА (Чтобы не пищал каждые 10 секунд)
                NotificationChannel silentChannel = new NotificationChannel(
                    CHANNEL_ID_SILENT, "Дежурный режим рации", NotificationManager.IMPORTANCE_LOW);
                silentChannel.setSound(null, null);
                silentChannel.enableVibration(false);
                silentChannel.setLockscreenVisibility(Notification.VISIBILITY_SECRET);
                manager.createNotificationChannel(silentChannel);

                // 2. СОЗДАЕМ ГРОМКИЙ VoIP КАНАЛ ДЛЯ РЕАЛЬНОГО ЗВОНКА АНФИСЫ
                NotificationChannel voipChannel = new NotificationChannel(
                    CHANNEL_ID_VOIP, "Входящие вызовы TeleLoc", NotificationManager.IMPORTANCE_HIGH);
                
                // Назначаем стандартный системный рингтон звонка на уровне Android
                android.net.Uri defaultRingtoneUri = android.provider.Settings.System.DEFAULT_RINGTONE_URI;
                voipChannel.setSound(defaultRingtoneUri, new android.media.AudioAttributes.Builder()
                    .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .setUsage(android.media.AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                    .build());
                
                voipChannel.enableVibration(true);
                voipChannel.setVibrationPattern(new long[]{0, 500, 500, 500}); // Цикл вибрации звонка
                voipChannel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
                manager.createNotificationChannel(voipChannel);
            }
        }
    }

    // Добавлен флаг isRealCall для разделения каналов
    private Notification createVoipNotification(String text, PendingIntent fullScreenIntent, boolean isRealCall) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O 
            ? new Notification.Builder(this, isRealCall ? CHANNEL_ID_VOIP : CHANNEL_ID_SILENT) 
            : new Notification.Builder(this);

        builder.setContentTitle("TeleLoc Рация")
               .setContentText(text)
               .setSmallIcon(android.R.drawable.ic_menu_call);

        if (isRealCall && fullScreenIntent != null) {
            // Если это РЕАЛЬНЫЙ звонок Анфисы — врубаем нативные телефонные приоритеты
            builder.setCategory(Notification.CATEGORY_CALL)
                   .setPriority(Notification.PRIORITY_MAX)
                   .setFullScreenIntent(fullScreenIntent, true)
                   .setAutoCancel(true); 
                   
            // Дублируем системный звук для старых версий Android
            builder.setDefaults(Notification.DEFAULT_SOUND | Notification.DEFAULT_VIBRATE);
        } else {
            // Если это фоновое тиканье стража — сидим абсолютно беззвучно
            builder.setCategory(Notification.CATEGORY_SERVICE)
                   .setPriority(Notification.PRIORITY_MIN)
                   .setOngoing(true);
        }

        return builder.build();
    }
    private void startServerThread() {
        m_isRunning = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Log.d(TAG, "@@@ СЕТЬ JAVA: Пытаюсь открыть TCP порт 28500...");
                    m_serverSocket = new ServerSocket(28500);
                    Log.d(TAG, "@@@ СЕТЬ JAVA: ПОРТ 28500 УСПЕШНО ОТКРЫТ! Жду подключений...");

                    while (m_isRunning) {
                        Socket clientSocket = m_serverSocket.accept();
                        String remoteIp = clientSocket.getInetAddress().getHostAddress();
                        Log.d(TAG, "@@@ СЕТЬ JAVA: Есть подключение от IP: " + remoteIp);
                        
                        BufferedReader reader = new BufferedReader(new InputStreamReader(clientSocket.getInputStream(), "UTF-8"));
                        String rawData = reader.readLine();
                        Log.d(TAG, "@@@ СЕТЬ JAVA: Прочитана строка данных: " + (rawData != null ? rawData : "NULL"));
                        
                        clientSocket.close();

                        if (rawData != null && !rawData.trim().isEmpty()) {
                            try {
                                JSONObject obj = new JSONObject(rawData.trim());
                                String type = obj.optString("type");
                                String callerName = obj.optString("name");

                                if ("incoming_call".equals(type)) {
                                    Log.d(TAG, "@@@ СЕТЬ JAVA: Распознан входящий вызов от " + callerName + ". Запускаю triggerFullScreenCall");
                                    triggerFullScreenCall(callerName, remoteIp);
                                }
                            } catch (Exception e) {
                                Log.e(TAG, "@@@ ОШИБКА JAVA ПАРСИНГА JSON: " + e.getMessage());
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "@@@ КРИТИЧЕСКАЯ ОШИБКА JAVA СЕРВЕРА НА ПОРТУ 28500: " + e.getMessage());
                }
            }
        }).start();
    }
     private void triggerFullScreenCall(String callerName, String callerIp) {
        Log.d(TAG, "@@@ ОКНО JAVA: Активация триггера вызова для " + callerName);
        
        // 1. Аппаратно зажигаем дисплей смартфона на максимальную яркость на 10 секунд
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                android.os.PowerManager.WakeLock wl = pm.newWakeLock(
                    android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK | android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP, "TeleLoc::Wake");
                wl.acquire(10000);
                Log.d(TAG, "@@@ ОКНО JAVA: Аппаратный WakeLock экрана выполнен");
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка экрана: " + e.getMessage());
        }

        // 2. Включаем физический вибромотор смартфона на 600 мс
        try {
            android.os.Vibrator v = (android.os.Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(android.os.VibrationEffect.createOneShot(600, android.os.VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(600);
                }
                Log.d(TAG, "@@@ ОКНО JAVA: Вибромотор отработал");
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка вибратора: " + e.getMessage());
        }

        // 3. УМНЫЙ АВТОЗАПУСК: Автоматически определяем легальное имя класса активности из манифеста Qt 6.8.3
        Intent callIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        
        if (callIntent != null) {
            // Берем нативный компонент лаунчера и перенастраиваем его на VoIP-подъем из фона
            callIntent.setComponent(callIntent.getComponent());
            callIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK 
                              | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT 
                              | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                              
            callIntent.putExtra("caller_name", callerName);
            callIntent.putExtra("caller_ip", callerIp);
            Log.d(TAG, "@@@ ОКНО JAVA: Автоматически определен класс активности: " + callIntent.getComponent().getClassName());
        } else {
            // Резервный дефолтный вариант на случай сбоя менеджера пакетов
            callIntent = new Intent();
            callIntent.setClassName(this, "org.qtproject.qt.android.QtActivity");
            callIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            callIntent.putExtra("caller_name", callerName);
            callIntent.putExtra("caller_ip", callerIp);
            Log.e(TAG, "@@@ ОКНО JAVA ПРЕДУПРЕЖДЕНИЕ: getLaunchIntentForPackage вернул null, откат на дефолт");
        }

        // 4. Безопасный запуск Activity из контекста службы
        try {
            startActivity(callIntent);
            Log.d(TAG, "@@@ ОКНО JAVA: Вызван startActivity() — Интент успешно отправлен в систему!");
        } catch (Exception e) {
            Log.e(TAG, "Ошибка запуска activity: " + e.getMessage());
        }

        // 5. Выводим плашку уведомления в шторку с ID = 1
        // [Найти внутри метода triggerFullScreenCall в самом конце]
        final NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            int pendingFlags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
                ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE 
                : PendingIntent.FLAG_UPDATE_CURRENT;

            PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(this, 0, callIntent, pendingFlags);
            
            // ИСПРАВЛЕНО: Передаем true в самом конце, чтобы переключить плашку на громкий VoIP-канал со звуком рингтона!
            Notification notification = createVoipNotification("Входящий вызов от " + callerName, fullScreenPendingIntent, true);
            
            manager.notify(1, notification);
            Log.d(TAG, "@@@ ОКНО JAVA: Громкое VoIP-уведомление отправлено в систему менеджеру");

            // Продлим время жизни плашки звонка до 6 секунд, чтобы рингтон успел проиграться пару раз
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
                @Override
                public void run() {
                    try {
                        manager.cancel(1);
                        Log.d(TAG, "@@@ ОКНО JAVA: Временная громкая плашка удалена");
                    } catch (Exception e) { e.printStackTrace(); }
                }
            }, 6000); 
        }
    }

    @Override
    public void onDestroy() {
        m_isRunning = false;
        try {
            if (m_serverSocket != null) m_serverSocket.close();
        } catch (Exception e) { e.printStackTrace(); }

        Log.d(TAG, "@@@ СЛУЖБА УНИЧТОЖЕНА: Завожу AlarmManager на экстренное восстановление...");
        try {
            Context context = getApplicationContext();
            Intent restartIntent = new Intent(context, TeleLocWakeReceiver.class);
            restartIntent.setAction("org.qtproject.example.appTeleLoc.WAKE_UP_ACTION");
            
            int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S 
                ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE 
                : PendingIntent.FLAG_UPDATE_CURRENT;

            PendingIntent pendingIntent = PendingIntent.getBroadcast(context, 0, restartIntent, flags);
            android.app.AlarmManager alarm = (android.app.AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
            if (alarm != null) {
                long triggerTime = System.currentTimeMillis() + 5000; // Через 5 секунд
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    alarm.setExactAndAllowWhileIdle(android.app.AlarmManager.RTC_WAKEUP, triggerTime, pendingIntent);
                } else {
                    alarm.setExact(android.app.AlarmManager.RTC_WAKEUP, triggerTime, pendingIntent);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Не удалось взвести AlarmManager: " + e.getMessage());
        }

        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
