package org.qtproject.example.appTeleLoc;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;

public class TeleLocService extends android.app.Service {
    private static final String TAG = "TeleLocService";
    private static TeleLocService instance = null;

    public static TeleLocService getInstance() { return instance; }

    private class UserInfo {
        String name = "";
        String[] ip = {"", "", ""};
        long lastPing = 0;
        boolean android;
        public void setName(String newName) { this.name = newName; }
    }

    private List<UserInfo> users = new ArrayList<>();
    public static final String CHANNEL_ID_SILENT = "TeleLocSilentChannel";
    public static final String CHANNEL_ID_VOIP = "TeleLocVoipChannel";
    public static final int NOTIFICATION_ID = 9999;
    private ServerSocket m_serverSocket;
    private boolean m_isRunning = false;
    private android.os.PowerManager.WakeLock m_wakeLock = null;
    private android.net.wifi.WifiManager.MulticastLock m_multicastLock = null;
    private android.net.wifi.WifiManager.WifiLock m_wifiLock = null;
    private int TCP_PORT = 28501;
    private ServerSocket serverSocket;
    private Thread serverThread;
    String configPath = "";
    org.json.JSONObject lastCall = null;
    long lastCallTime = 0;
    public int cnt = 0;

    public static native void sendDataToCpp(Object serviceObj, int eventId, String payloadData);

    public org.json.JSONObject readJsonConfig() {
        try {
            java.io.File file = new java.io.File(configPath);
            if (file.exists()) {
                java.io.FileInputStream fis = new java.io.FileInputStream(file);
                byte[] data = new byte[(int) file.length()];
                fis.read(data);
                fis.close();
                return new org.json.JSONObject(new String(data, "UTF-8"));
            }
        } catch (Exception e) { Log.e(TAG, "Ошибка чтения JSON: " + e.getMessage()); }
        return new org.json.JSONObject();
    }
    private void excludeUserDups(int ind){
        UserInfo ui = users.get(ind);
//        Log.w(TAG, "@@@Exclude " + ui.name + " " + ui.ip[0]);
        for (int i =0; i< users.size(); i++)
        {
            if (i == ind) continue;
            UserInfo u = users.get(i);
 //           Log.d(TAG,"@@@excl " + i + u.name + " " + u.ip[0] + (u.ip[0] == ui.ip[0]));
            if (u.name.equals(ui.name) || (!u.ip[0].equals("") && u.ip[0].equals(ui.ip[0]))
                         || (!u.ip[1].equals("") && u.ip[1].equals(ui.ip[1]))
                         || (!u.ip[2].equals("") && u.ip[2].equals(ui.ip[2])))
                {
 //                   Log.d(TAG, "@@@Excl removed");
                    users.remove(i); i--;}

        }
    }
    public void saveConfig(org.json.JSONObject obj) {
        try {
            java.io.FileOutputStream fos = new java.io.FileOutputStream(configPath);
          //  Log.d(TAG, "@@@Configpath JAVA=" + configPath);
          //  Log.d(TAG, "@@@saveConfig " + obj.toString());
            fos.write(obj.toString().getBytes("UTF-8"));
            fos.close();
        } catch (Exception e) { Log.e(TAG, "saveConfig error: " + e.getMessage()); }
    }

    public void readConfig() {
        try {
            java.io.File file = new java.io.File(configPath);
            if (file.exists()) {
                java.io.FileInputStream fis = new java.io.FileInputStream(file);
                byte[] data = new byte[(int) file.length()];
                fis.read(data);
                fis.close();
                org.json.JSONObject configObj = new org.json.JSONObject(new String(data, "UTF-8"));
                org.json.JSONArray peerArr = configObj.getJSONArray("peers");
                for (int i = 0; i < peerArr.length(); i++) {
                    org.json.JSONObject peer = peerArr.getJSONObject(i);
                    UserInfo user = new UserInfo();
                    user.name = peer.optString("name");
                    user.lastPing = peer.optLong("lastPing");
                    user.android = peer.optBoolean("android", true);
                    org.json.JSONArray ipArr = peer.getJSONArray("ip");
                    for (int j = 0; j < 3; j++) user.ip[j] = ipArr.getString(j);
                    users.add(user);
                }
                lastCall = configObj.optJSONObject("lastCall");
            }
        } catch (Exception e) { Log.e(TAG, "readConfig error: " + e.getMessage()); }
    }

