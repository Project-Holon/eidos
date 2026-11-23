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
import android.graphics.PixelFormat;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

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
            Log.i(TAG, "dual screen: second display " + d.getDisplayId() + " (" + d.getName() + ")");
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException e) {
            Log.w(TAG, "dual screen: cannot show on display " + d.getDisplayId(), e);
            shown = null;
        }
    }

    private void hide() {
        if (shown != null) {
            shown.dismiss();            /* its surfaceDestroyed tells the game */
            shown = null;
        }
    }

    @Override public void onDisplayAdded(int id) { update(); }
    @Override public void onDisplayRemoved(int id) { update(); }
    @Override public void onDisplayChanged(int id) { update(); }

    /* The Presentation: one SurfaceView, all of the display. */
    private static final class Screen extends Presentation implements SurfaceHolder.Callback {

        Screen(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle state) {
            super.onCreate(state);
            FrameLayout root = new FrameLayout(getContext());
            SurfaceView view = new SurfaceView(getContext());
            android.graphics.Point size = new android.graphics.Point();
            int w, h;

            /* the DS screen's shape (256 x 192), as large as fits, centred
             * on black: the game copies its picture in at this size */
            getDisplay().getRealSize(size);
            if ((long) size.x * 192 <= (long) size.y * 256) {
                w = size.x;
                h = size.x * 192 / 256;
            } else {
                h = size.y;
                w = size.y * 256 / 192;
            }
            root.setBackgroundColor(Color.BLACK);
            view.getHolder().setFormat(PixelFormat.RGBA_8888);
            view.getHolder().addCallback(this);
            view.setOnTouchListener(Screen::touch);
            root.addView(view, new FrameLayout.LayoutParams(w, h, Gravity.CENTER));
            setContentView(root);
            /* touches only: the buttons and sticks stay with the game's
             * window, which a focused window here would take them from */
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            immersive(view);
        }

        /* no status or navigation bar over the picture */
        @SuppressWarnings("deprecation")
        private void immersive(View view) {
            if (Build.VERSION.SDK_INT >= 30) {
                getWindow().setDecorFitsSystemWindows(false);
                WindowInsetsController c = getWindow().getInsetsController();
                if (c != null) {
                    c.hide(WindowInsets.Type.systemBars());
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                view.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
            }
        }

        private static boolean touch(View v, MotionEvent e) {
            int w = Math.max(1, v.getWidth()), h = Math.max(1, v.getHeight());
            int x = (int) (e.getX(0) * 65535f / w), y = (int) (e.getY(0) * 65535f / h);

            switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_POINTER_DOWN:
                nativeDualTouch(1, x, y);
                break;
            case MotionEvent.ACTION_POINTER_UP:
                if (e.getActionIndex() != 0) {
                    nativeDualTouch(1, x, y);
                    break;
                }
                /* the first finger lifted: the stylus goes up */
                nativeDualTouch(0, x, y);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                nativeDualTouch(0, x, y);
                break;
            default:
                break;
            }
            return true;
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
