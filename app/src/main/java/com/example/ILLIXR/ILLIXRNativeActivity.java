package com.example.ILLIXR;

import android.Manifest;
import android.app.NativeActivity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Android entry point for the native ILLIXR Quest client.
 *
 * Besides NativeActivity lifecycle forwarding, this class keeps Wi-Fi in
 * low-latency mode and performs the small LAN bootstrap used by desktop
 * `--quest-ip`. The desktop address comes from the UDP packet source, so the
 * user never has to type the computer's address inside the headset.
 */
public class ILLIXRNativeActivity extends NativeActivity {
    private static final int CAMERA_REQUEST_CODE = 100;
    private static final String SERVER_IP_EXTRA = "illixr_server_ip";
    private static final int CONFIG_PORT = 9010;
    private static final String CONNECT_REQUEST = "ILLIXR_CONNECT_V1";
    private static final String CONNECT_RESPONSE = "ILLIXR_READY_V1";
    private static final String TAG = "ILLIXRNativeActivity";

    /** First accepted desktop wins for the lifetime of one Activity session. */
    private WifiManager.WifiLock wifiLock_;
    private final AtomicReference<String> configuredServerIp_ = new AtomicReference<>(null);
    private volatile boolean configReceiverRunning_ = false;
    private DatagramSocket configSocket_;
    private Thread configThread_;

    static {
        System.loadLibrary("native-activity");
    }

    /** Initialize optional intent configuration, networking, and permissions. */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String serverIp = getIntent().getStringExtra(SERVER_IP_EXTRA);
        if (serverIp != null && !serverIp.trim().isEmpty()) {
            configureNative(serverIp.trim(), "Android intent");
        }
        super.onCreate(savedInstanceState);

        WifiManager wifiManager = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        wifiLock_ = wifiManager.createWifiLock(
                WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "illixr_wifi_lock");
        wifiLock_.acquire();
        Log.i(TAG, "WiFi low-latency lock acquired");

        startConfigurationReceiver();

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, CAMERA_REQUEST_CODE);
        } else {
            nativeOnPermissionGranted();
        }
    }

    /** Stop the UDP listener before releasing Android-owned runtime resources. */
    @Override
    protected void onDestroy() {
        stopConfigurationReceiver();
        if (wifiLock_ != null && wifiLock_.isHeld()) {
            wifiLock_.release();
            Log.i(TAG, "WiFi low-latency lock released");
        }
        super.onDestroy();
    }

    /** Forward a newly granted camera permission to the native runtime. */
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantedResults) {
        if (requestCode == CAMERA_REQUEST_CODE && grantedResults.length > 0 && grantedResults[0] == PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "Camera permission granted");
            nativeOnPermissionGranted();
        } else {
            Log.e(TAG, "Camera permission denied");
        }
    }

    /**
     * Commit a desktop address exactly once and wake the native ILLIXR thread.
     * Duplicate packets from the selected desktop are accepted so a lost UDP
     * acknowledgement can be retried safely.
     */
    private boolean configureNative(String serverIp, String source) {
        if (serverIp == null || serverIp.isEmpty()) {
            return false;
        }
        if (configuredServerIp_.compareAndSet(null, serverIp)) {
            Log.i(TAG, "Configuring desktop ILLIXR server " + serverIp + " from " + source);
            nativeConfigure(serverIp);
            return true;
        }
        return serverIp.equals(configuredServerIp_.get());
    }

    /**
     * Listen for the desktop bootstrap packet on a background Java thread.
     * The socket is intentionally separate from ILLIXR's media/control ports;
     * those backends start only after nativeConfigure releases the runtime.
     */
    private void startConfigurationReceiver() {
        configReceiverRunning_ = true;
        configThread_ = new Thread(() -> {
            try {
                DatagramSocket socket = new DatagramSocket(null);
                socket.setReuseAddress(true);
                socket.bind(new InetSocketAddress(CONFIG_PORT));
                configSocket_ = socket;
                Log.i(TAG, "Waiting for desktop configuration on UDP port " + CONFIG_PORT);

                byte[] receiveBuffer = new byte[128];
                while (configReceiverRunning_) {
                    DatagramPacket request = new DatagramPacket(receiveBuffer, receiveBuffer.length);
                    socket.receive(request);

                    String message = new String(request.getData(), request.getOffset(), request.getLength(),
                            StandardCharsets.US_ASCII).trim();
                    if (!CONNECT_REQUEST.equals(message)) {
                        Log.w(TAG, "Ignoring unknown configuration packet from " + request.getAddress());
                        continue;
                    }

                    // The source selected by the host routing table is the
                    // correct address for the Quest to use on multi-homed PCs.
                    String serverIp = request.getAddress().getHostAddress();
                    if (!configureNative(serverIp, "wireless discovery")) {
                        Log.w(TAG, "Ignoring configuration from " + serverIp
                                + " because this session is already connected to " + configuredServerIp_.get());
                        continue;
                    }

                    byte[] response = CONNECT_RESPONSE.getBytes(StandardCharsets.US_ASCII);
                    DatagramPacket reply = new DatagramPacket(response, response.length,
                            request.getAddress(), request.getPort());
                    socket.send(reply);
                }
            } catch (SocketException e) {
                if (configReceiverRunning_) {
                    Log.e(TAG, "Could not listen for desktop configuration", e);
                }
            } catch (IOException e) {
                if (configReceiverRunning_) {
                    Log.e(TAG, "Wireless configuration failed", e);
                }
            } finally {
                configReceiverRunning_ = false;
                configSocket_ = null;
            }
        }, "illixr-config-receiver");
        configThread_.start();
    }

    /** Close the socket to unblock receive() and join the listener promptly. */
    private void stopConfigurationReceiver() {
        configReceiverRunning_ = false;
        if (configSocket_ != null) {
            configSocket_.close();
        }
        if (configThread_ != null) {
            try {
                configThread_.join(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            configThread_ = null;
        }
    }

    /** Notify native code that camera access became available after startup. */
    public native void nativeOnPermissionGranted();

    /** Supply the discovered desktop address to the waiting native runtime. */
    private native void nativeConfigure(String serverIp);
}