    public void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                NotificationChannel silentChannel = new NotificationChannel(CHANNEL_ID_SILENT, "Дежурный режим рации", NotificationManager.IMPORTANCE_LOW);
                silentChannel.setSound(null, null);
                silentChannel.enableVibration(false);
                manager.createNotificationChannel(silentChannel);
                NotificationChannel voipChannel = new NotificationChannel(CHANNEL_ID_VOIP, "Входящие вызовы TeleLoc", NotificationManager.IMPORTANCE_HIGH);
                voipChannel.enableVibration(true);
                voipChannel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
                manager.createNotificationChannel(voipChannel);
            }
        }
    }

    public Notification createVoipNotification(String text, PendingIntent fullScreenIntent, boolean isRealCall) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ? new Notification.Builder(this, isRealCall ? CHANNEL_ID_VOIP : CHANNEL_ID_SILENT) : new Notification.Builder(this);
        builder.setContentTitle("TeleLoc Рация").setContentText(text).setSmallIcon(android.R.drawable.ic_menu_call);
        if (isRealCall && fullScreenIntent != null) {
            builder.setCategory(Notification.CATEGORY_CALL).setPriority(Notification.PRIORITY_MAX).setFullScreenIntent(fullScreenIntent, true).setAutoCancel(true).setOngoing(true);
        } else {
            builder.setCategory(Notification.CATEGORY_SERVICE).setPriority(Notification.PRIORITY_MIN).setOngoing(true);
        }
        return builder.build();
    }
    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        m_isRunning = true;
        Log.d(TAG, "@@@ [Java Service] onCreate() запущен в едином процессе приложения");

        try {
            Context deviceProtectedContext = this.createDeviceProtectedStorageContext();
            configPath = deviceProtectedContext.getFilesDir().getAbsolutePath() + "/teleloc.conf";
            // Это даст путь: /data/user_de/0/org.qtproject.example.appTeleLoc/files/teleloc.conf

            bindServiceToWiFi();
            readConfig();
            createMyUser();

            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                m_wakeLock = pm.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "TeleLoc::WakeLock");
                m_wakeLock.acquire();
            }

            android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm != null) {
                m_multicastLock = wm.createMulticastLock("TeleLoc:MulticastLock");
                m_multicastLock.acquire();
                m_wifiLock = wm.createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "TeleLoc:WifiLock");
                m_wifiLock.acquire();
            }
        } catch (Exception e) { Log.e(TAG, "Ошибка инициализации ресурсов: " + e.getMessage()); }

    createNotificationChannel();
    Notification notification = createVoipNotification("Рация TeleLoc работает в дежурном режиме", null, false);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(NOTIFICATION_ID, notification, android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_PHONE_CALL | android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
    } else {
        startForeground(NOTIFICATION_ID, notification);
    }

        startSendDiscovery();
        startUdpReceiver();
        serverThread = new Thread(this::listenForCalls);
        serverThread.start();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createNotificationChannel();
        Notification notification = createVoipNotification("Рация TeleLoc работает в дежурном режиме", null, false);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_PHONE_CALL | android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        return START_STICKY;
    }

    public void executeCommandFromCpp(int commandId, String param) {
        Log.d(TAG, "@@@ [Java Service] Выполнение команды: id=" + commandId + " Парам=" + param);
        switch (commandId) {
            case 0:
                users.get(0).setName(param);
                excludeUserDups(0);
                break;
            case 1:
                lastCall = null;
                break;
            case 2:
            Log.w(TAG , "@@@ Java: Cpp TERMINATING---------------------------------------------------");
                //triggerFullScreenCall(param);
                break;
            default:
                Log.w(TAG, "@@@ [Java Service] Неизвестный ID команды: " + commandId);
                break;
        }
    }
    private void listenForCalls() {
        try {
            android.net.ConnectivityManager cm = (android.net.ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            android.net.Network wifiDirectNetwork = null;
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
                for (android.net.Network net : cm.getAllNetworks()) {
                    android.net.NetworkCapabilities caps = cm.getNetworkCapabilities(net);
                    if (caps != null && caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI)) {
                        wifiDirectNetwork = net;
                        break;
                    }
                }
            }

            if (wifiDirectNetwork != null && android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
                serverSocket = new ServerSocket();
                serverSocket.bind(new java.net.InetSocketAddress("0.0.0.0", TCP_PORT));
            } else {
                serverSocket = new ServerSocket(TCP_PORT, 50, java.net.InetAddress.getByName("0.0.0.0"));
            }

            while (m_isRunning) {
                Socket clientSocket = serverSocket.accept();
                clientSocket.setTcpNoDelay(true);
                InputStream input = clientSocket.getInputStream();
                java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
                byte[] buffer = new byte[4096];
                int bytesRead;

                while ((bytesRead = input.read(buffer)) != -1) {
                    baos.write(buffer, 0, bytesRead);
                    if (input.available() == 0) break;
                }

                byte[] finalData = baos.toByteArray();
                if (finalData.length > 0) {
                    String message = new String(finalData, 0, finalData.length, "UTF-8");
                    try {
                        org.json.JSONObject obj = new org.json.JSONObject(message);
                        if ("incoming_call".equals(obj.optString("type"))) {
                            Log.w(TAG, "@@@listenforcalls incoming_call" + message + isCppContextAlive());
                            saveCall(obj.optString("name"), obj.optString("ip"), obj.optInt("netType"));
                            triggerFullScreenCall(message);
                            sendToCpp(1,message);
                        }
                    } catch (org.json.JSONException jsonEx) {
                        Log.e(TAG, "Ошибка парсинга TCP JSON: " + jsonEx.getMessage());
                    }
                }
                clientSocket.close();
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка ServerSocket: " + e.getMessage());
        }
    }

    private void startSendDiscovery() {
        new Thread(() -> {
            while (true) {
                try {
                    cnt++;
                    org.json.JSONObject jDiscovery = new org.json.JSONObject();
                    jDiscovery.put("type", "discovery");
                    jDiscovery.put("name", users.get(0).name);
                    jDiscovery.put("fromservice", true);

                    org.json.JSONArray ipArr = new org.json.JSONArray();
                    ipArr.put(getLocalIpAddress(0));
                    ipArr.put(getLocalIpAddress(1));
                    ipArr.put(getLocalIpAddress(2));
                    jDiscovery.put("ip", ipArr);

                    String json = jDiscovery.toString();
                    Log.d(TAG, "@@@#sendDiscovery " +json);
                    byte[] bytes = json.getBytes("UTF-8");
                    java.net.DatagramSocket socket = new java.net.DatagramSocket();
                    socket.setBroadcast(true);

                    List<String> ips = new ArrayList<>();
                    UserInfo u = users.get(0);
                    if (u.ip[0] !="")
                        ips.add("255.255.255.255");
                    if (u.ip[1] !="") {
                        ips.add("192.168.43.255");
                        ips.add("192.168.137.255");
                        }
                    if (u.ip[2] !="") {
                        if (u.ip[2] != "192.168.49.1")
                            ips.add("192.168.49.1");
                        if (u.ip[2] != "192.168.49.128")
                            ips.add("192.168.49.128");
                        if (u.ip[2] != "192.168.49.129")
                            ips.add("192.168.49.129");
                        if (u.ip[2] != "192.168.49.130")
                            ips.add("192.168.49.130");
                    }
                    for (String ip : ips) {
                        java.net.DatagramPacket packet = new java.net.DatagramPacket(bytes, bytes.length, java.net.InetAddress.getByName(ip), 28001);
                        socket.send(packet);
                    }
                    socket.close();
                    Thread.sleep(3000);

                    if (m_isRunning) {
                        sendToCpp(0, usersToString());
                    }
                } catch (Exception e) {
                    Log.w(TAG, "Ошибка Discovery пакета.");
                }
            }
        }).start();

        new Thread(() -> {
            try {
                m_serverSocket = new ServerSocket(28500);
                while (m_isRunning) {
                    Socket clientSocket = m_serverSocket.accept();
                    BufferedReader in = new BufferedReader(new InputStreamReader(clientSocket.getInputStream(), "UTF-8"));
                    String line = in.readLine();
                    if (line != null) {
                        try {
                            org.json.JSONObject obj = new org.json.JSONObject(line);
                            if ("incoming_call".equals(obj.optString("type"))) {
                                triggerFullScreenCall(line);
                            }
                        } catch (Exception e) {
                            Log.e(TAG, "Ошибка JSON на 28500");
                        }
                    }
                    clientSocket.close();
                }
            } catch (Exception e) {
                Log.e(TAG, "Ошибка TCP сервера 28500: " + e.getMessage());
            }
        }).start();
    }
