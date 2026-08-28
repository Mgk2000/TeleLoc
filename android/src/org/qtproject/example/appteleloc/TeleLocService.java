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
import java.io.InputStream;

import java.io.OutputStream;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
public class TeleLocService extends Service {
    private static final String TAG = "TeleLocService";
    private static final String CHANNEL_ID_SILENT = "TeleLocSilentChannel";
    private static final String CHANNEL_ID_VOIP = "TeleLocVoipChannel";
    private static final int NOTIFICATION_ID = 9999;
    private ServerSocket m_serverSocket;
    private boolean m_isRunning = false;
private android.net.wifi.WifiManager.MulticastLock m_multicastLock;
	    // Хранилище состояний
    private long lastAliveTime = 0;
    private String pendingDataMessage = null;
    private long pendingDataTime = 0;
    private String pendingIp="";
    private LocalSocket clientSocket = null;
    private boolean isConnected = false;
    private boolean isRunning = false;
        private int TCP_PORT = 28501;
	 private ServerSocket serverSocket;
	 private Thread serverThread;
 private void listenForCalls() {
        try {
            serverSocket = new ServerSocket(TCP_PORT);
            while (isRunning) {
                // Ждем подключения по TCP (процесс тут спит и не ест батарею)
                Socket clientSocket = serverSocket.accept();
                
                InputStream input = clientSocket.getInputStream();
                byte[] buffer = new byte[1024];
                int bytesRead = input.read(buffer);

                if (bytesRead > 0) {
					String message = new String(buffer,0 ,  bytesRead, "UTF-8");
					
                    Log.d(TAG, "@@@ Tcp received " + message);
                        org.json.JSONObject obj = new org.json.JSONObject(message);
						String stype = obj.optString("type");
                        String pName = obj.optString("name");
						int netType  = obj.optInt("netType");
                        String pIp = obj.optString("ip");
						//Log.d(TAG, "@@@ JAVA СЛУЖБА: stype=" + stype);
                    //Log.d(TAG, "@@@ JAVA СЛУЖБА: Получен UDP пакет: " + message + " stype= " + stype);
						if ("incoming_call".equals(stype))
							{
								String sNetType = obj.optString("name");
								saveCall(pName, pIp, netType);
								triggerFullScreenCall(message );
							}
						else
							{
							Log.d(TAG, "@@@ Tcp type:" + stype);
							}	
						
					                    // Записываем данные в ваш файл во внутреннюю память приложения
                   /* File callFile = new File(getFilesDir(), "incoming_call.txt");
                    try (FileOutputStream fos = new FileOutputStream(callFile)) {
                        fos.write(data.getBytes(StandardCharsets.UTF_8));
                    }

                    // БУДИМ QT-ПРОЦЕСС: Запускаем главное Activity вашего Qt-приложения
                    // Имя класса обычно совпадает с тем, что сгенерировал Qt Creator
                    Intent qtIntent = new Intent(this, Class.forName("org.qtproject.qt.android.QtActivity"));
                    qtIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(qtIntent); */
                }
                clientSocket.close();
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // --- 1. МЕТОД: Служба получила звонок по UDP ---
    private void onUdpCallReceived(String callStr) {
        long currentTime = System.currentTimeMillis();
        String dataMessage = callStr;
        Log.d (TAG, "@@@ onUdpCallReceived" + callStr);
        // Проверяем, был ли процесс активен в последние 10 секунд
        if (isConnected && (currentTime - lastAliveTime <= 10000)) {
            Log.d(TAG, "@@@ Процесс активен (живой пинг есть). Отправляем звонок сразу.");
            sendDirectly(dataMessage);
        } else {
            Log.d(TAG, "@@@ Процесс спит или не отвечает. Будим его и сохраняем звонок в буфер.");
            
            // Сохраняем сообщение и время его прихода
            this.pendingDataMessage = dataMessage;
            this.pendingDataTime = currentTime;

            // Будим процесс (ваш рабочий код запуска Activity)
            //wakeUpQtProcess(); 
            
            // Запускаем поток подключения (он будет пытаться соединиться, пока Qt просыпается)
            startConnectionLoop();
        }
    }

    // --- 2. МЕТОД: Поток фонового соединения и чтения alive-сообщений ---
    private void startConnectionLoop() {
        Log.d(TAG, "@@@ startConnectionLoop() 1" );
        if (isConnected) return;
        Log.d(TAG, "@@@ startConnectionLoop() 2" );

        new Thread(new Runnable() {
            @Override
            public void run() {
                Log.d(TAG, "@@@ startConnectionLoop() 3" );
                String socketPath = getCacheDir().getAbsolutePath() + "/TeleLocSocketKey";
                LocalSocketAddress address = new LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM);
                
                int attempts = 0;
                // Пытаемся подключиться в течение 10 секунд (20 попыток по 500мс)
                while (!isConnected && attempts < 20) {
                    try {
                        Log.d(TAG, "@@@ startConnectionLoop() 4 atempts" +  attempts);

                        clientSocket = new LocalSocket();
                        clientSocket.connect(address);
                         Log.d(TAG, "@@@ startConnectionLoop() 5 isConnecte = true" );
                        // Если строка выше не выбросила Exception — мы успешно подключились!
                        isConnected = true; 
                    } catch (Exception e) {
                        attempts++;
                        Log.d(TAG, "@@@ C++ процесс еще просыпается, ждем... (Попытка " + attempts + ")");
                        try { Thread.sleep(500); } catch (Exception ignored) {}
                    }
                }

                if (!isConnected) {
                    Log.e(TAG, "@@@ [Java ОШИБКА] Не удалось подключиться к C++ за 10 секунд. Звонок забыт.");
                    pendingDataMessage = null;
                    return;
                }

                try {
                    Log.d(TAG, "@@@ [Java УСПЕХ] Соединение с сокетом C++ установлено удерживается!");
                    InputStream input = clientSocket.getInputStream();
                    byte[] buffer = new byte[1024];
                    int bytesRead;

                    // Бесконечный цикл удержания сокета и чтения пингов от C++
                    while ( isConnected && (bytesRead = input.read(buffer)) != -1) {
                    //while (true) {
					//	bytesRead = input.read(buffer);
                        String message = new String(buffer, 0, bytesRead, "UTF-8");
                        
					if (message.equals("ALIVE") || message.equals("FIRSTALIVE") ) {
                            long currentTime = System.currentTimeMillis();
                            lastAliveTime = currentTime;
                            Log.d(TAG, "@@@ [Java] Получен пинг " + message + " от C++.");

                            // Если в буфере лежит отложенный звонок
                            if (pendingDataMessage != null) {
                                // Проверяем, уложился ли C++ в 10 секунд с момента звонка
                                if (currentTime - pendingDataTime >=5000 && currentTime - pendingDataTime <= 20000) {
                                    Log.d(TAG, "@@@ [Java] Условие выполнено! Отправляем звонок из буфера.");
                                    Log.d(TAG, "@@@ " + pendingDataMessage);
                                    //sendDirectly(pendingDataMessage);
									sendToQtViaUnixSocket(pendingDataMessage);
 
								}
								else {
                                    Log.w(TAG, "@@@ [Java] Процесс просыпался слишком долго (>10 сек). Удаляем звонок.");
                                }
                                pendingDataMessage = null; // Очищаем буфер
                            }
							Thread.sleep(5000);
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "@@@ Соединение разорвано: " + e.getMessage());
                    isConnected = false;
                }
            }
        }).start();
    }

    private void sendDirectly(String msg) {
        try {
            if (clientSocket != null && isConnected) {
                OutputStream out = clientSocket.getOutputStream();
                out.write(msg.getBytes("UTF-8"));
                out.flush();
                Log.d(TAG, "@@@ Данные физически ушли в Unix-сокет: " + msg);
            }
        } catch (Exception e) {
            Log.e(TAG, "Ошибка отправки: " + e.getMessage());
        }
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // Функция отправки данных по локальной сети в Qt
public  void sendToQtViaUnixSocket(final String message) {
    new Thread(new Runnable() {
        @Override
        public void run() {
            try {
                Log.d(TAG, "@@@ [Java] Попытка подключения через файловый Unix-сокет...");
                
                LocalSocket socket = new LocalSocket();
                
                // Формируем путь к файлу сокета в папке кэша
                String socketPath = getCacheDir().getAbsolutePath() + "/TeleLocSocketKey";
                Log.d(TAG, "@@@ [Java] Путь поиска сокета: " + socketPath);
                
                LocalSocketAddress address = new LocalSocketAddress(
                    socketPath, 
                    LocalSocketAddress.Namespace.FILESYSTEM
                );
                
                // Пытаемся подключиться в цикле, пока C++ полностью не создаст файл
                int retryCount = 0;
                while (!socket.isConnected() && retryCount < 20) {
                    try {
                        socket.connect(address);
                    } catch (Exception e) {
                        retryCount++;
                        Thread.sleep(500); // Ждем 0.5 сек перед повторной попыткой
                    }
                }

                if (socket.isConnected()) {
                    Log.d(TAG, "@@@ [Java УСПЕХ] Соединение с C++ установлено!");
                    
                    // Запускаем бесконечный цикл чтения пингов ALIVE от С++
                    InputStream input = socket.getInputStream();
                    byte[] buffer = new byte[1024];
                    int bytesRead;

                    // Тут выполняется ваш алгоритм проверки 10 секунд (из предыдущего ответа)
                    // ...
                    
                } else {
                    Log.e(TAG, "@@@ [Java ОШИБКА] Не удалось подключиться к файлу сокета за 10 секунд.");
                }
                
            } catch (Exception e) {
                Log.e(TAG, "@@@ [Java КРИТ ОШИБКА] " + e.getMessage());
            }
        }
    }).start();
}
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
        startConnectionLoop();
			Log.d(TAG, "@@@ startConnectionLoop.");
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
            startSendDiscovery();
        }
    }).start();

    startUdpReceiver();
	        isRunning = true;
        serverThread = new Thread(this::listenForCalls);
        serverThread.start();
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

 public int cnt=0;
 private void startSendDiscovery() {
    new Thread(new Runnable() {
        @Override
        public void run() {
                cnt++;
                Log.d(TAG, "@@@ сnt=" + cnt);
               while (m_isRunning) {
                try {
                    Thread.sleep(3000);
                    //Log.d(TAG, "@@@  SendDiscovery 0    ");
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
                    //Log.d(TAG, "@@@  SendDiscovery 1");
                    String myIp = getLocalIpAddress();
//                    String json = "{\"type\":\"discovery1\",\"name\":\"" + myName + "\",\"ip0\":\"" + myIp + "\",\"ip1\":\"\",\"ip2\":\"\"}";
					org.json.JSONObject jDiscovery = new org.json.JSONObject();
					jDiscovery.put("type", "discovery");
					jDiscovery.put("name", myName);
					jDiscovery.put("fromservice", true);
					org.json.JSONArray ipArr = new org.json.JSONArray();
					ipArr.put(myIp);
					ipArr.put("6.6.6.6");
					ipArr.put("");
					jDiscovery.put("ip",ipArr);
                    String json = jDiscovery.toString();
                    //Log.d(TAG, "@@@  SendDiscovery 2");

                    byte[] bytes = json.getBytes("UTF-8");
                    java.net.DatagramSocket socket = new java.net.DatagramSocket();

                    socket.setBroadcast(true);

                    //Log.d(TAG, "@@@  SendDiscovery 3");

                    String[] ips = {"255.255.255.255", "192.168.43.255", "192.168.137.255"};
                    for (String ip : ips) {
                        java.net.InetAddress addr = java.net.InetAddress.getByName(ip);
                        java.net.DatagramPacket packet = new java.net.DatagramPacket(bytes, bytes.length, addr, 28000);
                        socket.send(packet);
                    }
                    //Log.d(TAG, "@@@  SendDiscovery 4");
                    if (cnt < 2000000000)
                    continue;
                    if (file.exists()) {
                        java.io.FileInputStream fis = new java.io.FileInputStream(file);
                        byte[] data = new byte[(int) file.length()];
                        fis.read(data);
                        fis.close();
                        //Log.d(TAG, "@@@  SendDiscovery 5");

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
                    //L//og.d(TAG, "@@@  SendDiscovery 6");

                    socket.close();
                    //Log.d(TAG, "@@@ JAVA СЛУЖБА: Адресный пакет Discovery отправлен пирам.");
                    Thread.sleep(30000);
                } catch (Exception e) {
                    Log.d(TAG, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ сnt=" + cnt);
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
                        //triggerFullScreenCall(obj.optString("name", "Некто"), remoteIp);
                        triggerFullScreenCall(line);

                    }
                } catch (Exception e) {}
            }
            clientSocket.close();
        }
    } catch (Exception e) {}
}




    public void triggerFullScreenCall(String callStr) {
        Log.d(TAG, "@@@ JAVA СЛУЖБА: Вызов triggerFullScreenCall() для: " + callStr);
        sendToQtViaUnixSocket(callStr);
        onUdpCallReceived(callStr);
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

        callIntent.setAction("org.qtproject.example.appTeleLoc.WAKE_UP_ACTION");
        callIntent.putExtra("callerName", callStr);
        callIntent.putExtra("remoteIp", "0.0.0.0");
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
            
            Notification notification = createVoipNotification("Входящий вызов от " + callStr, fullScreenPendingIntent, true);
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
public void saveCall(String name, String ip, int netType) {
 try{
	Log.d(TAG, "@@@saveCall (" + name + ip +netType);
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
		org.json.JSONObject jcall = new org.json.JSONObject();
		jcall.put("name", name);
		jcall.put("ip", ip);
		jcall.put("netType", netType);
		jcall.put("time",System.currentTimeMillis());
		Log.d(TAG, "@@@ saveCall" + jcall);
		configObj.put("lastCall", jcall);
		String s = configObj.toString(); 
		Log.d(TAG, "@@@ s=" +s);
        java.io.FileOutputStream fos = new java.io.FileOutputStream(file);
        fos.write(configObj.toString().getBytes("UTF-8"));
        fos.close();
 }
 catch (Exception e) {
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
                    //Log.d(TAG, "@@@ JAVA СЛУЖБА: Получен UDP пакет: " + message);

                    try {
                        org.json.JSONObject obj = new org.json.JSONObject(message);
						String stype = obj.optString("type");
                        String pName = obj.optString("name");
						int netType  = obj.optInt("netType");
                        String pIp = obj.optString("ip");
						//Log.d(TAG, "@@@ JAVA СЛУЖБА: stype=" + stype);
                        if ("discovery".equals(stype)) 
                        {
                            
                            if (pName != null && !pName.isEmpty() && pIp != null && !pIp.isEmpty()) {
                                //Log.d(TAG, "@@@ JAVA СЛУЖБА: Обновляю пира в конфиге: " + pName + " -> " + pIp);
                                updatePeerInConfig(pName, pIp);
                            }
						}
						else
						{
                    Log.d(TAG, "@@@ JAVA СЛУЖБА: Получен UDP пакет: " + message + " stype= " + stype);
							if ("incoming_call1".equals(stype))
							{
								String sNetType = obj.optString("name");
								saveCall(pName, pIp, netType);
								triggerFullScreenCall(message );
							}
							else
							{
							sendToQtViaUnixSocket(message);
							onUdpCallReceived(message);
							}	
						}
					}
                    catch (Exception e) {
                        Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА парсинга JSON: " + e.getMessage());
                    }
                }
                socket.close();
            } 
			catch (Exception e) {
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
Log.d(TAG, "@@@ name=" + name + " IP=" + ip + " found=" + found);
        if (!found) {
            org.json.JSONObject newPeer = new org.json.JSONObject();
            newPeer.put("name", name);
            org.json.JSONArray ips = new  org.json.JSONArray();
            ips.put(ip);
            ips.put("");
            ips.put("");
            newPeer.put("ip", ips);
            newPeer.put("isAlive", true); // ИСПРАВЛЕНО: выставляем флаг при создании
            peers.put(newPeer);
        }

        java.io.FileOutputStream fos = new java.io.FileOutputStream(file);
        fos.write(configObj.toString().getBytes("UTF-8"));
        fos.close();
        //Log.d(TAG, "@@@ JAVA СЛУЖБА: Конфиг успешно перезаписан. Пир " + name + " добавлен/обновлен.");
    } catch (Exception e) {
        Log.e(TAG, "@@@ JAVA СЛУЖБА ОШИБКА внутри updatePeerInConfig: " + e.getMessage());
        e.printStackTrace();
    }
}

}
