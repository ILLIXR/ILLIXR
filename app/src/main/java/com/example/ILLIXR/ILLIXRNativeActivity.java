package com.example.ILLIXR;

import android.Manifest;
import android.app.Activity;
import android.app.NativeActivity;
import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicReference;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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
    private static final int STORAGE_REQUEST_CODE = 101;
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
        if (nativeIsBobaEnabled()) {
            String serverIp = getIntent().getStringExtra(SERVER_IP_EXTRA);
            if (serverIp != null && !serverIp.trim().isEmpty()) {
                configureNative(serverIp.trim(), "Android intent");
            }
        }
        super.onCreate(savedInstanceState);

        WifiManager wifiManager = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        wifiLock_ = wifiManager.createWifiLock(
                WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "illixr_wifi_lock");
        wifiLock_.acquire();
        Log.i(TAG, "WiFi low-latency lock acquired");

        if (nativeIsBobaEnabled()) {
            startConfigurationReceiver();
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, CAMERA_REQUEST_CODE);
        } else {
            nativeOnPermissionGranted();
        }

        // READ/WRITE_EXTERNAL_STORAGE were declared in the manifest but never actually
        // requested at runtime anywhere in this Activity. On API 23+, a manifest declaration
        // only makes the app *eligible* to hold a dangerous permission -- it still has to be
        // explicitly requested (and granted) at runtime, exactly like CAMERA above, or
        // checkSelfPermission() for it keeps returning DENIED indefinitely, no matter how long
        // it's been declared. Requested here as its own independent request (separate request
        // code) rather than merged into the CAMERA request above, specifically so
        // nativeOnPermissionGranted()'s existing meaning and timing (tied only to camera
        // readiness) isn't changed by adding this.
        boolean readGranted = ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
        boolean writeGranted = ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
        if (!readGranted || !writeGranted) {
            ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE},
                STORAGE_REQUEST_CODE);
        } else {
            Log.i("ILLIXRNativeActivity", "Storage permissions already granted");
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
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantedResults) {
        if (requestCode == CAMERA_REQUEST_CODE) {
            if (grantedResults.length > 0 && grantedResults[0] == PackageManager.PERMISSION_GRANTED) {
                Log.i("ILLIXRNativeActivity", "Camera permission granted");
                nativeOnPermissionGranted();
            } else {
                Log.e("ILLIXRNativeActivity", "Camera permission denied");
            }
        } else if (requestCode == STORAGE_REQUEST_CODE) {
            boolean allGranted = grantedResults.length > 0;
            for (int result : grantedResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }
            if (allGranted) {
                Log.i("ILLIXRNativeActivity", "Storage permissions granted");
            } else {
                Log.e("ILLIXRNativeActivity", "Storage permissions denied");
            }
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

    /** Query native build selection before using the optional Boba bootstrap. */
    private native boolean nativeIsBobaEnabled();

    /** Supply the discovered desktop address to the waiting native runtime. */
    private native void nativeConfigure(String serverIp);

    /**
     * Off-screen network configuration UI, rendered to a {@link Bitmap} for display as an OpenXR
     * quad layer rather than a real Android Window.
     *
     * <p>Every method on this class is called from a single native thread (the OpenXR render
     * thread) and is not synchronized; there is no concurrent access from the Android main thread,
     * since this UI is never attached to a real Window and never uses runOnUiThread.
     *
     * <p>Deliberately built from plain, non-editable, non-clickable {@link TextView}s only (used
     * both for static text and for anything that looks like a button or field). {@code EditText},
     * {@code Spinner}, and {@code Button} all lazily create a {@code Handler} tied to the
     * constructing thread's {@code Looper} for cursor blink, long-press detection, or ripple
     * animation -- and this class is built on a thread with no Looper. {@code Spinner}'s dropdown is
     * additionally a real popup Window, which would hit the same Horizon-OS-shell invisibility
     * problem that motivated this whole off-screen-panel approach in the first place. Hit-testing
     * against tappable regions is done manually (see {@link #handlePoke}) rather than through
     * Android's click/touch dispatch.
     *
     * <p>Text entry has no system keyboard involved: the numeric keypad and on-screen QWERTY
     * keyboard are part of this same layout, and route into whichever field is "active" (see
     * {@link #active_field_id_}).
     */
    public static class NetworkConfigPanel {

        // -----------------------------------------------------------------------
        // Logging / panel dimensions
        // -----------------------------------------------------------------------

        private static final String TAG = "NetworkConfigPanel";

        private static final int PANEL_WIDTH_PX  = 1100;
        private static final int PANEL_HEIGHT_PX = 1500;

        // Shrinks every widget's width/height/text-size/margin by 25% (applied inside FIXED_PARAMS,
        // MATCH_WIDTH_PARAMS, and the widget factory methods below), while PANEL_WIDTH_PX/HEIGHT_PX
        // above -- the overall canvas/world size -- stay untouched per feedback ("outer box is
        // fine"). Note this means more empty space around the now-smaller layout, not a more
        // tightly-packed one; if a more compact layout is wanted instead, that's a different change
        // (reducing the canvas dimensions to match).
        private static final float UI_SCALE = 0.75f;

        private static final int COLOR_BACKGROUND      = Color.rgb(30, 30, 34);
        private static final int COLOR_FIELD           = Color.rgb(55, 55, 62);
        private static final int COLOR_FIELD_ACTIVE    = Color.rgb(70, 110, 160);
        private static final int COLOR_KEY              = Color.rgb(60, 60, 68);
        private static final int COLOR_ACTION           = Color.rgb(60, 110, 70);
        private static final int COLOR_ACTION_CANCEL    = Color.rgb(120, 60, 60);
        private static final int COLOR_TEXT             = Color.WHITE;
        private static final int COLOR_LABEL            = Color.rgb(180, 180, 190);
        private static final int COLOR_PRESSED          = Color.rgb(230, 230, 235);
        private static final int COLOR_STATUS_MESSAGE   = Color.rgb(240, 200, 80);

        /// How long a tapped button/key stays visibly flashed before reverting to its normal color.
        private static final long PRESS_FLASH_MS = 150;
        /// How long a status message (e.g. "Saved") stays visible before clearing itself.
        private static final long STATUS_MESSAGE_MS = 2500;

        // -----------------------------------------------------------------------
        // Persistent storage (same schema/location as the earlier dialog-based version)
        // -----------------------------------------------------------------------


        private static final String KEY_CONFIGS         = "configs";
        private static final String KEY_NAME            = "name";
        private static final String KEY_TCP_SERVER_IP   = "tcp_server_ip";
        private static final String KEY_TCP_SERVER_PORT = "tcp_server_port";
        private static final String KEY_TCP_CLIENT_PORT = "tcp_client_port";
        private static final String KEY_UDP_SERVER_IP   = "udp_server_ip";
        private static final String KEY_UDP_SERVER_PORT = "udp_server_port";
        private static final String KEY_UDP_CLIENT_PORT = "udp_client_port";

        private static final String DEFAULT_TCP_SERVER_PORT = "9001";
        private static final String DEFAULT_TCP_CLIENT_PORT = "9000";
        private static final String DEFAULT_UDP_SERVER_PORT = "9003";
        private static final String DEFAULT_UDP_CLIENT_PORT = "9002";

        // -----------------------------------------------------------------------
        // Construction / config
        // -----------------------------------------------------------------------

        private final Activity activity_;
        private final boolean  use_tcp_;
        private final boolean  use_udp_;

        private final String client_ip_;

        public NetworkConfigPanel(Activity activity, boolean useTcp, boolean useUdp) {
            // Defensive: nothing in this class should need a Handler, but if some framework code
            // path unexpectedly does, failing with a clear Looper.prepare() state is preferable to
            // an obscure crash deep in a View constructor.
            if (Looper.myLooper() == null) {
                Looper.prepare();
            }

            activity_ = activity;
            use_tcp_  = useTcp;
            use_udp_  = useUdp;
            client_ip_ = detectClientIp();

            // Compare against the matching "constructing NetworkConfigPanel with use_tcp_=... " log
            // in oxr_interface.cpp: if the two values here don't match what that logged, it's a JNI
            // marshalling issue; if they match but the sections still don't render, the bug is in
            // buildUi()'s use of these fields instead.
            Log.i(TAG, "NetworkConfigPanel constructor received useTcp=" + useTcp + " useUdp=" + useUdp);

            cursor_paint_ = new Paint();
            cursor_paint_.setColor(Color.YELLOW);
            cursor_paint_.setAntiAlias(true);

            saved_configs_ = loadSavedConfigs();
            buildUi();
            dirty_ = true;
        }

        // -----------------------------------------------------------------------
        // Field state
        // -----------------------------------------------------------------------

        private final String[] tcp_octets_ = {"", "", "", ""};
        private String          tcp_server_port_ = DEFAULT_TCP_SERVER_PORT;
        private String          tcp_client_port_ = DEFAULT_TCP_CLIENT_PORT;

        private final String[] udp_octets_ = {"", "", "", ""};
        private String          udp_server_port_ = DEFAULT_UDP_SERVER_PORT;
        private String          udp_client_port_ = DEFAULT_UDP_CLIENT_PORT;

        private String  config_name_    = "";
        private boolean same_ip_as_tcp_ = true; // only meaningful when use_tcp_ && use_udp_

        /// Currently focused field, e.g. "field:tcp_octet_0", "field:tcp_server_port", "field:name".
        private String active_field_id_ = null;

        private List<JSONObject> saved_configs_;
        /// -1 means "(New configuration)"; otherwise an index into saved_configs_.
        private int current_config_index_ = -1;

        // -----------------------------------------------------------------------
        // Result state (polled by native each frame; no blocking, no CountDownLatch)
        // -----------------------------------------------------------------------

        private boolean finished_  = false;
        private boolean confirmed_ = false;
        private String  result_    = null;

        public boolean isFinished() {
            return finished_;
        }

        public boolean isConfirmed() {
            return confirmed_;
        }

        public String getResult() {
            return result_;
        }

        // -----------------------------------------------------------------------
        // Rendering
        // -----------------------------------------------------------------------

        private LinearLayout root_;
        private Bitmap       bitmap_;
        private Canvas       canvas_;
        private boolean      dirty_;

        private final Map<String, TextView> widgets_       = new HashMap<>();
        private final Map<String, TextView> field_display_  = new HashMap<>();
        private final Map<String, Rect>     widget_rects_   = new HashMap<>();

        // Mutually exclusive: exactly one of these is visible at a time, depending on whether
        // active_field_id_ is a numeric field (octets/ports) or the free-text name field. See
        // setActiveField().
        private LinearLayout numeric_keypad_container_;
        private LinearLayout text_keyboard_container_;

        // Simple 2D cursor per hand (0=left, 1=right), baked directly into the same bitmap rather
        // than rendered as a separate 3D marker -- a portable stand-in for a real hand skeleton,
        // which would be a substantially larger rendering task on its own.
        private final float[]   hover_u_       = {0f, 0f};
        private final float[]   hover_v_       = {0f, 0f};
        private final boolean[] hover_visible_ = {false, false};
        private final Paint     cursor_paint_;

        private static final float CURSOR_RADIUS_PX = 16f * UI_SCALE;

        // Brief visual feedback for a tapped button/key, expired by time rather than by any other
        // event (see checkPressFlashExpiry()) so it can't get stuck showing if the hand moves away
        // right after tapping and nothing else happens to trigger a redraw.
        private String pressed_widget_id_  = null;
        private long   pressed_since_ms_   = 0;

        // Transient status message (e.g. "Saved", or a reason something didn't happen), shown in
        // status_message_view_ and expired the same time-based way as the press flash above.
        private String   status_message_          = null;
        private long     status_message_since_ms_  = 0;
        private TextView status_message_view_;

        public int getPanelWidthPx() {
            return PANEL_WIDTH_PX;
        }

        public int getPanelHeightPx() {
            return PANEL_HEIGHT_PX;
        }

        /**
         * Redraws the backing bitmap if anything changed since the last call.
         *
         * @return true if a redraw happened (native should re-upload the texture), false if the
         *         bitmap is unchanged from the last call.
         */
        public boolean render() {
            checkPressFlashExpiry();
            checkStatusMessageExpiry();
            if (!dirty_) {
                return false;
            }
            refreshFieldDisplays();
            refreshHighlights();
            if (status_message_view_ != null) {
                status_message_view_.setText(status_message_ != null ? status_message_ : "");
            }
            canvas_.drawColor(COLOR_BACKGROUND);
            root_.draw(canvas_);
            for (int hand = 0; hand < 2; hand++) {
                if (hover_visible_[hand]) {
                    canvas_.drawCircle(hover_u_[hand] * PANEL_WIDTH_PX, hover_v_[hand] * PANEL_HEIGHT_PX,
                        CURSOR_RADIUS_PX, cursor_paint_);
                }
            }
            dirty_ = false;
            return true;
        }

        public Bitmap getBitmap() {
            return bitmap_;
        }

        /**
         * Converts a poke event, in normalized panel-space coordinates (0,0 = top-left, 1,1 =
         * bottom-right -- matching Android's top-down Canvas convention; verify this against
         * whatever V-axis convention the quad layer's UVs actually use before wiring this up, since
         * that boundary is an easy thing to get flipped), into a tap against whichever widget
         * occupies that point. Acts only on the down edge; native is responsible for tracking the
         * poke state machine and calling this only on down/up transitions.
         *
         * @param u horizontal position, 0..1
         * @param v vertical position, 0..1
         * @param down true on the down edge of a poke, false on the up edge (currently ignored)
         */
        public void handlePoke(float u, float v, boolean down) {
            if (!down) {
                return;
            }
            int px = Math.round(u * PANEL_WIDTH_PX);
            int py = Math.round(v * PANEL_HEIGHT_PX);
            for (Map.Entry<String, Rect> entry : widget_rects_.entrySet()) {
                if (entry.getValue().contains(px, py)) {
                    handleTap(entry.getKey());
                    break;
                }
            }
        }

        /**
         * Reports the current projected position of one hand's fingertip, called every frame
         * regardless of whether it's actively poking, so a cursor can track the hand as it
         * approaches the panel rather than only appearing at the moment of a tap.
         *
         * @param hand 0 for left, 1 for right
         * @param u horizontal position, 0..1 (same convention as handlePoke)
         * @param v vertical position, 0..1
         * @param visible whether the hand is currently close enough to the panel to show a cursor
         */
        public void updateHover(int hand, float u, float v, boolean visible) {
            if (hand < 0 || hand > 1) {
                return;
            }
            if (hover_visible_[hand] == visible && !visible) {
                return; // avoid marking dirty every frame while simply not hovering
            }
            hover_u_[hand]       = u;
            hover_v_[hand]       = v;
            hover_visible_[hand] = visible;
            dirty_ = true;
        }

        // -----------------------------------------------------------------------
        // Tap handling
        // -----------------------------------------------------------------------

        private void handleTap(String id) {
            if (id.startsWith("field:")) {
                setActiveField(id);
                return;
            }
            flashPressedWidget(id);
            switch (id) {
                case "cycle:prev":
                    cycleConfig(-1);
                    break;
                case "cycle:next":
                    cycleConfig(1);
                    break;
                case "action:delete":
                    deleteCurrentConfig();
                    break;
                case "action:save":
                    onSave();
                    break;
                case "action:connect":
                    onConnect();
                    break;
                case "action:cancel":
                    confirmed_ = false;
                    finished_  = true;
                    break;
                case "action:next_field":
                    advanceToNextField();
                    break;
                case "toggle:same_ip":
                    same_ip_as_tcp_ = !same_ip_as_tcp_;
                    if (same_ip_as_tcp_) {
                        mirrorTcpToUdp();
                    }
                    dirty_ = true;
                    break;
                case "key:backspace":
                case "numpad:backspace":
                    backspaceActiveField();
                    break;
                case "key:space":
                    appendToActiveField(" ");
                    break;
                default:
                    if (id.startsWith("key:")) {
                        appendToActiveField(id.substring("key:".length()));
                    } else if (id.startsWith("numpad:")) {
                        appendToActiveField(id.substring("numpad:".length()));
                    }
                    break;
            }
        }

        /**
         * Selects a field as active and swaps which on-screen input widget is visible: the compact
         * numeric keypad for octet/port fields, or the full keyboard (with a numeric top row) for
         * the free-text name field. Triggers a re-layout, since the two widgets differ in height.
         */
        private void setActiveField(String fieldId) {
            active_field_id_ = fieldId;
            boolean numeric = isNumericField(fieldId);
            numeric_keypad_container_.setVisibility(numeric ? View.VISIBLE : View.GONE);
            text_keyboard_container_.setVisibility(numeric ? View.GONE : View.VISIBLE);
            relayout();
            dirty_ = true;
        }

        /** Every field except the free-text name field is numeric (octets, ports). */
        private static boolean isNumericField(String fieldId) {
            return fieldId != null && !fieldId.equals("field:name");
        }

        /**
         * Advances to the next field in tab order, wrapping back to the first after the last.
         * Skips UDP octet fields while they're mirrored from TCP (same_ip_as_tcp_), since those
         * aren't independently editable in that state.
         */
        private void advanceToNextField() {
            List<String> order = buildFieldOrder();
            if (order.isEmpty()) {
                return;
            }
            int current = order.indexOf(active_field_id_);
            int next    = (current < 0) ? 0 : (current + 1) % order.size();
            setActiveField(order.get(next));
        }

        private List<String> buildFieldOrder() {
            List<String> order = new ArrayList<>();
            if (use_tcp_) {
                for (int i = 0; i < 4; i++) {
                    order.add("field:tcp_octet_" + i);
                }
                order.add("field:tcp_server_port");
                order.add("field:tcp_client_port");
            }
            if (use_udp_) {
                if (!same_ip_as_tcp_) {
                    for (int i = 0; i < 4; i++) {
                        order.add("field:udp_octet_" + i);
                    }
                }
                order.add("field:udp_server_port");
                order.add("field:udp_client_port");
            }
            order.add("field:name");
            return order;
        }

        private void appendToActiveField(String s) {
            if (active_field_id_ == null) {
                return;
            }
            if (active_field_id_.equals("field:name")) {
                config_name_ += s;
            } else if (!s.matches("[0-9]")) {
                // Only the name field accepts non-digit characters from the QWERTY keyboard.
                return;
            } else if (active_field_id_.startsWith("field:tcp_octet_")) {
                int i = Integer.parseInt(active_field_id_.substring("field:tcp_octet_".length()));
                if (tcp_octets_[i].length() < 3) {
                    tcp_octets_[i] += s;
                    if (same_ip_as_tcp_) {
                        mirrorTcpToUdp();
                    }
                }
            } else if (active_field_id_.startsWith("field:udp_octet_") && !same_ip_as_tcp_) {
                int i = Integer.parseInt(active_field_id_.substring("field:udp_octet_".length()));
                if (udp_octets_[i].length() < 3) {
                    udp_octets_[i] += s;
                }
            } else if (active_field_id_.equals("field:tcp_server_port") && tcp_server_port_.length() < 5) {
                tcp_server_port_ += s;
            } else if (active_field_id_.equals("field:tcp_client_port") && tcp_client_port_.length() < 5) {
                tcp_client_port_ += s;
            } else if (active_field_id_.equals("field:udp_server_port") && udp_server_port_.length() < 5) {
                udp_server_port_ += s;
            } else if (active_field_id_.equals("field:udp_client_port") && udp_client_port_.length() < 5) {
                udp_client_port_ += s;
            }
            dirty_ = true;
        }

        private void backspaceActiveField() {
            if (active_field_id_ == null) {
                return;
            }
            if (active_field_id_.equals("field:name") && !config_name_.isEmpty()) {
                config_name_ = config_name_.substring(0, config_name_.length() - 1);
            } else if (active_field_id_.startsWith("field:tcp_octet_")) {
                int i = Integer.parseInt(active_field_id_.substring("field:tcp_octet_".length()));
                if (!tcp_octets_[i].isEmpty()) {
                    tcp_octets_[i] = tcp_octets_[i].substring(0, tcp_octets_[i].length() - 1);
                    if (same_ip_as_tcp_) {
                        mirrorTcpToUdp();
                    }
                }
            } else if (active_field_id_.startsWith("field:udp_octet_") && !same_ip_as_tcp_) {
                int i = Integer.parseInt(active_field_id_.substring("field:udp_octet_".length()));
                if (!udp_octets_[i].isEmpty()) {
                    udp_octets_[i] = udp_octets_[i].substring(0, udp_octets_[i].length() - 1);
                }
            } else {
                String cur = getPortFieldValue(active_field_id_);
                if (cur != null && !cur.isEmpty()) {
                    setPortFieldValue(active_field_id_, cur.substring(0, cur.length() - 1));
                }
            }
            dirty_ = true;
        }

        private String getPortFieldValue(String fieldId) {
            switch (fieldId) {
                case "field:tcp_server_port": return tcp_server_port_;
                case "field:tcp_client_port": return tcp_client_port_;
                case "field:udp_server_port": return udp_server_port_;
                case "field:udp_client_port": return udp_client_port_;
                default: return null;
            }
        }

        private void setPortFieldValue(String fieldId, String value) {
            switch (fieldId) {
                case "field:tcp_server_port": tcp_server_port_ = value; break;
                case "field:tcp_client_port": tcp_client_port_ = value; break;
                case "field:udp_server_port": udp_server_port_ = value; break;
                case "field:udp_client_port": udp_client_port_ = value; break;
                default: break;
            }
        }

        private void mirrorTcpToUdp() {
            System.arraycopy(tcp_octets_, 0, udp_octets_, 0, 4);
        }

        // -----------------------------------------------------------------------
        // Connect / Save / Delete / Cycle
        // -----------------------------------------------------------------------

        private void onConnect() {
            if (use_tcp_ && !isValidOctets(tcp_octets_)) {
                showStatusMessage("TCP server IP is incomplete or invalid");
                return;
            }
            if (use_udp_ && !isValidOctets(udp_octets_)) {
                showStatusMessage("UDP server IP is incomplete or invalid");
                return;
            }
            if (use_tcp_ && use_udp_ && portsConflict()) {
                showStatusMessage("TCP and UDP ports must all be different");
                return;
            }

            StringBuilder sb = new StringBuilder();
            if (use_tcp_) {
                sb.append(joinOctets(tcp_octets_)).append('|')
                    .append(tcp_server_port_).append('|')
                    .append(tcp_client_port_).append('|');
            }
            if (use_udp_) {
                sb.append(joinOctets(udp_octets_)).append('|')
                    .append(udp_server_port_).append('|')
                    .append(udp_client_port_).append('|');
            }
            sb.append(client_ip_);

            result_    = sb.toString();
            confirmed_ = true;
            finished_  = true;
        }

        private boolean portsConflict() {
            String[] ports = {tcp_server_port_, tcp_client_port_, udp_server_port_, udp_client_port_};
            for (int i = 0; i < ports.length; i++) {
                for (int j = i + 1; j < ports.length; j++) {
                    if (ports[i].equals(ports[j])) {
                        return true;
                    }
                }
            }
            return false;
        }

        private void onSave() {
            if (!hasStoragePermission()) {
                boolean read = activity_.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
                boolean write = activity_.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
                showStatusMessage("Storage permission not granted - see logcat");
                Log.w(TAG, "Save failed: READ_EXTERNAL_STORAGE granted=" + read
                    + ", WRITE_EXTERNAL_STORAGE granted=" + write);
                return;
            }
            if (config_name_.isEmpty()) {
                showStatusMessage("Enter a name before saving");
                return;
            }
            if (use_tcp_ && !isValidOctets(tcp_octets_)) {
                showStatusMessage("TCP server IP is incomplete or invalid");
                return;
            }
            if (use_udp_ && !isValidOctets(udp_octets_)) {
                showStatusMessage("UDP server IP is incomplete or invalid");
                return;
            }

            JSONObject cfg = new JSONObject();
            try {
                cfg.put(KEY_NAME, config_name_);
                cfg.put(KEY_TCP_SERVER_IP, use_tcp_ ? joinOctets(tcp_octets_) : "");
                cfg.put(KEY_TCP_SERVER_PORT, use_tcp_ ? tcp_server_port_ : "");
                cfg.put(KEY_TCP_CLIENT_PORT, use_tcp_ ? tcp_client_port_ : "");
                cfg.put(KEY_UDP_SERVER_IP, use_udp_ ? joinOctets(udp_octets_) : "");
                cfg.put(KEY_UDP_SERVER_PORT, use_udp_ ? udp_server_port_ : "");
                cfg.put(KEY_UDP_CLIENT_PORT, use_udp_ ? udp_client_port_ : "");
            } catch (JSONException e) {
                Log.w(TAG, "Failed to build config JSON", e);
                showStatusMessage("Save failed (see logcat)");
                return;
            }

            List<JSONObject> configs = loadSavedConfigs();
            for (int i = configs.size() - 1; i >= 0; i--) {
                if (config_name_.equals(configs.get(i).optString(KEY_NAME))) {
                    configs.remove(i);
                }
            }
            configs.add(cfg);
            writeSavedConfigs(configs);

            // writeSavedConfigs() only logs a warning internally on failure rather than throwing, so
            // read back what's actually on disk to confirm the write really happened before claiming
            // success -- otherwise a silent write failure (e.g. disk full, unexpected I/O error)
            // would still show "Saved" despite nothing actually having been persisted.
            List<JSONObject> verify = loadSavedConfigs();
            boolean persisted = false;
            for (JSONObject c : verify) {
                if (config_name_.equals(c.optString(KEY_NAME))) {
                    persisted = true;
                    break;
                }
            }

            saved_configs_ = configs;
            current_config_index_ = saved_configs_.size() - 1;
            dirty_ = true;

            if (persisted) {
                showStatusMessage("Saved \"" + config_name_ + "\"");
            } else {
                showStatusMessage("Save may have failed - see logcat");
                Log.w(TAG, "writeSavedConfigs() completed but \"" + config_name_
                    + "\" wasn't found when reading it back from MediaStore.Downloads ("
                    + CONFIG_RELATIVE_PATH + CONFIG_DISPLAY_NAME + ")");
            }
        }

        private void deleteCurrentConfig() {
            if (current_config_index_ < 0 || current_config_index_ >= saved_configs_.size()) {
                return;
            }
            String name = saved_configs_.get(current_config_index_).optString(KEY_NAME, "");

            List<JSONObject> configs = loadSavedConfigs();
            for (int i = configs.size() - 1; i >= 0; i--) {
                if (name.equals(configs.get(i).optString(KEY_NAME))) {
                    configs.remove(i);
                }
            }
            writeSavedConfigs(configs);
            saved_configs_ = configs;
            current_config_index_ = -1;
            resetFieldsToDefaults();
            dirty_ = true;
        }

        private void cycleConfig(int direction) {
            saved_configs_ = loadSavedConfigs();
            int maxIndex = saved_configs_.size() - 1;
            current_config_index_ += direction;
            if (current_config_index_ < -1) {
                current_config_index_ = maxIndex;
            }
            if (current_config_index_ > maxIndex) {
                current_config_index_ = -1;
            }

            if (current_config_index_ == -1) {
                resetFieldsToDefaults();
            } else {
                applySavedConfig(saved_configs_.get(current_config_index_));
            }
            dirty_ = true;
        }

        /** Populates fields from a saved configuration; disables IP mirroring, per prior design. */
        private void applySavedConfig(JSONObject cfg) {
            same_ip_as_tcp_ = false;
            config_name_    = cfg.optString(KEY_NAME, "");
            if (use_tcp_) {
                setOctets(tcp_octets_, cfg.optString(KEY_TCP_SERVER_IP, ""));
                tcp_server_port_ = orDefault(cfg.optString(KEY_TCP_SERVER_PORT, ""), DEFAULT_TCP_SERVER_PORT);
                tcp_client_port_ = orDefault(cfg.optString(KEY_TCP_CLIENT_PORT, ""), DEFAULT_TCP_CLIENT_PORT);
            }
            if (use_udp_) {
                setOctets(udp_octets_, cfg.optString(KEY_UDP_SERVER_IP, ""));
                udp_server_port_ = orDefault(cfg.optString(KEY_UDP_SERVER_PORT, ""), DEFAULT_UDP_SERVER_PORT);
                udp_client_port_ = orDefault(cfg.optString(KEY_UDP_CLIENT_PORT, ""), DEFAULT_UDP_CLIENT_PORT);
            }
        }

        private void resetFieldsToDefaults() {
            for (int i = 0; i < 4; i++) {
                tcp_octets_[i] = "";
                udp_octets_[i] = "";
            }
            tcp_server_port_ = DEFAULT_TCP_SERVER_PORT;
            tcp_client_port_ = DEFAULT_TCP_CLIENT_PORT;
            udp_server_port_ = DEFAULT_UDP_SERVER_PORT;
            udp_client_port_ = DEFAULT_UDP_CLIENT_PORT;
            config_name_     = "";
            same_ip_as_tcp_  = use_tcp_ && use_udp_;
        }

        private static String orDefault(String value, String fallback) {
            return (value == null || value.isEmpty()) ? fallback : value;
        }

        // -----------------------------------------------------------------------
        // UI construction
        // -----------------------------------------------------------------------

        private void buildUi() {
            root_ = new LinearLayout(activity_);
            root_.setOrientation(LinearLayout.VERTICAL);
            int pad = Math.round(24 * UI_SCALE);
            root_.setPadding(pad, pad, pad, pad);

            addLabel(root_, "ILLIXR Network Configuration", 36, true);
            addLabel(root_, "Device IP: " + (client_ip_.isEmpty() ? "(unknown)" : client_ip_), 24, false);

            addCyclerRow();

            if (use_tcp_) {
                addLabel(root_, "TCP", 30, true);
                addOctetRow("tcp_octet_", tcp_octets_);
                addPortRow("tcp_server_port", "Server Port");
                addPortRow("tcp_client_port", "Client Port");
            }

            if (use_udp_) {
                addLabel(root_, "UDP", 30, true);
                if (use_tcp_) {
                    addToggleRow();
                }
                addOctetRow("udp_octet_", udp_octets_);
                addPortRow("udp_server_port", "Server Port");
                addPortRow("udp_client_port", "Client Port");
            }

            addLabel(root_, "Configuration Name", 24, false);
            TextView nameField = makeFieldView();
            addTappable(root_, "field:name", nameField, MATCH_WIDTH_PARAMS(90));
            field_display_.put("field:name", nameField);

            addNumericKeypad();
            addTextKeyboard();

            status_message_view_ = makeLabelView("", 22, false);
            status_message_view_.setTextColor(COLOR_STATUS_MESSAGE);
            root_.addView(status_message_view_);

            addActionRow();

            createBitmapAndCanvas();
            relayout();
        }

        private void addCyclerRow() {
            LinearLayout row = newRow();
            addTappable(row, "cycle:prev", makeKeyView("<"), FIXED_PARAMS(80, 80));
            TextView label = makeLabelView("(New configuration)", 22, false);
            label.setGravity(Gravity.CENTER);
            row.addView(label, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            field_display_.put("cycle:label", label);
            addTappable(row, "cycle:next", makeKeyView(">"), FIXED_PARAMS(80, 80));
            addTappable(row, "action:delete", makeActionView("Delete", COLOR_ACTION_CANCEL), FIXED_PARAMS(160, 80));
            root_.addView(row);
        }

        private void addToggleRow() {
            LinearLayout row = newRow();
            TextView toggle = makeFieldView();
            toggle.setText("[ ] Use same server IP as TCP");
            addTappable(row, "toggle:same_ip", toggle, MATCH_WIDTH_PARAMS(80));
            field_display_.put("toggle:same_ip", toggle);
            root_.addView(row);
        }

        private void addOctetRow(String idPrefix, String[] octets) {
            LinearLayout row = newRow();
            for (int i = 0; i < 4; i++) {
                String id = "field:" + idPrefix + i;
                TextView field = makeFieldView();
                addTappable(row, id, field, FIXED_PARAMS(120, 90));
                field_display_.put(id, field);
                if (i < 3) {
                    addLabel(row, ".", 30, false);
                }
            }
            root_.addView(row);
        }

        private void addPortRow(String idSuffix, String labelText) {
            LinearLayout row = newRow();
            addLabel(row, labelText, 22, false).setLayoutParams(
                new LinearLayout.LayoutParams(Math.round(280 * UI_SCALE), LinearLayout.LayoutParams.WRAP_CONTENT));
            String id = "field:" + idSuffix;
            TextView field = makeFieldView();
            addTappable(row, id, field, FIXED_PARAMS(200, 80));
            field_display_.put(id, field);
            root_.addView(row);
        }

        /**
         * Compact numeric keypad shown only when a numeric field (octet/port) is active. Digit/
         * backspace ids use a "numpad:" prefix rather than "key:", distinct from the text
         * keyboard's own digit-row ids -- both keyboards are always present in the view hierarchy
         * (just one or the other GONE at a time), so reusing "key:0" in both would collide in the
         * widgets_ map and make one of them silently untappable.
         */
        private void addNumericKeypad() {
            numeric_keypad_container_ = new LinearLayout(activity_);
            numeric_keypad_container_.setOrientation(LinearLayout.VERTICAL);
            numeric_keypad_container_.setVisibility(View.GONE);

            String[] rows = {"123", "456", "789"};
            for (String r : rows) {
                LinearLayout row = newRow();
                for (char c : r.toCharArray()) {
                    addTappable(row, "numpad:" + c, makeKeyView(String.valueOf(c)), FIXED_PARAMS(100, 90));
                }
                numeric_keypad_container_.addView(row);
            }
            LinearLayout lastRow = newRow();
            addTappable(lastRow, "numpad:0", makeKeyView("0"), FIXED_PARAMS(100, 90));
            addTappable(lastRow, "numpad:backspace", makeKeyView("<-"), FIXED_PARAMS(100, 90));
            addTappable(lastRow, "action:next_field", makeActionView("Next", COLOR_ACTION), FIXED_PARAMS(160, 90));
            numeric_keypad_container_.addView(lastRow);

            root_.addView(numeric_keypad_container_);
        }

        /**
         * Full keyboard shown only when the free-text name field is active, with digits as its own
         * horizontal top row (rather than a separate numeric pad) since this is for general text
         * entry, not numeric-only fields.
         */
        private void addTextKeyboard() {
            text_keyboard_container_ = new LinearLayout(activity_);
            text_keyboard_container_.setOrientation(LinearLayout.VERTICAL);
            text_keyboard_container_.setVisibility(View.GONE);

            String[] rows = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
            for (String r : rows) {
                LinearLayout row = newRow();
                for (char c : r.toCharArray()) {
                    addTappable(row, "key:" + c, makeKeyView(String.valueOf(c)), FIXED_PARAMS(90, 80));
                }
                text_keyboard_container_.addView(row);
            }
            LinearLayout lastRow = newRow();
            addTappable(lastRow, "key:space", makeKeyView("space"), FIXED_PARAMS(400, 80));
            addTappable(lastRow, "key:backspace", makeKeyView("<-"), FIXED_PARAMS(200, 80));
            text_keyboard_container_.addView(lastRow);

            root_.addView(text_keyboard_container_);
        }

        private void addActionRow() {
            LinearLayout row = newRow();
            addTappable(row, "action:save", makeActionView("Save", COLOR_ACTION), FIXED_PARAMS(200, 100));
            addTappable(row, "action:connect", makeActionView("Connect", COLOR_ACTION), FIXED_PARAMS(240, 100));
            addTappable(row, "action:cancel", makeActionView("Cancel", COLOR_ACTION_CANCEL), FIXED_PARAMS(200, 100));
            root_.addView(row);
        }

        // -----------------------------------------------------------------------
        // Widget factories
        // -----------------------------------------------------------------------

        private LinearLayout newRow() {
            LinearLayout row = new LinearLayout(activity_);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            int vpad = Math.round(8 * UI_SCALE);
            row.setPadding(0, vpad, 0, vpad);
            return row;
        }

        private TextView addLabel(LinearLayout parent, String text, int textSizeSp, boolean bold) {
            TextView label = makeLabelView(text, textSizeSp, bold);
            parent.addView(label);
            return label;
        }

        private TextView makeLabelView(String text, int textSizeSp, boolean bold) {
            TextView tv = new TextView(activity_);
            tv.setText(text);
            tv.setTextColor(bold ? COLOR_TEXT : COLOR_LABEL);
            tv.setTextSize(textSizeSp * UI_SCALE);
            if (bold) {
                tv.setTypeface(null, Typeface.BOLD);
            }
            return tv;
        }

        private TextView makeFieldView() {
            TextView tv = new TextView(activity_);
            tv.setTextColor(COLOR_TEXT);
            tv.setTextSize(26 * UI_SCALE);
            tv.setGravity(Gravity.CENTER);
            tv.setBackgroundColor(COLOR_FIELD);
            return tv;
        }

        private TextView makeKeyView(String label) {
            TextView tv = new TextView(activity_);
            tv.setText(label);
            tv.setTextColor(COLOR_TEXT);
            tv.setTextSize(24 * UI_SCALE);
            tv.setGravity(Gravity.CENTER);
            tv.setBackgroundColor(COLOR_KEY);
            return tv;
        }

        private TextView makeActionView(String label, int color) {
            TextView tv = new TextView(activity_);
            tv.setText(label);
            tv.setTextColor(COLOR_TEXT);
            tv.setTextSize(26 * UI_SCALE);
            tv.setGravity(Gravity.CENTER);
            tv.setBackgroundColor(color);
            return tv;
        }

        private void addTappable(LinearLayout parent, String id, TextView view, LinearLayout.LayoutParams params) {
            view.setLayoutParams(params);
            parent.addView(view);
            widgets_.put(id, view);
        }

        private static LinearLayout.LayoutParams FIXED_PARAMS(int widthPx, int heightPx) {
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                Math.round(widthPx * UI_SCALE), Math.round(heightPx * UI_SCALE));
            int margin = Math.round(6 * UI_SCALE);
            p.setMargins(margin, margin, margin, margin);
            return p;
        }

        private static LinearLayout.LayoutParams MATCH_WIDTH_PARAMS(int heightPx) {
            LinearLayout.LayoutParams p =
                new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.round(heightPx * UI_SCALE));
            int margin = Math.round(6 * UI_SCALE);
            p.setMargins(margin, margin, margin, margin);
            return p;
        }

        // -----------------------------------------------------------------------
        // Layout / hit-rect caching / bitmap setup
        // -----------------------------------------------------------------------

        private void createBitmapAndCanvas() {
            bitmap_ = Bitmap.createBitmap(PANEL_WIDTH_PX, PANEL_HEIGHT_PX, Bitmap.Config.ARGB_8888);
            canvas_ = new Canvas(bitmap_);
        }

        /**
         * Re-runs measure/layout and refreshes cached hit rects. Called once after the initial
         * build, and again every time setActiveField() swaps which keypad/keyboard is visible,
         * since that changes the laid-out height (and thus the position of everything below it,
         * including the Save/Connect/Cancel row).
         */
        private void relayout() {
            int wSpec = View.MeasureSpec.makeMeasureSpec(PANEL_WIDTH_PX, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(PANEL_HEIGHT_PX, View.MeasureSpec.AT_MOST);
            root_.measure(wSpec, hSpec);
            root_.layout(0, 0, PANEL_WIDTH_PX, root_.getMeasuredHeight());
            cacheWidgetRects();
        }

        /** Absolute (panel-space) hit rects, recomputed whenever the layout's shape changes. */
        private void cacheWidgetRects() {
            widget_rects_.clear();
            for (Map.Entry<String, TextView> entry : widgets_.entrySet()) {
                if (!isEffectivelyVisible(entry.getValue())) {
                    continue; // e.g. a key belonging to whichever of the keypad/keyboard is GONE
                }
                widget_rects_.put(entry.getKey(), getAbsoluteRect(entry.getValue()));
            }
        }

        /**
         * A View's own getVisibility() only reflects its own flag, not whether a GONE ancestor
         * (like numeric_keypad_container_ or text_keyboard_container_) hides it -- walk up the
         * parent chain to check the effective visibility instead.
         */
        private static boolean isEffectivelyVisible(View v) {
            View cur = v;
            while (cur != null) {
                if (cur.getVisibility() != View.VISIBLE) {
                    return false;
                }
                ViewParent parent = cur.getParent();
                cur = (parent instanceof View) ? (View) parent : null;
            }
            return true;
        }

        private static Rect getAbsoluteRect(View v) {
            int x = 0;
            int y = 0;
            View cur = v;
            while (cur != null) {
                x += cur.getLeft();
                y += cur.getTop();
                ViewParent parent = cur.getParent();
                cur = (parent instanceof View) ? (View) parent : null;
            }
            return new Rect(x, y, x + v.getWidth(), y + v.getHeight());
        }

        private void refreshFieldDisplays() {
            if (use_tcp_) {
                for (int i = 0; i < 4; i++) {
                    field_display_.get("field:tcp_octet_" + i).setText(tcp_octets_[i]);
                }
                field_display_.get("field:tcp_server_port").setText(tcp_server_port_);
                field_display_.get("field:tcp_client_port").setText(tcp_client_port_);
            }
            if (use_udp_) {
                for (int i = 0; i < 4; i++) {
                    field_display_.get("field:udp_octet_" + i).setText(udp_octets_[i]);
                }
                field_display_.get("field:udp_server_port").setText(udp_server_port_);
                field_display_.get("field:udp_client_port").setText(udp_client_port_);
                if (use_tcp_) {
                    field_display_.get("toggle:same_ip").setText(
                        (same_ip_as_tcp_ ? "[x] " : "[ ] ") + "Use same server IP as TCP");
                }
            }
            field_display_.get("field:name").setText(config_name_);

            String cyclerText = (current_config_index_ == -1)
                ? "(New configuration)"
                : saved_configs_.get(current_config_index_).optString(KEY_NAME, "(unnamed)");
            field_display_.get("cycle:label").setText(cyclerText);
        }

        private void refreshHighlights() {
            for (Map.Entry<String, TextView> entry : field_display_.entrySet()) {
                if (!entry.getKey().startsWith("field:")) {
                    continue;
                }
                boolean isActive = entry.getKey().equals(active_field_id_);
                entry.getValue().setBackgroundColor(isActive ? COLOR_FIELD_ACTIVE : COLOR_FIELD);
            }
        }

        /** Immediately flashes a tapped (non-field) widget; see checkPressFlashExpiry() for reversion. */
        private void flashPressedWidget(String id) {
            TextView tv = widgets_.get(id);
            if (tv == null) {
                return;
            }
            tv.setBackgroundColor(COLOR_PRESSED);
            pressed_widget_id_ = id;
            pressed_since_ms_  = System.currentTimeMillis();
            dirty_ = true;
        }

        /**
         * Reverts a flashed widget back to its normal color once PRESS_FLASH_MS has elapsed. Called
         * unconditionally at the top of render(), which native code calls every frame regardless of
         * dirty_ -- so the flash reliably expires even if nothing else happens to trigger a redraw
         * (e.g. the hand moves away right after tapping, with no further hover updates).
         */
        private void checkPressFlashExpiry() {
            if (pressed_widget_id_ == null) {
                return;
            }
            if (System.currentTimeMillis() - pressed_since_ms_ <= PRESS_FLASH_MS) {
                return;
            }
            TextView tv = widgets_.get(pressed_widget_id_);
            if (tv != null) {
                tv.setBackgroundColor(baseColorForWidget(pressed_widget_id_));
            }
            pressed_widget_id_ = null;
            dirty_ = true;
        }

        /** The steady-state (non-flashed, non-active) background color for a given non-field id. */
        private static int baseColorForWidget(String id) {
            if (id.equals("action:cancel") || id.equals("action:delete")) {
                return COLOR_ACTION_CANCEL;
            }
            if (id.startsWith("action:")) {
                return COLOR_ACTION;
            }
            if (id.equals("toggle:same_ip")) {
                return COLOR_FIELD;
            }
            return COLOR_KEY; // key:*, numpad:*, cycle:*
        }

        /** Shows a transient status message (e.g. "Saved", or why an action didn't happen). */
        private void showStatusMessage(String text) {
            status_message_          = text;
            status_message_since_ms_ = System.currentTimeMillis();
            dirty_ = true;
        }

        /** Same time-based expiry pattern as checkPressFlashExpiry(); see that method's comment. */
        private void checkStatusMessageExpiry() {
            if (status_message_ == null) {
                return;
            }
            if (System.currentTimeMillis() - status_message_since_ms_ <= STATUS_MESSAGE_MS) {
                return;
            }
            status_message_ = null;
            dirty_ = true;
        }

        // -----------------------------------------------------------------------
        // IP / validation helpers
        // -----------------------------------------------------------------------

        private static String joinOctets(String[] octets) {
            return octets[0] + "." + octets[1] + "." + octets[2] + "." + octets[3];
        }

        private static void setOctets(String[] octets, String ip) {
            if (ip == null || ip.isEmpty()) {
                return;
            }
            String[] parts = ip.split("\\.", -1);
            if (parts.length != 4) {
                return;
            }
            System.arraycopy(parts, 0, octets, 0, 4);
        }

        private static boolean isValidOctets(String[] octets) {
            for (String octet : octets) {
                if (octet.isEmpty()) {
                    return false;
                }
                int value;
                try {
                    value = Integer.parseInt(octet);
                } catch (NumberFormatException e) {
                    return false;
                }
                if (value < 0 || value > 255) {
                    return false;
                }
            }
            return true;
        }

        private static String detectClientIp() {
            try {
                Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
                while (interfaces != null && interfaces.hasMoreElements()) {
                    NetworkInterface iface = interfaces.nextElement();
                    if (!iface.isUp() || iface.isLoopback()) {
                        continue;
                    }
                    Enumeration<InetAddress> addresses = iface.getInetAddresses();
                    while (addresses.hasMoreElements()) {
                        InetAddress addr = addresses.nextElement();
                        if (addr instanceof Inet4Address && !addr.isLoopbackAddress()) {
                            return addr.getHostAddress();
                        }
                    }
                }
            } catch (SocketException e) {
                Log.w(TAG, "Failed to enumerate network interfaces", e);
            }
            return "";
        }

        // -----------------------------------------------------------------------
        // Persistence: MediaStore.Downloads rather than a raw file path. Writing (and reading back
        // within the same install) needs no permission at all under scoped storage; this project
        // already has READ_EXTERNAL_STORAGE/WRITE_EXTERNAL_STORAGE declared and grants them cleanly
        // via the pre-launch system dialog, so those are checked here as a matter of being
        // consistent with that existing flow, not because scoped storage strictly requires them for
        // the same-install case. Reading a config saved by a *previous* install (surviving an actual
        // uninstall/reinstall) is the one edge case genuinely worth testing on-device rather than
        // assuming -- Android's own documentation isn't fully consistent on whether that needs
        // READ_EXTERNAL_STORAGE or the Storage Access Framework instead.
        // -----------------------------------------------------------------------

        private static final String CONFIG_RELATIVE_PATH = "Download/ILLIXR/";
        private static final String CONFIG_DISPLAY_NAME  = "network_configs.json";

        private boolean hasStoragePermission() {
            boolean read = activity_.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
            boolean write = activity_.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
            return read && write;
        }

        /** Looks up the existing saved-configs entry's content Uri, or null if none exists yet. */
        private static Uri findExistingConfigUri(ContentResolver resolver, Uri collection) {
            String   selection     = MediaStore.Downloads.DISPLAY_NAME + "=? AND " + MediaStore.Downloads.RELATIVE_PATH + "=?";
            String[] selectionArgs = {CONFIG_DISPLAY_NAME, CONFIG_RELATIVE_PATH};
            try (Cursor cursor = resolver.query(collection, new String[] {MediaStore.Downloads._ID},
                selection, selectionArgs, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    long id = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.Downloads._ID));
                    return ContentUris.withAppendedId(collection, id);
                }
            }
            return null;
        }

        private List<JSONObject> loadSavedConfigs() {
            List<JSONObject> configs = new ArrayList<>();
            if (!hasStoragePermission()) {
                return configs;
            }

            ContentResolver resolver  = activity_.getContentResolver();
            Uri              collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI;
            Uri              fileUri    = findExistingConfigUri(resolver, collection);
            if (fileUri == null) {
                return configs; // no saved file yet
            }

            try (InputStream in = resolver.openInputStream(fileUri)) {
                if (in == null) {
                    return configs;
                }
                JSONObject root  = new JSONObject(readAllUtf8(in));
                JSONArray  array = root.optJSONArray(KEY_CONFIGS);
                if (array != null) {
                    for (int i = 0; i < array.length(); i++) {
                        configs.add(array.getJSONObject(i));
                    }
                }
            } catch (IOException | JSONException e) {
                Log.w(TAG, "Failed to read saved configs from MediaStore", e);
            }
            return configs;
        }

        private void writeSavedConfigs(List<JSONObject> configs) {
            if (!hasStoragePermission()) {
                return;
            }

            String json;
            try {
                JSONArray array = new JSONArray();
                for (JSONObject cfg : configs) {
                    array.put(cfg);
                }
                JSONObject root = new JSONObject();
                root.put(KEY_CONFIGS, array);
                json = root.toString(2);
            } catch (JSONException e) {
                Log.w(TAG, "Failed to build config JSON", e);
                return;
            }

            ContentResolver resolver   = activity_.getContentResolver();
            Uri              collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI;

            try {
                Uri targetUri = findExistingConfigUri(resolver, collection);
                if (targetUri == null) {
                    ContentValues values = new ContentValues();
                    values.put(MediaStore.Downloads.DISPLAY_NAME, CONFIG_DISPLAY_NAME);
                    values.put(MediaStore.Downloads.MIME_TYPE, "application/json");
                    values.put(MediaStore.Downloads.RELATIVE_PATH, CONFIG_RELATIVE_PATH);
                    targetUri = resolver.insert(collection, values);
                    if (targetUri == null) {
                        Log.w(TAG, "MediaStore insert() returned null for " + CONFIG_DISPLAY_NAME);
                        return;
                    }
                }
                // "wt": truncate-write, so re-saving overwrites the existing entry rather than
                // appending to or interleaving with its previous contents.
                try (OutputStream out = resolver.openOutputStream(targetUri, "wt")) {
                    if (out == null) {
                        Log.w(TAG, "openOutputStream() returned null for " + targetUri);
                        return;
                    }
                    out.write(json.getBytes(StandardCharsets.UTF_8));
                }
            } catch (IOException e) {
                Log.w(TAG, "Failed to write saved configs to MediaStore", e);
            }
        }

        private static String readAllUtf8(InputStream in) throws IOException {
            try (ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[4096];
                int    n;
                while ((n = in.read(buffer)) != -1) {
                    out.write(buffer, 0, n);
                }
                return out.toString("UTF-8");
            }
        }
    }

    /**
     * Minimal black-background, white-text status display, rendered off-screen to a {@link Bitmap}
     * the same way {@link NetworkConfigPanel} is. Shown by {@code oxr_interface} in place of the
     * (otherwise blank/frozen) eye buffers until the first valid frame ever arrives from the
     * network -- see {@code oxr_interface::render_connection_log_into_eyes()}.
     *
     * <p>Deliberately built from a single plain, non-editable {@link TextView} -- no
     * {@code EditText}, {@code Button}, or {@code ScrollView} -- for the same reason as
     * {@code NetworkConfigPanel}: this is constructed and driven entirely from the render thread,
     * which never calls {@code Looper.prepare()}, and those widgets can lazily create a
     * {@code Handler} tied to the constructing thread's Looper (cursor blink, long-press timers,
     * fling/over-scroll physics) that would crash without one.
     *
     * <p>Text simply clips at the bottom if it doesn't fit rather than scrolling -- the native side
     * already caps what it sends to the last ~40 lines, so this is expected to rarely matter in
     * practice, and a real scrolling view isn't worth the Handler risk for a screen that's only up
     * for a few seconds at most.
     */
    public static class LogDisplayPanel {

        private final int width_px_;
        private final int height_px_;

        private final TextView text_view_;
        private final Bitmap   bitmap_;
        private final Canvas   canvas_;

        private boolean dirty_ = true;

        /**
         * @param activity used as the Context for the underlying TextView
         * @param widthPx  must match the eye swapchains' width exactly -- the native side uploads
         *                 this bitmap's raw pixels directly into those images with no scaling
         * @param heightPx must match the eye swapchains' height exactly, for the same reason
         */
        public LogDisplayPanel(Activity activity, int widthPx, int heightPx) {
            // Defensive, as in NetworkConfigPanel: costs nothing, forecloses an obscure crash if
            // some framework code path unexpectedly wants a Handler despite the plain-TextView-only
            // design above.
            if (Looper.myLooper() == null) {
                Looper.prepare();
            }

            width_px_  = widthPx;
            height_px_ = heightPx;

            bitmap_ = Bitmap.createBitmap(width_px_, height_px_, Bitmap.Config.ARGB_8888);
            canvas_ = new Canvas(bitmap_);

            text_view_ = new TextView(activity);
            text_view_.setTextColor(Color.WHITE);
            text_view_.setTextSize(28);
            text_view_.setGravity(Gravity.TOP | Gravity.START);
            int pad = Math.round(width_px_ * 0.04f);
            text_view_.setPadding(pad, pad, pad, pad);
            // Normally a view's LayoutParams come from being added to a parent (as
            // NetworkConfigPanel's widgets are, via addView()); this one is never attached to any
            // parent at all, so it needs one set explicitly, or TextView.setText()'s internal
            // checkForRelayout() crashes with a NullPointerException reading LayoutParams.width.
            text_view_.setLayoutParams(new ViewGroup.LayoutParams(width_px_, height_px_));

            layoutTextView();
        }

        /**
         * Replaces the displayed text. Safe to call repeatedly with unchanged text -- render()
         * separately tracks whether an actual redraw is needed, so redundant calls here don't cause
         * redundant bitmap redraws or texture uploads on the caller's side.
         */
        public void setLogText(String text) {
            text_view_.setText(text);
            layoutTextView(); // re-wrap for the new content
            dirty_ = true;
        }

        private void layoutTextView() {
            int wSpec = View.MeasureSpec.makeMeasureSpec(width_px_, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(height_px_, View.MeasureSpec.EXACTLY);
            text_view_.measure(wSpec, hSpec);
            text_view_.layout(0, 0, width_px_, height_px_);
        }

        /**
         * Redraws the backing bitmap if the text changed since the last call.
         *
         * @return true if a redraw happened (caller should re-upload the texture), false if the
         *         bitmap is unchanged since the last call.
         */
        public boolean render() {
            if (!dirty_) {
                return false;
            }
            canvas_.drawColor(Color.BLACK);
            text_view_.draw(canvas_);
            dirty_ = false;
            return true;
        }

        public Bitmap getBitmap() {
            return bitmap_;
        }
    }
}