private void startUdpReceiver() {
    new Thread(() -> {
        try {
            java.net.DatagramSocket socket = new java.net.DatagramSocket(28001);
            socket.setReuseAddress(true);
            byte[] buffer = new byte[4096];
            while (m_isRunning) {
                java.net.DatagramPacket packet = new java.net.DatagramPacket(buffer, buffer.length);
                socket.receive(packet);
                String message = new String(packet.getData(), 0, packet.getLength(), "UTF-8").trim();
                try {
                    org.json.JSONObject obj = new org.json.JSONObject(message);
                    String stype = obj.optString("type");
                    String pName = obj.optString("name");
                    int netType  = obj.optInt("netType");
                    String pIp = obj.optString("ip");
                    boolean android = obj.optInt("android") == 1;
                    Log.d(TAG, "@@@UDPReceive " + message);
                    if ("discovery".equals(stype)) {
                        if (pName != null && !pName.isEmpty() && pIp != null && !pIp.isEmpty()) {
                            updatePeer(pName, pIp, android);
                        }
                    } else if ("incoming_call1".equals(stype)) {
                        saveCall(pName, pIp, netType);
                        triggerFullScreenCall(message);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Ошибка UDP JSON: " + e.getMessage());
                }
            }
            socket.close();
        } catch (Exception e) {
            Log.e(TAG, "Ошибка UDP приемника: " + e.getMessage());
        }
    }).start();
}

public void triggerFullScreenCall(String callStr) {
    Log.d(TAG, "@@@ [Java Service] Вызов triggerFullScreenCall(): " + callStr);
    android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
    if (pm != null) {
        android.os.PowerManager.WakeLock wl = pm.newWakeLock(
            android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK | android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP | android.os.PowerManager.ON_AFTER_RELEASE,
            "TeleLoc:CallWakeLock"
        );
        wl.acquire(10000);
    }

    Intent callIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
    if (callIntent == null) return;

    callIntent.setAction("org.qtproject.example.appTeleLoc.WAKE_UP_ACTION");
    callIntent.putExtra("callerName", callStr);
    callIntent.putExtra("remoteIp", "0.0.0.0");
    callIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);

    try {
        startActivity(callIntent);
    } catch (Exception e) {
        Log.w(TAG, "Background startActivity заблокирован ОС Android");
    }

    final NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
    if (manager != null) {
        int pendingFlags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE : PendingIntent.FLAG_UPDATE_CURRENT;
        PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(this, 0, callIntent, pendingFlags);

        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ? new Notification.Builder(this, CHANNEL_ID_VOIP) : new Notification.Builder(this);
        builder.setSmallIcon(android.R.drawable.ic_menu_call)
            .setContentTitle("Входящий вызов")
            .setContentText("Вызывает: " + callStr)
            .setAutoCancel(true)
            .setOngoing(true);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            builder.setCategory(Notification.CATEGORY_CALL).setPriority(Notification.PRIORITY_MAX).setVisibility(Notification.VISIBILITY_PUBLIC);
        }
        builder.setFullScreenIntent(fullScreenPendingIntent, true);
        manager.notify(102, builder.build());

        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
            try { manager.cancel(102); } catch (Exception e) {}
        }, 15000);
    }
}

