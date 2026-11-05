/*
 * host/ios_view.m -- the home indicator on iOS.
 *
 * SDL's view controller either auto-hides the home indicator (and lets the
 * first swipe go home) or keeps it on screen, dimmed, and defers the swipe.
 * A game wants both, as iOS allows: the indicator hidden while playing, and
 * a swipe from the bottom edge first showing it, the second going home.
 * So the host gives SDL's controller those two answers before the window
 * is made.
 */

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

static BOOL hide_home(id self, SEL cmd)
{
    (void)self;
    (void)cmd;
    return YES;
}

static UIRectEdge defer_edges(id self, SEL cmd)
{
    (void)self;
    (void)cmd;
    return UIRectEdgeAll;
}

void host_ios_view_setup(void)
{
    Class vc = NSClassFromString(@"SDL_uikitviewcontroller");
    Method m;

    if (vc == Nil) {
        return;
    }
    if ((m = class_getInstanceMethod(vc, @selector(prefersHomeIndicatorAutoHidden))) != NULL) {
        method_setImplementation(m, (IMP)hide_home);
    }
    if ((m = class_getInstanceMethod(vc, @selector(preferredScreenEdgesDeferringSystemGestures))) != NULL) {
        method_setImplementation(m, (IMP)defer_edges);
    }
}

/* An iPad turns both ways: the one-screen layout fits a tall window too. */
int host_ios_is_pad(void)
{
    return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
}
