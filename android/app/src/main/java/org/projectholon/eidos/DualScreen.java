package org.projectholon.eidos;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.graphics.Color;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

/*
 * The second display of a dual-screen handheld (the AYN Thor, the Ayaneo
 * Flip DS, ...): a Presentation there holding one SurfaceView, whose Surface
 * goes to the game (host/dual.c).  The game draws the second DS screen into
 * it and reads fingers on it as the stylus (engine/platform/plat_dual.h).
 *
 * With no second display nothing happens and the game keeps its own
 * layouts.  A display that comes or goes while the game runs is followed.
 */
final class DualScreen implements DisplayManager.DisplayListener {

    private static final String TAG = "eidos";

    static native void nativeDualSurface(Object surface, int w, int h);
    static native void nativeDualSurfaceGone();
    static native void nativeDualTouch(int down, int x, int y);

    private final Activity activity;
    private final DisplayManager displays;
    private Screen shown;

    /* the shown Presentation, for touches that reach the game's own window
     * (EidosActivity) but belong to the second display */
    private static volatile Screen current;

    /*
     * A touch the game's window was given: 1 when it is the second
     * display's and went to the game as the stylus.  Some handhelds (the
     * AYN Thor among them) can deliver the bottom panel's touches to the
     * top display's window, marked with the second display's id.
     */
    static boolean fromMainWindow(MotionEvent e, int ownDisplay) {
        Screen sc = current;
        int id = displayOf(e);

        if (sc == null) {
            return false;
        }
        sc.report("game window", e, id);
        if (id < 0 || id == ownDisplay || id != sc.getDisplay().getDisplayId()) {
            return false;
        }
        sc.forward(e, true);
        return true;
    }

    static int displayOf(MotionEvent e) {
        /* InputEvent.getDisplayId, Android 10 on; reached by name so the
         * build does not depend on its visibility in the SDK */
        try {
            Object id = MotionEvent.class.getMethod("getDisplayId").invoke(e);
            return id instanceof Integer ? (Integer) id : -1;
        } catch (Throwable t) {
            return -1;
        }
    }

    DualScreen(Activity activity) {
        this.activity = activity;
        this.displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    void start() {
        if (displays == null) {
            return;
        }
        displays.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
        update();
    }

    void stop() {
        if (displays != null) {
            displays.unregisterDisplayListener(this);
        }
        hide();
    }

    /* The display for the second screen: one the system offers for
     * presentations, else any display that is not the activity's own. */
    private Display pick() {
        int own = Display.DEFAULT_DISPLAY;

        if (Build.VERSION.SDK_INT >= 30 && activity.getDisplay() != null) {
            own = activity.getDisplay().getDisplayId();
        }
        for (Display d : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (d.getDisplayId() != own && d.getState() != Display.STATE_OFF) {
                return d;
            }
        }
        for (Display d : displays.getDisplays()) {
            if (d.getDisplayId() != own && d.getState() != Display.STATE_OFF) {
                return d;
            }
        }
        return null;
    }

    void update() {
        Display d = pick();

        if (shown != null && (d == null || shown.getDisplay().getDisplayId() != d.getDisplayId())) {
            hide();
        }
        if (d == null || shown != null || activity.isFinishing()) {
            return;
        }
        try {
            shown = new Screen(activity, d);
            shown.show();
            current = shown;
            Log.i(TAG, "dual screen: second display " + d.getDisplayId() + " (" + d.getName() + ")");
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException e) {
            Log.w(TAG, "dual screen: cannot show on display " + d.getDisplayId(), e);
            shown = null;
        }
    }

    private void hide() {
        if (shown != null) {
            current = null;
            shown.dismiss();            /* its surfaceDestroyed tells the game */
            shown = null;
        }
    }

    @Override public void onDisplayAdded(int id) { update(); }
    @Override public void onDisplayRemoved(int id) { update(); }
    @Override public void onDisplayChanged(int id) { update(); }

    /* The Presentation: one SurfaceView, all of the display, and (for now)
     * a small label naming where touches arrive, to tell how a handheld
     * routes its second panel. */
    private static final class Screen extends Presentation implements SurfaceHolder.Callback {

        private SurfaceView view;
        private TextView label;
        private final Handler ui = new Handler(Looper.getMainLooper());
        private final Runnable hideLabel = () -> { if (label != null) label.setVisibility(View.GONE); };

        Screen(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle state) {
            super.onCreate(state);
            FrameLayout root = new FrameLayout(getContext());

            view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            root.addView(view, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
            label = new TextView(getContext());
            label.setTextColor(Color.WHITE);
            label.setBackgroundColor(0xB0000000);
            label.setTextSize(14f);
            label.setPadding(16, 8, 16, 8);
            label.setVisibility(View.GONE);
            root.addView(label, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP | Gravity.START));
            setContentView(root);
            /* touches only: the buttons and sticks stay with the game's
             * window, which a focused window here would take them from */
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            immersive(root);
        }

        /* every touch on this window, whichever view it lands on */
        @Override
        public boolean dispatchTouchEvent(MotionEvent e) {
            report("bottom window", e, displayOf(e));
            forward(e, false);
            return true;
        }

        /* where a touch arrived, on the label for a few seconds */
        void report(String where, MotionEvent e, int display) {
            if (e.getActionMasked() != MotionEvent.ACTION_DOWN) {
                return;
            }
            InputDevice dev = e.getDevice();
            final String text = "touch: " + where + ", display " + display
                    + ", " + (dev != null ? dev.getName() : "device " + e.getDeviceId());

            Log.i(TAG, "dual screen: " + text);
            ui.post(() -> {
                if (label != null) {
                    label.setText(text);
                    label.setVisibility(View.VISIBLE);
                    ui.removeCallbacks(hideLabel);
                    ui.postDelayed(hideLabel, 3000);
                }
            });
        }

        /*
         * The touch to the game as the stylus.  On this window the
         * coordinates are the window's; from the game's window they are
         * taken raw, against this display's size.
         */
        void forward(MotionEvent e, boolean raw) {
            float w, h, x, y;

            if (raw || view == null || view.getWidth() <= 0) {
                android.graphics.Point size = new android.graphics.Point();
                getDisplay().getRealSize(size);
                w = size.x;
                h = size.y;
                x = e.getRawX();
                y = e.getRawY();
            } else {
                w = view.getWidth();
                h = view.getHeight();
                x = e.getX(0);
                y = e.getY(0);
            }
            w = Math.max(1f, w);
            h = Math.max(1f, h);
            int nx = (int) (x * 65535f / w), ny = (int) (y * 65535f / h);

            switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_POINTER_DOWN:
                nativeDualTouch(1, nx, ny);
                break;
            case MotionEvent.ACTION_POINTER_UP:
                nativeDualTouch(e.getActionIndex() != 0 ? 1 : 0, nx, ny);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                nativeDualTouch(0, nx, ny);
                break;
            default:
                break;
            }
        }

        /* no status or navigation bar over the picture */
        @SuppressWarnings("deprecation")
        private void immersive(View v) {
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
                WindowInsetsController c = getWindow().getInsetsController();
                if (c != null) {
                    c.hide(WindowInsets.Type.systemBars());
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                v.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
            }
        }

        @Override public void surfaceCreated(SurfaceHolder holder) { }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
            nativeDualSurface(holder.getSurface(), w, h);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            nativeDualSurfaceGone();
        }
    }
}