@Override
public void onTaskRemoved(Intent rootIntent) {
    Log.d(TAG, "@@@ [Java Service] shadow onTaskRemoved");
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
private void updatePeer(String name, String sip, boolean android) {
    try {
        if (users.isEmpty()) readConfig();
        String[] ip = new String[3];
        org.json.JSONArray ipArr = new org.json.JSONArray(sip);
        for (int i = 0; i < 3; i++) ip[i] = ipArr.getString(i);
        if (users.isEmpty()) return;

        if (name.equals(users.get(0).name)) {
            UserInfo user0 = users.get(0);
            for (int i = 0; i < 3; i++) user0.ip[i] = ip[i];
            user0.lastPing = System.currentTimeMillis();
            user0.android = true;
            saveUsers();
            return;
        }

        int ind = -1;;
        for (int i =0; i<users.size(); i++){
            UserInfo user = users.get(i);
            if (user.name.equals(name)) {
                ind = i;
                for (int j = 0; j < 3; j++) user.ip[j] = ip[j];
                user.lastPing = System.currentTimeMillis();
            }
        }
        if (ind == -1) {
            UserInfo u = new UserInfo();
            u.name = name;
            for (int i = 0; i < 3; i++) u.ip[i] = ip[i];
            u.lastPing = System.currentTimeMillis();
            users.add(u);
            ind = users.size() - 1;
        }
    excludeUserDups(ind);
        saveUsers();
    } catch (Exception e) {
        Log.e(TAG, "Ошибка updatePeer: " + e.getMessage());
    }
}

private void bindServiceToWiFi() {
    final ConnectivityManager connectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
    if (connectivityManager == null) return;
    NetworkRequest request = new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build();
    connectivityManager.requestNetwork(request, new ConnectivityManager.NetworkCallback() {
        @Override
        public void onAvailable(Network network) {
            super.onAvailable(network);
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) connectivityManager.bindProcessToNetwork(network);
                else ConnectivityManager.setProcessDefaultNetwork(network);
            } catch (Exception e) { Log.e(TAG, "Ошибка Wi-Fi привязки: " + e.getMessage()); }
        }
        @Override
        public void onLost(Network network) {
            super.onLost(network);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) connectivityManager.bindProcessToNetwork(null);
            else ConnectivityManager.setProcessDefaultNetwork(null);
        }
    });
}

