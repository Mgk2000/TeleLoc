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
import android.os.PowerManager;
import android.util.Log;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;

public class TeleLocService extends Service {
    private static final String TAG = "TeleLocService";
    private static final String CHANNEL_ID_SILENT = "TeleLocSilentChannel";
    private static final String CHANNEL_ID_VOIP = "TeleLocVoipChannel";
    private static final int NOTIFICATION_ID = 9999;
    private ServerSocket m_serverSocket;
    private boolean m_isRunning = false;
private android.net.wifi.WifiManager.MulticastLock m_multicastLock;

 @Override
public void onCreate() {
    super.onCreate();
    Log.d(TAG, "@@@ JAVA СЛУЖБА: Вызов onCreate()");

    m_isRunning = true;

    try {
		Log.d(TAG, "@@@ JAVA СЛУЖБА: Попытка получить WifiManageк"); 
        android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
				Log.d(TAG, "@@@ JAVA СЛУЖБА: WifiManageк получен"); 

        if (wm != null) {
		Log.d(TAG, "@@@ JAVA СЛУЖБА: Попытка получить m_multicastLock"); 
            m_multicastLock = wm.createMulticastLock("TeleLoc:MulticastLock");
            m_multicastLock.acquire();
            Log.d(TAG, "@@@ JAVA СЛУЖБА: MulticastLock успешно получен.");
        }
		else
			Log.d(TAG, "@@@ JAVA СЛУЖБА: WifiManager = 0.");
    } catch (Exception e) {
        e.printStackTrace();
    }

    createNotificationChannel();
    Notification notification = createVoipNotification("Рация TeleLoc работает в дежурном режиме", null, false);
    startForeground(NOTIFICATION_ID, notification);

    new Thread(new Runnable() {
        @Override
        public void run() {
            startTcpServer();
        }
    }).start();

    startUdpReceiver();
}
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "@@@ JAVA СЛУЖБА: Вызов onStartCommand()");
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
    Log.d(TAG, "@@@ JAVA СЛУЖБА: Вызов onDestroy()");
    m_isRunning = false;
    
    if (m_multicastLock != null && m_multicastLock.isHeld()) {
        try {
            m_multicastLock.release();
            Log.d(TAG, "@@@ JAVA СЛУЖБА: MulticastLock успешно освобожден.");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    if (m_serverSocket != null) {
        try {
            m_serverSocket.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    super.onDestroy();
}


    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                NotificationChannel silentChannel = new NotificationChannel(
                    CHANNEL_ID_SILENT, "Дежурный режим рации", NotificationManager.IMPORTANCE_LOW);
                silentChannel.setSound(null, null);
                silentChannel.enableVibration(false);
                manager.createNotificationChannel(silentChannel);

                NotificationChannel voipChannel = new NotificationChannel(
                    CHANNEL_ID_VOIP, "Входящие вызовы TeleLoc", NotificationManager.IMPORTANCE_HIGH);
                voipChannel.enableVibration(true);
                voipChannel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
                manager.createNotificationChannel(voipChannel);
            }
        }
    }

    private Notification createVoipNotification(String text, PendingIntent fullScreenIntent, boolean isRealCall) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O 
            ? new Notification.Builder(this, isRealCall ? CHANNEL_ID_VOIP : CHANNEL_ID_SILENT) 
            : new Notification.Builder(this);

        builder.setContentTitle("TeleLoc Рация")
               .setContentText(text)
               .setSmallIcon(android.R.drawable.ic_menu_call);

        if (isRealCall && fullScreenIntent != null) {
            builder.setCategory(Notification.CATEGORY_CALL)
                   .setPriority(Notification.PRIORITY_MAX)
                   .setFullScreenIntent(fullScreenIntent, true)
                   .setAutoCancel(true)
                   .setOngoing(true);
            builder.setDefaults(Notification.DEFAULT_SOUND | Notification.DEFAULT_VIBRATE);
        } else {
            builder.setCategory(Notification.CATEGORY_SERVICE)
                   .setPriority(Notification.PRIORITY_MIN)
                   .setOngoing(true);
        }

        return builder.build();
    }

 private void startTcpServer() {
    new Thread(new Runnable() {
        @Override
        public void run() {
            while (m_isRunning) {
                try {
                    String myName = "Неизвестный";
                    String configPath = QStandardPaths_writableLocation();
                    java.io.File file = new java.io.File(configPath);
                    if (file.exists()) {
                        java.io.FileInputStream fis = new java.io.FileInputStream(file);
                        byte[] data = new byte[(int) file.length()];
                        fis.read(data);
                        fis.close();
                        org.json.JSONObject configObj = new org.json.JSONObject(new String(data, "UTF-8"));
                        String savedName = configObj.optString("my_name");
                        if (savedName != null && !savedName.isEmpty()) {
                            myName = savedName;
                        }
                    }

                    String myIp = getLocalIpAddress();
                    String json = "{\"type\":\"discovery\",\"name\":\"" + myName + "\",\"ip0\":\"" + myIp + "\",\"ip1\":\"\",\"ip2\":\"\"}";
                    byte[] bytes = json.getBytes("UTF-8");
                    java.net.DatagramSocket socket = new java.net.DatagramSocket();

                    socket.setBroadcast(true);
                    String[] ips = {"255.255.255.255", "192.168.43.255", "192.168.137.255"};
                    for (String ip : ips) {
                        java.net.InetAddress addr = java.net.InetAddress.getByName(ip);
                        java.net.DatagramPacket packet = new java.net.DatagramPacket(bytes, bytes.length, addr, 28000);
                        socket.send(packet);
                    }

                    if (file.exists()) {
                        java.io.FileInputStream fis = new java.io.FileInputStream(file);
                        byte[] data = new byte[(int) file.length()];
                        fis.read(data);
                        fis.close();
                        org.json.JSONObject configObj = new org.json.JSONObject(new String(data, "UTF-8"));
                        org.json.JSONArray peers = configObj.optJSONArray("peers");
                        if (peers != null) {
                            for (int i = 0; i < peers.length(); i++) {
                                org.json.JSONObject peer = peers.getJSONObject(i);
                                String pIp = peer.optString("ip0");
                                if (pIp != null && !pIp.isEmpty()) {
                                    java.net.InetAddress addr = java.net.InetAddress.getByName(pIp);
                                    java.net.DatagramPacket packet = new java.net.DatagramPacket(bytes, bytes.length, addr, 28000);
                                    socket.send(packet);
                                }
                            }
                        }
                    }
                    socket.close();
                    //Log.d(TAG, "@@@ JAVA СЛУЖБА: Адресный пакет Discovery отправлен пирам.");
                    Thread.sleep(30000);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }).start();

    m_isRunning = true;
    try {
        m_serverSocket = new ServerSocket(28500);
        Log.d(TAG, "@@@ JAVA СЛУЖБА: TCP Сервер запущен на порту 28500");

        while (m_isRunning) {
            Socket clientSocket = m_serverSocket.accept();
            String remoteIp = clientSocket.getInetAddress().getHostAddress();
            BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream(), "UTF-8"));
            String line = in.readLine();
            if (line != null) {
                try {
                    org.json.JSONObject obj = new org.json.JSONObject(line);
                    if ("incoming_call".equals(obj.optString("type"))) {
                        triggerFullScreenCall(obj.optString("name", "Некто"), remoteIp);
                    }
                } catch (Exception e) {}
            }
            clientSocket.close();
        }
    } catch (Exception e) {}
}




    public void triggerFullScreenCall(String callerName, String remoteIp) {
        Log.d(TAG, "@@@ JAVA СЛУЖБА: Вызов triggerFullScreenCall() для: " + callerName + " (" + remoteIp + ")");
        
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            PowerManager.WakeLock wl = pm.newWakeLock(
                PowerManager.SCREEN_BRIGHT_WAKE_LOCK | PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.ON_AFTER_RELEASE, 
                "TeleLoc:CallWakeLock"
            );
            wl.acquire(5000);
        }

        Intent callIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (callIntent == null) {
            Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА: Не удалось получить Launch Intent для приложения!");
            return;
        }

        callIntent.setAction("org.qtproject.example.appteleloc.WAKE_UP_ACTION");
        callIntent.putExtra("callerName", callerName);
        callIntent.putExtra("remoteIp", remoteIp);
        callIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);

        try {
            Log.d(TAG, "@@@ JAVA СЛУЖБА: Попытка фонового вызова startActivity()...");
            startActivity(callIntent);
        } catch (Exception e) {
            Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА запуска startActivity(): " + e.getMessage());
        }

        final NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            int pendingFlags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE
                : PendingIntent.FLAG_UPDATE_CURRENT;

            PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(this, 0, callIntent, pendingFlags);
            
            Notification notification = createVoipNotification("Входящий вызов от " + callerName, fullScreenPendingIntent, true);
            manager.notify(1, notification);
            Log.d(TAG, "@@@ ОКНО JAVA: Громкая плашка выслана менеджеру");

            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
                @Override
                public void run() {
                    try {
                        manager.cancel(1);
                        Log.d(TAG, "@@@ ОКНО JAVA: Временная громкая плашка удалена");
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            }, 6000);
        }
    }
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        Log.d(TAG, "@@@ JAVA СЛУЖБА: Пользователь смахнул приложение! Планирую перезапуск...");
        Intent restartServiceIntent = new Intent(getApplicationContext(), this.getClass());
        restartServiceIntent.setPackage(getPackageName());
        PendingIntent restartServicePendingIntent = PendingIntent.getService(
            getApplicationContext(), 1, restartServiceIntent, 
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_MUTABLE : PendingIntent.FLAG_ONE_SHOT
        );
        android.app.AlarmManager alarmManager = (android.app.AlarmManager) getApplicationContext().getSystemService(Context.ALARM_SERVICE);
        if (alarmManager != null) {
            alarmManager.set(android.app.AlarmManager.RTC_WAKEUP, System.currentTimeMillis() + 1000, restartServicePendingIntent);
        }
        super.onTaskRemoved(rootIntent);
    }
