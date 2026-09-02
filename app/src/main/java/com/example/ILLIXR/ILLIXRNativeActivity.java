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

    /**
     * Presents a blocking network configuration dialog to the user before ILLIXR plugins are loaded.
     *
     * <p>The dialog is adaptive: sections for TCP and UDP are shown only when the corresponding
     * backend plugin is present in the plugin list, as indicated by the {@code useTcp} and
     * {@code useUdp} flags passed to {@link #showDialog}.
     *
     * <p>When both backends are active a "Use same IP as TCP" checkbox (checked by default) locks
     * the UDP server IP field to match the TCP server IP, eliminating redundant entry in the common
     * case where both backends connect to the same host.
     *
     * <p>When both backends are active all four port numbers (TCP server, TCP client, UDP server,
     * UDP client) must be distinct; the dialog re-presents with a descriptive error message if any
     * two ports are equal.
     *
     * <p>Presets (previously confirmed configurations) are stored in {@link SharedPreferences} and
     * offered in a drop-down at the top of the dialog for quick recall.  Only fields relevant to the
     * current backend combination are populated from a preset.  An optional user-supplied name is
     * stored with each preset; if omitted the Spinner shows an auto-generated label derived from the
     * IP addresses and port numbers instead.  Presets can be deleted from the drop-down row via a
     * Delete button; a confirmation dialog names the preset before removing it.
     *
     * <p>The client (device) IP address is determined automatically from the active Wi-Fi interface
     * and shown read-only; it does not need to be entered by the user.
     */
    public static class NetworkConfigDialog {

        // -----------------------------------------------------------------------
        // SharedPreferences keys
        // -----------------------------------------------------------------------

        private static final String PREFS_NAME    = "illixr_network_config";
        private static final String PREFS_PRESETS = "presets";

        // Field separator used inside stored preset strings.
        // Must not appear in any valid IP address, port number, or preset name.
        private static final char FIELD_SEP = '|';

        // Number of fields in a fully-populated preset (both backends).
        // Layout: name|tcpServerIp|tcpServerPort|tcpClientPort|udpServerIp|udpServerPort|udpClientPort
        // The name field is first and may be empty; all other fields follow as before.
        private static final int PRESET_FIELD_COUNT = 7;

        // -----------------------------------------------------------------------
        // Result state
        // -----------------------------------------------------------------------

        private String  tcp_server_ip_   = "";
        private String  tcp_server_port_ = "9001";
        private String  tcp_client_port_ = "9000";
        private String  udp_server_ip_   = "";
        private String  udp_server_port_ = "9003";
        private String  udp_client_port_ = "9002";
        private String  client_ip_       = "";
        private boolean confirmed_       = false;

        // -----------------------------------------------------------------------
        // Construction
        // -----------------------------------------------------------------------

        private final Activity activity_;
        private final boolean  use_tcp_;
        private final boolean  use_udp_;

        private NetworkConfigDialog(Activity activity, boolean useTcp, boolean useUdp) {
            activity_ = activity;
            use_tcp_  = useTcp;
            use_udp_  = useUdp;
        }

        // -----------------------------------------------------------------------
        // Public API
        // -----------------------------------------------------------------------

        /**
         * Shows the dialog on the UI thread and blocks the calling thread until the user
         * either confirms or cancels.
         *
         * @return {@code true} if the user pressed Connect.
         */
        public boolean show() {
            client_ip_ = resolveClientIp();

            final CountDownLatch latch = new CountDownLatch(1);
            activity_.runOnUiThread(() -> buildAndShow(latch));
            try {
                latch.await();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            return confirmed_;
        }

        // Result getters — valid only after show() returns true
        public String getTcpServerIp()   { return tcp_server_ip_; }
        public String getTcpServerPort() { return tcp_server_port_; }
        public String getTcpClientPort() { return tcp_client_port_; }
        public String getUdpServerIp()   { return udp_server_ip_; }
        public String getUdpServerPort() { return udp_server_port_; }
        public String getUdpClientPort() { return udp_client_port_; }
        public String getClientIp()      { return client_ip_; }

        // -----------------------------------------------------------------------
        // Dialog construction
        // -----------------------------------------------------------------------

        @SuppressLint("SetTextI18n")
        private void buildAndShow(CountDownLatch latch) {
            LinearLayout root = new LinearLayout(activity_);
            root.setOrientation(LinearLayout.VERTICAL);
            int pad = dp(16);
            root.setPadding(pad, pad, pad, pad);

            // --- Preset row: spinner + delete button side by side ---
            addSectionHeader(root, "Saved presets");

            // Mutable list so the delete handler can remove an entry and refresh the adapter.
            final List<String> presetKeys   = loadPresetKeys();
            final List<String> presetLabels = new ArrayList<>();
            presetLabels.add("— select a preset —");
            for (String key : presetKeys) {
                presetLabels.add(presetDisplayLabel(key));
            }

            final ArrayAdapter<String> presetAdapter = new ArrayAdapter<>(
                activity_, android.R.layout.simple_spinner_item, presetLabels);
            presetAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

            final Spinner presetSpinner = new Spinner(activity_);
            presetSpinner.setAdapter(presetAdapter);

            // Delete button — sits to the right of the spinner in a horizontal row.
            final Button btnDelete = new Button(activity_);
            btnDelete.setText("Delete");
            btnDelete.setEnabled(false); // enabled only when a real preset is selected

            LinearLayout spinnerRow = new LinearLayout(activity_);
            spinnerRow.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams spinnerParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            LinearLayout.LayoutParams deleteParams  = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            presetSpinner.setLayoutParams(spinnerParams);
            btnDelete.setLayoutParams(deleteParams);
            spinnerRow.addView(presetSpinner);
            spinnerRow.addView(btnDelete);
            root.addView(spinnerRow);
            addSpacing(root, 12);

            // --- Client IP (read-only) ---
            addSectionHeader(root, "Client IP (this device)");
            TextView clientIpView = new TextView(activity_);
            clientIpView.setText(client_ip_.isEmpty() ? "(unavailable — check Wi-Fi)" : client_ip_);
            clientIpView.setTextSize(14);
            root.addView(clientIpView);
            addSpacing(root, 12);

            // --- TCP section ---
            EditText etTcpServerIp   = null;
            EditText etTcpServerPort = null;
            EditText etTcpClientPort = null;
            if (use_tcp_) {
                addSectionHeader(root, "TCP");
                etTcpServerIp   = addLabeledField(root, "Server IP address", tcp_server_ip_);
                etTcpServerPort = addLabeledField(root, "Server port",       tcp_server_port_);
                etTcpClientPort = addLabeledField(root, "Client port",       tcp_client_port_);
                addSpacing(root, 8);
            }

            // --- UDP section ---
            EditText etUdpServerIp   = null;
            EditText etUdpServerPort = null;
            EditText etUdpClientPort = null;
            CheckBox cbSameIp        = null;
            if (use_udp_) {
                addSectionHeader(root, "UDP");

                // "Same IP as TCP" checkbox — only meaningful when both backends are active.
                if (use_tcp_) {
                    cbSameIp = new CheckBox(activity_);
                    cbSameIp.setText("Use same server IP as TCP");
                    cbSameIp.setChecked(true);
                    root.addView(cbSameIp);
                    addSpacing(root, 4);
                }

                etUdpServerIp   = addLabeledField(root, "Server IP address", udp_server_ip_);
                etUdpServerPort = addLabeledField(root, "Server port",       udp_server_port_);
                etUdpClientPort = addLabeledField(root, "Client port",       udp_client_port_);
                addSpacing(root, 8);

                // When both backends are active and the checkbox is checked:
                //  - The UDP IP field starts disabled and mirroring the TCP IP field.
                //  - Unchecking enables independent entry.
                if (use_tcp_ && cbSameIp != null) {
                    final EditText finalEtTcpServerIp = etTcpServerIp;
                    final EditText finalEtUdpServerIp = etUdpServerIp;
                    final CheckBox finalCbSameIp      = cbSameIp;

                    // Initial state: linked
                    finalEtUdpServerIp.setEnabled(false);
                    finalEtUdpServerIp.setText(finalEtTcpServerIp.getText().toString());

                    // Mirror TCP -> UDP while checkbox is checked
                    finalEtTcpServerIp.addTextChangedListener(new TextWatcher() {
                        @Override
                        public void beforeTextChanged(CharSequence s, int start, int count, int after) { }

                        @Override
                        public void onTextChanged(CharSequence s, int start, int before, int count) {
                            if (finalCbSameIp.isChecked()) {
                                finalEtUdpServerIp.setText(s.toString());
                            }
                        }

                        @Override
                        public void afterTextChanged(Editable s) { }
                    });

                    // Toggle UDP IP field editability
                    finalCbSameIp.setOnCheckedChangeListener((btn, isChecked) -> {
                        if (isChecked) {
                            finalEtUdpServerIp.setEnabled(false);
                            finalEtUdpServerIp.setText(finalEtTcpServerIp.getText().toString());
                        } else {
                            finalEtUdpServerIp.setEnabled(true);
                        }
                    });
                }
            }

            // --- Profile name (optional) ---
            addSectionHeader(root, "Profile name (optional)");
            EditText etProfileName = addLabeledField(root, "e.g. Lab Server", "");
            addSpacing(root, 8);

            // Keep final references for use in lambdas below.
            final EditText finalEtTcpServerIp   = etTcpServerIp;
            final EditText finalEtTcpServerPort = etTcpServerPort;
            final EditText finalEtTcpClientPort = etTcpClientPort;
            final EditText finalEtUdpServerIp   = etUdpServerIp;
            final EditText finalEtUdpServerPort = etUdpServerPort;
            final EditText finalEtUdpClientPort = etUdpClientPort;
            final EditText finalEtProfileName   = etProfileName;

            // --- Preset selection: fills fields and enables the Delete button ---
            presetSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
                @Override
                public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                    // Position 0 is the placeholder; positions 1..N map to presetKeys[0..N-1].
                    btnDelete.setEnabled(position > 0);
                    if (position == 0) return;

                    com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig cfg = parsePreset(presetKeys.get(position - 1));
                    if (cfg == null) return;

                    // Populate name field (may be empty for older presets — that is fine)
                    finalEtProfileName.setText(cfg.name);

                    if (use_tcp_ && finalEtTcpServerIp != null) {
                        if (!cfg.tcp_server_ip.isEmpty())
                            finalEtTcpServerIp.setText(cfg.tcp_server_ip);
                        if (!cfg.tcp_server_port.isEmpty())
                            finalEtTcpServerPort.setText(cfg.tcp_server_port);
                        if (!cfg.tcp_client_port.isEmpty())
                            finalEtTcpClientPort.setText(cfg.tcp_client_port);
                    }
                    if (use_udp_ && finalEtUdpServerIp != null) {
                        if (!cfg.udp_server_ip.isEmpty())
                            finalEtUdpServerIp.setText(cfg.udp_server_ip);
                        if (!cfg.udp_server_port.isEmpty())
                            finalEtUdpServerPort.setText(cfg.udp_server_port);
                        if (!cfg.udp_client_port.isEmpty())
                            finalEtUdpClientPort.setText(cfg.udp_client_port);
                    }
                }

                @Override
                public void onNothingSelected(AdapterView<?> parent) {
                    btnDelete.setEnabled(false);
                }
            });

            // --- Delete button: confirm then remove ---
            btnDelete.setOnClickListener(v -> {
                int selectedPos = presetSpinner.getSelectedItemPosition();
                if (selectedPos <= 0) return; // safety — should not be reachable while disabled

                String keyToDelete   = presetKeys.get(selectedPos - 1);
                String labelToDelete = presetLabels.get(selectedPos);

                new AlertDialog.Builder(activity_)
                    .setTitle("Delete preset")
                    .setMessage("Delete \"" + labelToDelete + "\"?")
                    .setPositiveButton("Delete", (confirmDialog, which) -> {
                        // Remove from persistent storage
                        deletePreset(keyToDelete);

                        // Remove from the in-memory lists and refresh the adapter
                        presetKeys.remove(selectedPos - 1);
                        presetLabels.remove(selectedPos);
                        presetAdapter.notifyDataSetChanged();

                        // Reset spinner to the placeholder
                        presetSpinner.setSelection(0);
                        btnDelete.setEnabled(false);
                    })
                    .setNegativeButton("Cancel", null)
                    .show();
            });

            // Wrap in ScrollView for small screens
            ScrollView scroll = new ScrollView(activity_);
            scroll.addView(root);

            // --- Build AlertDialog ---
            AlertDialog.Builder builder = new AlertDialog.Builder(activity_);
            builder.setTitle("ILLIXR Network Configuration");
            builder.setView(scroll);
            builder.setCancelable(false);

            builder.setPositiveButton("Connect", (dialog, which) -> {
                // --- Presence validation: every visible field must be non-empty ---
                if (use_tcp_) {
                    if (finalEtTcpServerIp.getText().toString().trim().isEmpty()
                        || finalEtTcpServerPort.getText().toString().trim().isEmpty()
                        || finalEtTcpClientPort.getText().toString().trim().isEmpty()) {
                        Toast.makeText(activity_, "All TCP fields are required.", Toast.LENGTH_SHORT).show();
                        buildAndShow(latch);
                        return;
                    }
                }
                if (use_udp_) {
                    // The UDP IP field may be disabled (mirroring TCP); read its displayed value.
                    String udpIpText = finalEtUdpServerIp.isEnabled()
                        ? finalEtUdpServerIp.getText().toString().trim()
                        : (use_tcp_ ? finalEtTcpServerIp.getText().toString().trim() : "");
                    if (udpIpText.isEmpty()
                        || finalEtUdpServerPort.getText().toString().trim().isEmpty()
                        || finalEtUdpClientPort.getText().toString().trim().isEmpty()) {
                        Toast.makeText(activity_, "All UDP fields are required.", Toast.LENGTH_SHORT).show();
                        buildAndShow(latch);
                        return;
                    }
                }

                // --- Port-conflict validation (only when both backends are active) ---
                // All four ports must be distinct so that each backend's server and
                // client sockets do not collide.
                if (use_tcp_ && use_udp_) {
                    String tcpServer = finalEtTcpServerPort.getText().toString().trim();
                    String tcpClient = finalEtTcpClientPort.getText().toString().trim();
                    String udpServer = finalEtUdpServerPort.getText().toString().trim();
                    String udpClient = finalEtUdpClientPort.getText().toString().trim();

                    String portConflict = null;
                    if (tcpServer.equals(tcpClient)) {
                        portConflict = "TCP server port and TCP client port must differ.";
                    } else if (tcpServer.equals(udpServer)) {
                        portConflict = "TCP server port and UDP server port must differ.";
                    } else if (tcpServer.equals(udpClient)) {
                        portConflict = "TCP server port and UDP client port must differ.";
                    } else if (tcpClient.equals(udpServer)) {
                        portConflict = "TCP client port and UDP server port must differ.";
                    } else if (tcpClient.equals(udpClient)) {
                        portConflict = "TCP client port and UDP client port must differ.";
                    } else if (udpServer.equals(udpClient)) {
                        portConflict = "UDP server port and UDP client port must differ.";
                    }

                    if (portConflict != null) {
                        Toast.makeText(activity_, portConflict, Toast.LENGTH_LONG).show();
                        buildAndShow(latch);
                        return;
                    }
                }

                // --- Commit results ---
                if (use_tcp_) {
                    tcp_server_ip_   = finalEtTcpServerIp.getText().toString().trim();
                    tcp_server_port_ = finalEtTcpServerPort.getText().toString().trim();
                    tcp_client_port_ = finalEtTcpClientPort.getText().toString().trim();
                }
                if (use_udp_) {
                    // If the IP field is disabled it displays the mirrored TCP value.
                    udp_server_ip_   = finalEtUdpServerIp.getText().toString().trim();
                    udp_server_port_ = finalEtUdpServerPort.getText().toString().trim();
                    udp_client_port_ = finalEtUdpClientPort.getText().toString().trim();
                }
                confirmed_ = true;

                savePreset(finalEtProfileName.getText().toString().trim(),
                    tcp_server_ip_, tcp_server_port_, tcp_client_port_,
                    udp_server_ip_, udp_server_port_, udp_client_port_);
                latch.countDown();
            });

            builder.setNegativeButton("Cancel", (dialog, which) -> {
                confirmed_ = false;
                latch.countDown();
            });

            builder.show();
        }

        // -----------------------------------------------------------------------
        // Preset persistence
        // -----------------------------------------------------------------------

        /**
         * Plain-old-data holder for one stored configuration.
         * Fields are empty strings when not present in a partial preset.
         */
        private static class NetworkConfig {
            String name          = "";
            String tcp_server_ip   = "";
            String tcp_server_port = "";
            String tcp_client_port = "";
            String udp_server_ip   = "";
            String udp_server_port = "";
            String udp_client_port = "";
        }

        /**
         * Encodes a full configuration as a single storable string.
         *
         * <p>Format: {@code name|tcpServerIp|tcpServerPort|tcpClientPort|udpServerIp|udpServerPort|udpClientPort}
         *
         * <p>The name may be empty. Fields for backends that are not currently active are stored as
         * empty strings so that the preset can still be recalled in a future session where both
         * backends are used.
         */
        private String encodePreset(String name,
                                    String tcp_server_ip, String tcp_server_port, String tcp_client_port,
                                    String udp_server_ip, String udp_server_port, String udp_client_port) {
            return name          + FIELD_SEP
                + tcp_server_ip   + FIELD_SEP
                + tcp_server_port + FIELD_SEP
                + tcp_client_port + FIELD_SEP
                + udp_server_ip   + FIELD_SEP
                + udp_server_port + FIELD_SEP
                + udp_client_port;
        }

        /**
         * Decodes a stored preset string into a {@link com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig}.
         * Returns {@code null} if the string is malformed.
         */
        private com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig parsePreset(String encoded) {
            String[] parts = encoded.split("\\" + FIELD_SEP, -1);
            if (parts.length != PRESET_FIELD_COUNT) return null;
            com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig cfg  = new com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig();
            cfg.name           = parts[0];
            cfg.tcp_server_ip   = parts[1];
            cfg.tcp_server_port = parts[2];
            cfg.tcp_client_port = parts[3];
            cfg.udp_server_ip   = parts[4];
            cfg.udp_server_port = parts[5];
            cfg.udp_client_port = parts[6];
            return cfg;
        }

        /**
         * Builds the human-readable label shown in the Spinner for a given preset key.
         *
         * <p>If the preset has a non-empty name that name is used as the label.  Otherwise an
         * auto-generated label is derived from the stored IP addresses and port numbers.
         */
        private String presetDisplayLabel(String encoded) {
            com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog.NetworkConfig cfg = parsePreset(encoded);
            if (cfg == null) return encoded;

            // User-supplied name takes priority
            if (!cfg.name.isEmpty()) return cfg.name;

            // Fallback: auto-generate from addresses and ports
            StringBuilder sb = new StringBuilder();
            boolean has_tcp = !cfg.tcp_server_ip.isEmpty();
            boolean has_udp = !cfg.udp_server_ip.isEmpty();

            if (has_tcp) {
                sb.append(cfg.tcp_server_ip)
                    .append(" TCP ").append(cfg.tcp_server_port).append("/").append(cfg.tcp_client_port);
            }
            if (has_udp) {
                if (has_tcp) sb.append("  ");
                // Only show the UDP IP if it differs from TCP, to keep labels compact
                if (!has_tcp || !cfg.udp_server_ip.equals(cfg.tcp_server_ip)) {
                    sb.append(cfg.udp_server_ip).append(" ");
                }
                sb.append("UDP ").append(cfg.udp_server_port).append("/").append(cfg.udp_client_port);
            }
            return sb.toString();
        }

        /**
         * Returns all stored preset encoded strings from {@link SharedPreferences}.
         */
        private List<String> loadPresetKeys() {
            SharedPreferences prefs = activity_.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            Set<String> raw = prefs.getStringSet(PREFS_PRESETS, new HashSet<>());
            return new ArrayList<>(raw);
        }

        /**
         * Saves a new preset, de-duplicating by exact encoded key.
         *
         * <p>The name is included in the key, so saving the same addresses/ports under a different
         * name creates a distinct preset entry.  Fields for inactive backends are stored as empty
         * strings so the full preset format is always {@value #PRESET_FIELD_COUNT} fields wide.
         */
        private void savePreset(String name,
                                String tcp_server_ip, String tcp_server_port, String tcp_client_port,
                                String udp_server_ip, String udp_server_port, String udp_client_port) {
            SharedPreferences prefs = activity_.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            Set<String> existing = new HashSet<>(prefs.getStringSet(PREFS_PRESETS, new HashSet<>()));
            existing.add(encodePreset(
                name,
                use_tcp_ ? tcp_server_ip   : "",
                use_tcp_ ? tcp_server_port : "",
                use_tcp_ ? tcp_client_port : "",
                use_udp_ ? udp_server_ip   : "",
                use_udp_ ? udp_server_port : "",
                use_udp_ ? udp_client_port : ""));
            prefs.edit().putStringSet(PREFS_PRESETS, existing).apply();
        }

        /**
         * Removes a single preset (identified by its exact encoded key) from {@link SharedPreferences}.
         */
        private void deletePreset(String encoded_key) {
            SharedPreferences prefs = activity_.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            Set<String> existing = new HashSet<>(prefs.getStringSet(PREFS_PRESETS, new HashSet<>()));
            existing.remove(encoded_key);
            prefs.edit().putStringSet(PREFS_PRESETS, existing).apply();
        }

        // -----------------------------------------------------------------------
        // View helpers
        // -----------------------------------------------------------------------

        /** Adds a bold section-header label. */
        private void addSectionHeader(LinearLayout parent, String text) {
            TextView tv = new TextView(activity_);
            tv.setText(text);
            tv.setTextSize(14);
            tv.setTypeface(null, android.graphics.Typeface.BOLD);
            parent.addView(tv);
            addSpacing(parent, 2);
        }

        /**
         * Appends a small label and a single-line EditText to {@code parent}, and returns the field.
         */
        @SuppressLint("SetTextI18n")
        private EditText addLabeledField(LinearLayout parent, String label, String defaultValue) {
            TextView tv = new TextView(activity_);
            tv.setText(label + ":");
            tv.setTextSize(12);
            parent.addView(tv);

            EditText et = new EditText(activity_);
            et.setHint(label);
            et.setText(defaultValue);
            et.setSingleLine(true);
            parent.addView(et);
            addSpacing(parent, 4);
            return et;
        }

        private void addSpacing(LinearLayout parent, int dpValue) {
            View spacer = new View(activity_);
            spacer.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(dpValue)));
            parent.addView(spacer);
        }

        private int dp(int dp) {
            return Math.round(dp * activity_.getResources().getDisplayMetrics().density);
        }

        // -----------------------------------------------------------------------
        // Wi-Fi client IP resolution
        // -----------------------------------------------------------------------

        private String resolveClientIp() {
            WifiManager wm = (WifiManager) activity_.getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
            if (wm == null) return "";
            WifiInfo info = wm.getConnectionInfo();
            int ip = info.getIpAddress();
            return (ip == 0) ? "" : Formatter.formatIpAddress(ip);
        }

        // -----------------------------------------------------------------------
        // JNI entry point
        // -----------------------------------------------------------------------

        /**
         * Static entry point called from JNI ({@code network_config_dialog.cpp}).
         *
         * <p>Creates and runs the dialog synchronously, then assembles the result
         * into a pipe-delimited string.
         *
         * <p>Return format on success (fields present only for active backends, client IP always last):
         * <pre>
         *   [tcpServerIp|tcpServerPort|tcpClientPort|]  (when useTcp)
         *   [udpServerIp|udpServerPort|udpClientPort|]  (when useUdp)
         *   clientIp
         * </pre>
         * Returns an empty string if the user cancelled.
         *
         * @param activity Android activity context.
         * @param useTcp   Whether to show TCP configuration fields.
         * @param useUdp   Whether to show UDP configuration fields.
         */
        public static String showDialog(Activity activity, boolean useTcp, boolean useUdp) {
            com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog dlg = new com.example.ILLIXR.ILLIXRNativeActivity.NetworkConfigDialog(activity, useTcp, useUdp);
            if (!dlg.show()) return "";

            StringBuilder result = new StringBuilder();
            if (useTcp) {
                result.append(dlg.getTcpServerIp()).append('|')
                    .append(dlg.getTcpServerPort()).append('|')
                    .append(dlg.getTcpClientPort()).append('|');
            }
            if (useUdp) {
                result.append(dlg.getUdpServerIp()).append('|')
                    .append(dlg.getUdpServerPort()).append('|')
                    .append(dlg.getUdpClientPort()).append('|');
            }
            result.append(dlg.getClientIp());
            return result.toString();
        }
    }

}