private String getLocalIpAddress(int netType) {
    try {
        java.util.List<java.net.NetworkInterface> interfaces = java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces());
        for (java.net.NetworkInterface intf : interfaces) {
            Enumeration<java.net.InetAddress> addresses = intf.getInetAddresses();
            while (addresses.hasMoreElements()) {
                java.net.InetAddress addr = addresses.nextElement();
                String sAddr = addr.getHostAddress().replace("::ffff:", "");
                if (sAddr.startsWith("192.168.49.")) { if (netType == 2) return sAddr; }
                else if (sAddr.startsWith("192.168.43.") || sAddr.startsWith("192.168.137.")) { if (netType == 1) return sAddr; }
                else if (sAddr.startsWith("192.168.") && netType == 0) return sAddr;
            }
        }
    } catch (Exception e) { e.printStackTrace(); }
    return "";
}

private String usersToString() {
    try {
        org.json.JSONObject configObj = readJsonConfig();
        configObj.put("peers", usersToJson());
        return configObj.toString();
    } catch (Exception e) { return ""; }
}

private void saveUsers() {
    try {
        org.json.JSONObject configObj = readJsonConfig();
        configObj.put("peers", usersToJson());
        saveConfig(configObj);
    } catch (Exception e) {}
}

public void saveCall(String name, String ip, int netType) {
    try {
        lastCall = new org.json.JSONObject();
        lastCall.put("name", name);
        lastCall.put("ip", ip);
        lastCall.put("netType", netType);
        lastCall.put("time", System.currentTimeMillis());
        lastCallTime = System.currentTimeMillis();
        org.json.JSONObject obj = readJsonConfig();
        obj.put("lastCall", lastCall);
        saveConfig(obj);
    } catch (Exception e) {}
}

private org.json.JSONArray usersToJson() {
    try {
        org.json.JSONArray peers = new org.json.JSONArray();
        for (int i = 0; i < users.size(); i++) {
            org.json.JSONObject peer = new org.json.JSONObject();
            peer.put("name", users.get(i).name);
            peer.put("lastPing", users.get(i).lastPing);
            peer.put("android", users.get(i).android);
            org.json.JSONArray ipArr = new org.json.JSONArray();
            for (int j = 0; j < 3; j++) ipArr.put(users.get(i).ip[j]);
            peer.put("ip", ipArr);
            peers.put(peer);
        }
        return peers;
    } catch (Exception e) { return new org.json.JSONArray(); }
}

private void createMyUser() {
    if (users.size() != 0) return;
    UserInfo u = new UserInfo();
    u.name = "Malamu";
    u.android = true;
    for (int i = 0; i < 3; i++) u.ip[i] = getLocalIpAddress(i);
    users.add(u);
}
private boolean isCppContextAlive() {
    try {
        // Делаем легкий холостой вызов JNI (eventId = 0).
        // Если Qt и C++ живы в памяти — проверка мгновенно пройдет без ошибок!
        sendDataToCpp(TeleLocService.this, 4, "*" );
        return true;
    } catch (UnsatisfiedLinkError | Exception e) {
        // Если приложение смахнуто — мост разрушен.
        // Java бесшумно перехватит ошибку и выдаст честный false!
        return false;
    }
}
void sendToCpp(int num, String message) {

    if (!isCppContextAlive()) {
        //m_isRunning = false;
        return;
        }
    sendDataToCpp(TeleLocService.this, num, message);
}

@Override
public IBinder onBind(Intent intent) { return null; }
}
