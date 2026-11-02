/*
 * host/pick_ios.m -- the ROM dialog on iOS (host/pick_mobile.c).
 *
 * UIDocumentPickerViewController over the game's view, opened as a copy:
 * iOS puts the chosen file in the app's own tmp/ folder, where the
 * installer opens it like any file and needs no security-scoped access.
 * The game runs on the main thread, so the call turns the main run loop
 * until the player has chosen or cancelled.
 */

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdio.h>

#include "picker.h"

@interface EidosRomPicker : NSObject <UIDocumentPickerDelegate>
@property (nonatomic) int done;
@property (nonatomic, copy) NSString *path;
@end

@implementation EidosRomPicker
- (void)documentPicker:(UIDocumentPickerViewController *)picker didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    (void)picker;
    self.path = urls.count > 0 ? urls[0].path : nil;
    self.done = 1;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)picker
{
    (void)picker;
    self.done = 1;
}
@end

static UIViewController *top_controller(void)
{
    UIWindow *window = nil;
    UIViewController *vc;

    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:UIWindowScene.class]) {
            for (UIWindow *w in ((UIWindowScene *)scene).windows) {
                if (w.isKeyWindow || window == nil) {
                    window = w;
                }
            }
        }
    }
    vc = window.rootViewController;
    while (vc.presentedViewController != nil) {
        vc = vc.presentedViewController;
    }
    return vc;
}

int host_pick_rom_ios(const char *title, char *out, size_t n)
{
    @autoreleasepool {
        EidosRomPicker *delegate = [EidosRomPicker new];
        UIDocumentPickerViewController *picker;
        UIViewController *vc = top_controller();

        (void)title;
        if (vc == nil) {
            snprintf(out, n, "%s", "There is no window to show the file list over.");
            return BW_PICK_UNAVAILABLE;
        }
        picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeItem] asCopy:YES];
        picker.delegate = delegate;
        picker.allowsMultipleSelection = NO;
        [vc presentViewController:picker animated:YES completion:nil];
        while (!delegate.done) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
        }
        if (delegate.path == nil) {
            return BW_PICK_CANCELLED;
        }
        snprintf(out, n, "%s", delegate.path.fileSystemRepresentation);
        return BW_PICK_OK;
    }
}