private String getLocalIpAddress() {
    try {
        java.util.List<java.net.NetworkInterface> interfaces = java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces());
        for (java.net.NetworkInterface intf : interfaces) {
            java.util.List<java.net.InetAddress> addrs = java.util.Collections.list(intf.getInetAddresses());
            for (java.net.InetAddress addr : addrs) {
                if (!addr.isLoopbackAddress()) {
                    String sAddr = addr.getHostAddress();
                    boolean isIPv4 = sAddr.indexOf(':') < 0;
                    if (isIPv4) return sAddr;
                }
            }
        }
    } catch (Exception e) {
        e.printStackTrace();
    }
    return "";
}
    private String QStandardPaths_writableLocation() {
        return getFilesDir().getParent() + "/files/teleloc.conf";
    }
private void saveConfigToFile(org.json.JSONObject configObj) {
    try {
        String configPath = QStandardPaths_writableLocation();
        java.io.FileOutputStream fos = new java.io.FileOutputStream(configPath);
        fos.write(configObj.toString().getBytes("UTF-8"));
        fos.close();
        //Log.d(TAG, "@@@ JAVA СЛУЖБА: Конфиг успешно обновлен на диске.");
    } catch (Exception e) {
        e.printStackTrace();
    }
}
private void startUdpReceiver() {
    new Thread(new Runnable() {
        @Override
        public void run() {
            try {
                java.net.DatagramSocket socket = new java.net.DatagramSocket(28000);
                socket.setReuseAddress(true);
                byte[] buffer = new byte[4096];
                Log.d(TAG, "@@@ JAVA СЛУЖБА: UDP Приемник Discovery запущен на порту 28000");

                while (m_isRunning) {
                    java.net.DatagramPacket packet = new java.net.DatagramPacket(buffer, buffer.length);
                    socket.receive(packet);
                    
                    String message = new String(packet.getData(), 0, packet.getLength(), "UTF-8").trim();
                    Log.d(TAG, "@@@ JAVA СЛУЖБА: Получен UDP пакет: " + message);

                    try {
                        org.json.JSONObject obj = new org.json.JSONObject(message);
                        if ("discovery".equals(obj.optString("type"))) {
                            String pName = obj.optString("name");
                            String pIp = obj.optString("ip0");
                            
                            if (pName != null && !pName.isEmpty() && pIp != null && !pIp.isEmpty()) {
                                Log.d(TAG, "@@@ JAVA СЛУЖБА: Обновляю пира в конфиге: " + pName + " -> " + pIp);
                                updatePeerInConfig(pName, pIp);
                            }
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА парсинга JSON: " + e.getMessage());
                    }
                }
                socket.close();
            } catch (Exception e) {
                Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА UDP приемника: " + e.getMessage());
            }
        }
    }).start();
}
private void updatePeerInConfig(String name, String ip) {
    try {
        String configPath = QStandardPaths_writableLocation();
        java.io.File file = new java.io.File(configPath);
        org.json.JSONObject configObj = null;
        
        if (file.exists() && file.length() > 0) {
            java.io.FileInputStream fis = new java.io.FileInputStream(file);
            byte[] data = new byte[(int) file.length()];
            fis.read(data);
            fis.close();
            configObj = new org.json.JSONObject(new String(data, "UTF-8"));
        } else {
            configObj = new org.json.JSONObject();
        }

        org.json.JSONArray peers = configObj.optJSONArray("peers");
        if (peers == null) {
            peers = new org.json.JSONArray();
            configObj.put("peers", peers);
        }

        boolean found = false;
        for (int i = 0; i < peers.length(); i++) {
            org.json.JSONObject peer = peers.getJSONObject(i);
            if (name.equals(peer.optString("name"))) {
                peer.put("ip0", ip);
                peer.put("isAlive", true); // ИСПРАВЛЕНО: обновляем флаг активности
                found = true;
                break;
            }
        }

        if (!found) {
            org.json.JSONObject newPeer = new org.json.JSONObject();
            newPeer.put("name", name);
            newPeer.put("ip0", ip);
            newPeer.put("ip1", "");
            newPeer.put("ip2", "");
            newPeer.put("isAlive", true); // ИСПРАВЛЕНО: выставляем флаг при создании
            peers.put(newPeer);
        }

        java.io.FileOutputStream fos = new java.io.FileOutputStream(file);
        fos.write(configObj.toString().getBytes("UTF-8"));
        fos.close();
        Log.d(TAG, "@@@ JAVA СЛУЖБА: Конфиг успешно перезаписан. Пир " + name + " добавлен/обновлен.");
    } catch (Exception e) {
        Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА внутри updatePeerInConfig: " + e.getMessage());
        e.printStackTrace();
    }
}

}