package org.projectholon.eidos;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/*
 * eidos on Android: SDL's activity, with one change to what the game is
 * told about the screen's edges (SDL_GetWindowSafeArea).
 *
 * SDL counts the system's gesture areas (the swipe down from the top, the
 * home gesture at the bottom, the back gestures at the sides) as unsafe.
 * In a fullscreen game those are only places a swipe starts; nothing
 * covers the picture there, so the one-screen layout would leave empty
 * bands for nothing.  The game is told only about what does cover it: the
 * display's cutouts (a camera hole) and the system bars while they show.
 */
public class EidosActivity extends SDLActivity {

    static class EidosSurface extends SDLSurface {
        EidosSurface(Context context) {
            super(context);
        }

        @Override
        public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
            WindowInsets out = super.onApplyWindowInsets(v, insets);

            if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                Insets in = insets.getInsets(WindowInsets.Type.systemBars() |
                                             WindowInsets.Type.displayCutout());

                SDLActivity.onNativeInsetsChanged(in.left, in.right, in.top, in.bottom);
            }
            return out;
        }
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new EidosSurface(context);
    }
}
