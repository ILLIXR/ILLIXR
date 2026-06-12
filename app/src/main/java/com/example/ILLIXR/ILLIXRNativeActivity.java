package com.example.ILLIXR;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.text.format.Formatter;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;

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

    /**
     * Presents a blocking profile selection dialog to the user before the ILLIXR
     * runtime is started.
     *
     * <p>Profile YAML files are bundled in the APK's {@code assets/profiles/}
     * directory and extracted to the app's internal storage by the C++ side before
     * this dialog is shown.  The C++ layer passes the extraction directory path in
     * via {@link #showDialog}; this class enumerates {@code *.yaml} files in that
     * directory, strips the {@code .yaml} suffix (keeping underscores), and
     * presents them in a Spinner.
     *
     * <p>If no profiles are found the dialog shows an error message and the
     * positive button is disabled, so the only option is to cancel (which causes
     * the app to exit).
     *
     * <p>The dialog is non-cancelable via the back button to prevent the app from
     * starting without a valid configuration.
     */
    public static class ProfilePickerDialog {

        private final Activity activity_;
        private final String   profiles_dir_;

        private String  chosen_path_ = "";
        private boolean confirmed_   = false;

        private ProfilePickerDialog(Activity activity, String profilesDir) {
            activity_     = activity;
            profiles_dir_ = profilesDir;
        }

        // -----------------------------------------------------------------------
        // Public API
        // -----------------------------------------------------------------------

        /**
         * Shows the dialog on the UI thread and blocks the calling thread until
         * the user either selects a profile and presses OK, or presses Cancel.
         *
         * @return {@code true} if the user confirmed a selection.
         */
        public boolean show() {
            final CountDownLatch latch = new CountDownLatch(1);
            activity_.runOnUiThread(() -> buildAndShow(latch));
            try {
                latch.await();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            return confirmed_;
        }

        /** Full filesystem path to the chosen profile file. */
        public String getChosenPath() { return chosen_path_; }

        // -----------------------------------------------------------------------
        // Dialog construction
        // -----------------------------------------------------------------------

        @SuppressLint("SetTextI18n")
        private void buildAndShow(CountDownLatch latch) {
            // Enumerate *.yaml files in the profiles directory.
            final List<File> yaml_files  = listYamlFiles(profiles_dir_);
            final List<String> file_labels = new ArrayList<>();
            for (File f : yaml_files) {
                file_labels.add(labelForFile(f));
            }

            LinearLayout root = new LinearLayout(activity_);
            root.setOrientation(LinearLayout.VERTICAL);
            int pad = dp(16);
            root.setPadding(pad, pad, pad, pad);

            TextView header = new TextView(activity_);
            header.setText("Select a configuration profile:");
            header.setTextSize(14);
            header.setTypeface(null, android.graphics.Typeface.BOLD);
            root.addView(header);
            addSpacing(root, 8);

            final Spinner spinner = new Spinner(activity_);
            final AlertDialog.Builder builder = new AlertDialog.Builder(activity_);

            if (yaml_files.isEmpty()) {
                // No profiles — show error and allow only Cancel.
                TextView error = new TextView(activity_);
                error.setText("No profile files were found. The application cannot start.");
                error.setTextSize(13);
                root.addView(error);

                ScrollView scroll = new ScrollView(activity_);
                scroll.addView(root);

                builder.setTitle("ILLIXR — Select Profile");
                builder.setView(scroll);
                builder.setCancelable(false);
                builder.setNegativeButton("Exit", (dialog, which) -> {
                    confirmed_ = false;
                    latch.countDown();
                });
                builder.show();
                return;
            }

            ArrayAdapter<String> adapter = new ArrayAdapter<>(
                activity_, android.R.layout.simple_spinner_item, file_labels);
            adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
            spinner.setAdapter(adapter);
            root.addView(spinner);

            ScrollView scroll = new ScrollView(activity_);
            scroll.addView(root);

            builder.setTitle("ILLIXR — Select Profile");
            builder.setView(scroll);
            builder.setCancelable(false);

            builder.setPositiveButton("OK", (dialog, which) -> {
                int pos = spinner.getSelectedItemPosition();
                chosen_path_ = yaml_files.get(pos).getAbsolutePath();
                confirmed_   = true;
                latch.countDown();
            });

            builder.setNegativeButton("Cancel", (dialog, which) -> {
                confirmed_ = false;
                latch.countDown();
            });

            builder.show();
        }

        // -----------------------------------------------------------------------
        // Helpers
        // -----------------------------------------------------------------------

        /**
         * Returns all {@code *.yaml} files in {@code dirPath}, sorted
         * alphabetically by filename.
         */
        private static List<File> listYamlFiles(String dirPath) {
            File dir = new File(dirPath);
            File[] files = dir.listFiles(
                (d, name) -> name.toLowerCase().endsWith(".yaml"));
            if (files == null) return Collections.emptyList();
            List<File> result = new ArrayList<>(Arrays.asList(files));
            result.sort((a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            return result;
        }

        /**
         * Produces the Spinner label for a profile file: strips the {@code .yaml}
         * suffix and preserves underscores exactly as-is.
         */
        private static String labelForFile(File f) {
            String name = f.getName();
            if (name.toLowerCase().endsWith(".yaml")) {
                name = name.substring(0, name.length() - 5);
            }
            return name;
        }

        private void addSpacing(LinearLayout parent, int dpValue) {
            android.view.View spacer = new android.view.View(activity_);
            spacer.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(dpValue)));
            parent.addView(spacer);
        }

        private int dp(int dp) {
            return Math.round(dp * activity_.getResources().getDisplayMetrics().density);
        }

        // -----------------------------------------------------------------------
        // JNI entry point
        // -----------------------------------------------------------------------

        /**
         * Static entry point called from JNI ({@code profile_picker_dialog.cpp}).
         *
         * <p>Creates and runs the dialog synchronously, then returns the absolute
         * filesystem path of the chosen profile, or an empty string if the user
         * cancelled.
         *
         * @param activity    Android activity context.
         * @param profilesDir Absolute path to the directory containing extracted
         *                    {@code *.yaml} profile files.
         */
        public static String showDialog(Activity activity, String profilesDir) {
            com.example.ILLIXR.ILLIXRNativeActivity.ProfilePickerDialog dlg = new com.example.ILLIXR.ILLIXRNativeActivity.ProfilePickerDialog(activity, profilesDir);
            if (!dlg.show()) return "";
            return dlg.getChosenPath();
        }
    }
