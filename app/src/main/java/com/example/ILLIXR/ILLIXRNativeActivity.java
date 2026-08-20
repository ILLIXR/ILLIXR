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

public class ILLIXRNativeActivity extends NativeActivity {
    private static final int CAMERA_REQUEST_CODE = 100;
    private WifiManager.WifiLock wifiLock_;

    static {
        System.loadLibrary("native-activity");
    }
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        WifiManager wifiManager = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        wifiLock_ = wifiManager.createWifiLock(
                WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "illixr_wifi_lock");
        wifiLock_.acquire();
        Log.i("ILLIXRNativeActivity", "WiFi low-latency lock acquired");

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, CAMERA_REQUEST_CODE);
        } else {
            nativeOnPermissionGranted();
        }
    }

    @Override
    protected void onDestroy() {
        if (wifiLock_ != null && wifiLock_.isHeld()) {
            wifiLock_.release();
            Log.i("ILLIXRNativeActivity", "WiFi low-latency lock released");
        }
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantedResults) {
        if (requestCode == CAMERA_REQUEST_CODE && grantedResults.length > 0 && grantedResults[0] == PackageManager.PERMISSION_GRANTED) {
            Log.i("ILLIXRNativeActivity", "Camera permission granted");
            nativeOnPermissionGranted();
        } else {
            Log.e("ILLIXRNativeActivity", "Camera permission denied");
        }
    }

    public native void nativeOnPermissionGranted();
}