package com.skirf.skypoint;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.hardware.GeomagneticField;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * SkyPoint for Android: a thin native shell around the web sky engine in assets/www.
 * Java owns the hardware (rotation vector, GNSS, compass declination, camera permission, vibration,
 * network); the page owns everything you see. They talk through window.SkyPointNative.
 */
public class MainActivity extends Activity implements SensorEventListener, LocationListener {

    private static final String ORIGIN = "https://appassets.androidplatform.net/";
    private static final int REQ_LOCATION = 1, REQ_CAMERA = 2;

    private WebView web;
    private SensorManager sensors;
    private Sensor rotation;
    private LocationManager locations;
    private Vibrator vibrator;
    private final Handler ui = new Handler(Looper.getMainLooper());

    // written on sensor / location threads, read from the JS bridge thread
    private volatile String orientation = "";
    private volatile int sensorAccuracy = 3;
    private volatile Location best;
    private volatile String gnssJson = "{}";
    private PermissionRequest pendingCamera;

    private final GnssStatus.Callback gnssCallback = new GnssStatus.Callback() {
        @Override public void onSatelliteStatusChanged(GnssStatus s) {
            StringBuilder b = new StringBuilder("{\"sats\":[");
            for (int i = 0; i < s.getSatelliteCount(); i++) {
                if (i > 0) b.append(',');
                b.append('[').append(Math.round(s.getAzimuthDegrees(i))).append(',')
                 .append(Math.round(s.getElevationDegrees(i))).append(',')
                 .append(Math.round(s.getCn0DbHz(i))).append(',')
                 .append(s.usedInFix(i) ? 1 : 0).append(',')
                 .append(s.getConstellationType(i)).append(']');
            }
            gnssJson = b.append("]}").toString();
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        sensors = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        rotation = sensors.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        locations = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);

        applyRotation(getSharedPreferences("skypoint", MODE_PRIVATE).getString("rotation", "portrait"));

        web = new WebView(this);
        web.setBackgroundColor(0xFF000000);
        WebSettings ws = web.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setMediaPlaybackRequiresUserGesture(false);
        ws.setAllowFileAccess(false);
        ws.setAllowContentAccess(false);
        ws.setGeolocationEnabled(false);   // location comes from the bridge instead
        web.setWebViewClient(new WebViewClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) {
                String u = r.getUrl().toString();
                if (!u.startsWith(ORIGIN)) return null;
                String path = u.substring(ORIGIN.length());
                int q = path.indexOf('?'); if (q >= 0) path = path.substring(0, q);
                int h = path.indexOf('#'); if (h >= 0) path = path.substring(0, h);
                if (path.isEmpty()) path = "index.html";
                if (path.contains("..")) return notFound();
                try {
                    InputStream in = getAssets().open("www/" + path);
                    WebResourceResponse res = new WebResourceResponse(mime(path), "utf-8", in);
                    Map<String, String> hd = new HashMap<>();
                    hd.put("Cache-Control", "no-cache");
                    res.setResponseHeaders(hd);
                    return res;
                } catch (Exception e) {
                    return notFound();
                }
            }
            @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                return !r.getUrl().toString().startsWith(ORIGIN);   // stay inside the app
            }
            @Override public void onPageFinished(WebView v, String url) { pushInsets(); }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public void onPermissionRequest(final PermissionRequest req) {
                ui.post(new Runnable() { @Override public void run() {
                    boolean wantsCam = false;
                    for (String r : req.getResources()) if (PermissionRequest.RESOURCE_VIDEO_CAPTURE.equals(r)) wantsCam = true;
                    if (!wantsCam) { req.deny(); return; }
                    if (hasPerm(Manifest.permission.CAMERA)) req.grant(new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE});
                    else { pendingCamera = req; requestPermissions(new String[]{Manifest.permission.CAMERA}, REQ_CAMERA); }
                } });
            }
        });
        web.addJavascriptInterface(new Bridge(), "SkyPointNative");
        setContentView(web);
        immersive();
        web.loadUrl(ORIGIN + "index.html");
    }

    private static WebResourceResponse notFound() {
        WebResourceResponse r = new WebResourceResponse("text/plain", "utf-8", null);
        r.setStatusCodeAndReasonPhrase(404, "Not Found");
        return r;
    }

    private static String mime(String p) {
        p = p.toLowerCase(Locale.ROOT);
        if (p.endsWith(".html")) return "text/html";
        if (p.endsWith(".js")) return "application/javascript";
        if (p.endsWith(".css")) return "text/css";
        if (p.endsWith(".woff2")) return "font/woff2";
        if (p.endsWith(".png")) return "image/png";
        if (p.endsWith(".webp")) return "image/webp";
        if (p.endsWith(".svg")) return "image/svg+xml";
        if (p.endsWith(".json")) return "application/json";
        return "application/octet-stream";
    }

    /** portrait (default), auto (follows the phone, honours the system rotation lock), landscape (either way round). */
    private void applyRotation(String mode) {
        int o = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        if ("auto".equals(mode)) o = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
        else if ("landscape".equals(mode)) o = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        setRequestedOrientation(o);
    }

    @SuppressWarnings("deprecation")
    private int screenRotation() {
        android.view.Display d = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
        int r = d != null ? d.getRotation() : 0;
        return r == android.view.Surface.ROTATION_90 ? 90 : r == android.view.Surface.ROTATION_180 ? 180 : r == android.view.Surface.ROTATION_270 ? 270 : 0;
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration c) {
        super.onConfigurationChanged(c);
        immersive();
        ui.postDelayed(new Runnable() { @Override public void run() { pushInsets(); } }, 200);
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /** Tell the page how much room a camera cutout takes at the top, in CSS pixels. */
    private void pushInsets() {
        int top = 0, left = 0, right = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            WindowInsets in = web.getRootWindowInsets();
            if (in != null) {
                DisplayCutout dc = in.getDisplayCutout();
                if (dc != null) { top = dc.getSafeInsetTop(); left = dc.getSafeInsetLeft(); right = dc.getSafeInsetRight(); }
            }
        }
        float d = getResources().getDisplayMetrics().density;
        int t = Math.round(top / d), l = Math.round(left / d), r = Math.round(right / d);
        js("window.SP_INSET_TOP=" + t + ";var ds=document.documentElement.style;ds.setProperty('--inset-top','" + t + "px');"
                + "ds.setProperty('--inset-left','" + l + "px');ds.setProperty('--inset-right','" + r + "px');");
    }

    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); }

    @Override
    protected void onResume() {
        super.onResume();
        web.onResume();
        if (rotation != null) sensors.registerListener(this, rotation, SensorManager.SENSOR_DELAY_GAME);
        startLocation();
    }

    @Override
    protected void onPause() {
        sensors.unregisterListener(this);
        stopLocation();
        web.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        web.destroy();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        web.evaluateJavascript("window.SP_back?SP_back():false", new android.webkit.ValueCallback<String>() {
            @Override public void onReceiveValue(String v) { if (!"true".equals(v)) finish(); }
        });
    }

    /* ---------------------------------------------------------------- sensors */
    @Override
    public void onSensorChanged(SensorEvent e) {
        float x = e.values[0], y = e.values[1], z = e.values[2];
        float w = e.values.length > 3 ? e.values[3] : (float) Math.sqrt(Math.max(0, 1 - x * x - y * y - z * z));
        orientation = x + "," + y + "," + z + "," + w + "," + sensorAccuracy;
    }

    @Override public void onAccuracyChanged(Sensor s, int accuracy) { sensorAccuracy = accuracy; }

    /* ---------------------------------------------------------------- location */
    private boolean hasPerm(String p) { return checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED; }

    @SuppressWarnings("MissingPermission")
    private void startLocation() {
        if (!hasPerm(Manifest.permission.ACCESS_FINE_LOCATION) && !hasPerm(Manifest.permission.ACCESS_COARSE_LOCATION)) return;
        try {
            for (String p : new String[]{LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER}) {
                if (!locations.getAllProviders().contains(p)) continue;
                Location last = locations.getLastKnownLocation(p);
                if (last != null) consider(last);
                locations.requestLocationUpdates(p, 1000, 0, this, Looper.getMainLooper());
            }
            if (hasPerm(Manifest.permission.ACCESS_FINE_LOCATION)) locations.registerGnssStatusCallback(gnssCallback, ui);
        } catch (Exception ignored) { }
    }

    private void stopLocation() {
        try { locations.removeUpdates(this); locations.unregisterGnssStatusCallback(gnssCallback); } catch (Exception ignored) { }
    }

    private void consider(Location l) {
        Location b = best;
        // prefer a fresh GPS fix; accept anything newer than 2 minutes or more accurate
        if (b == null || l.getTime() - b.getTime() > 120000 || LocationManager.GPS_PROVIDER.equals(l.getProvider())
                || (l.hasAccuracy() && b.hasAccuracy() && l.getAccuracy() < b.getAccuracy())) best = l;
    }

    @Override public void onLocationChanged(Location l) { consider(l); }
    @Override public void onProviderEnabled(String p) { }
    @Override public void onProviderDisabled(String p) { }
    @Override @SuppressWarnings("deprecation") public void onStatusChanged(String p, int s, Bundle b) { }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        boolean ok = res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED;
        if (code == REQ_LOCATION) {
            if (ok) startLocation();
            js("window.SP_onPerm&&SP_onPerm('location'," + ok + ")");
        } else if (code == REQ_CAMERA) {
            if (pendingCamera != null) {
                if (ok) pendingCamera.grant(new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE}); else pendingCamera.deny();
                pendingCamera = null;
            }
            js("window.SP_onPerm&&SP_onPerm('camera'," + ok + ")");
        }
    }

    private void js(final String code) { ui.post(new Runnable() { @Override public void run() { web.evaluateJavascript(code, null); } }); }

    /* ---------------------------------------------------------------- bridge */
    private class Bridge {
        @JavascriptInterface public String orientation() { return orientation; }

        @JavascriptInterface public String location() {
            Location l = best;
            if (l == null) return "";
            try {
                JSONObject o = new JSONObject();
                o.put("lat", l.getLatitude()); o.put("lon", l.getLongitude());
                o.put("alt", l.hasAltitude() ? l.getAltitude() : 0);
                o.put("acc", l.hasAccuracy() ? l.getAccuracy() : 0);
                o.put("time", l.getTime()); o.put("provider", l.getProvider());
                return o.toString();
            } catch (Exception e) { return ""; }
        }

        @JavascriptInterface public String gnss() { return gnssJson; }

        @JavascriptInterface public double declination(double lat, double lon, double alt) {
            return new GeomagneticField((float) lat, (float) lon, (float) alt, System.currentTimeMillis()).getDeclination();
        }

        @JavascriptInterface public void vibrate(int ms) {
            if (vibrator == null || !vibrator.hasVibrator()) return;
            vibrator.vibrate(VibrationEffect.createOneShot(Math.max(5, Math.min(ms, 400)), VibrationEffect.DEFAULT_AMPLITUDE));
        }

        @JavascriptInterface public boolean hasCamera() { return hasPerm(Manifest.permission.CAMERA); }

        @JavascriptInterface public void requestCamera() {
            ui.post(new Runnable() { @Override public void run() { requestPermissions(new String[]{Manifest.permission.CAMERA}, REQ_CAMERA); } });
        }

        @JavascriptInterface public void requestLocation() {
            ui.post(new Runnable() { @Override public void run() {
                if (hasPerm(Manifest.permission.ACCESS_FINE_LOCATION)) startLocation();
                else requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION}, REQ_LOCATION);
            } });
        }

        @JavascriptInterface public int rotation() { return screenRotation(); }

        @JavascriptInterface public void setRotation(final String mode) {
            getSharedPreferences("skypoint", MODE_PRIVATE).edit().putString("rotation", mode).apply();
            ui.post(new Runnable() { @Override public void run() { applyRotation(mode); } });
        }

        /** Open an https link (Wikipedia, CelesTrak) in the user's browser. */
        @JavascriptInterface public void openUrl(final String url) {
            if (url == null || !url.startsWith("https://")) return;
            ui.post(new Runnable() { @Override public void run() {
                try {
                    startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(url)));
                } catch (Exception ignored) { }
            } });
        }

        @JavascriptInterface public String version() {
            try {
                PackageInfo p = getPackageManager().getPackageInfo(getPackageName(), 0);
                return p.versionName;
            } catch (Exception e) { return "?"; }
        }

        /** Fetch newline-separated HTTPS URLs on a worker thread; answers SP_onFetch(ok, text). */
        @JavascriptInterface public void fetchText(final String urls) {
            new Thread(new Runnable() { @Override public void run() {
                StringBuilder all = new StringBuilder();
                for (String u : urls.split("\n")) {
                    u = u.trim();
                    if (!u.startsWith("https://")) continue;
                    HttpURLConnection c = null;
                    try {
                        c = (HttpURLConnection) new URL(u).openConnection();
                        c.setConnectTimeout(8000); c.setReadTimeout(8000);
                        c.setRequestProperty("User-Agent", "SkyPoint-Android");
                        if (c.getResponseCode() == 200) {
                            InputStream in = c.getInputStream();
                            ByteArrayOutputStream out = new ByteArrayOutputStream();
                            byte[] buf = new byte[4096]; int n;
                            while ((n = in.read(buf)) > 0 && out.size() < 65536) out.write(buf, 0, n);
                            all.append(out.toString("UTF-8")).append('\n');
                        }
                    } catch (Exception ignored) {
                    } finally { if (c != null) c.disconnect(); }
                }
                final boolean ok = all.length() > 100;
                js("window.SP_onFetch&&SP_onFetch(" + ok + "," + JSONObject.quote(all.toString()) + ")");
            } }, "skypoint-fetch").start();
        }
    }
}
