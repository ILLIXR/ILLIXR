package com.example.ILLIXR;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class ILLIXRNativeActivity extends NativeActivity {
    private static final int CAMERA_REQUEST_CODE = 100;
    static {
        System.loadLibrary("native-activity");
    }
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, CAMERA_REQUEST_CODE);
        } else {
            nativeOnPermissionGranted();
        }
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