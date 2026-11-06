// The launcher's save editor on macOS (Debug -> Save editor): tools/save_editor's web UI in a window of the app, as the
// old macOS launcher (tools/launcher/Launcher.swift) showed it, instead of in the browser. The server runs as a child
// process on a free localhost port (serve --port 0 --write --exit-with-parent); the window is a WKWebView. The page's
// alert()/confirm() become sheets, its file input an open panel, and closing the window with unsaved changes asks
// first (the page's window.hasUnsavedChanges). The server stops with the window, with the launcher
// (Port_SaveEditorStop) and, through --exit-with-parent, if the game ends some other way.
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include <string>

@interface PortSaveEditor : NSObject <NSWindowDelegate, WKNavigationDelegate, WKUIDelegate>
@property(nonatomic, strong) NSTask* server;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* web;
@property(nonatomic, copy) NSString* savePath;
@property(nonatomic, copy) NSString* logPath;
@property(nonatomic) BOOL closeConfirmed;
@end

static PortSaveEditor* sEditor = nil;

@implementation PortSaveEditor

- (BOOL)startIn:(NSString*)dir save:(NSString*)save error:(std::string*)error {
    self.savePath = save;
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/env"];
    task.arguments = @[ @"python3", @"-m", @"stadium_save", @"serve", save, @"--port", @"0", @"--write",
                        @"--exit-with-parent" ];
    task.currentDirectoryURL = [NSURL fileURLWithPath:dir];
    NSMutableDictionary* env = [[[NSProcessInfo processInfo] environment] mutableCopy];
    env[@"PYTHONDONTWRITEBYTECODE"] = @"1";
    env[@"PYTHONPATH"] = dir;
    // started from Finder the app gets only /usr/bin:/bin:/usr/sbin:/sbin: also where Homebrew and python.org put python3
    NSString* path = env[@"PATH"] ?: @"/usr/bin:/bin";
    for (NSString* extra in @[ @"/opt/homebrew/bin", @"/usr/local/bin",
                               @"/Library/Frameworks/Python.framework/Versions/Current/bin" ]) {
        if (![[path componentsSeparatedByString:@":"] containsObject:extra]) {
            path = [path stringByAppendingFormat:@":%@", extra];
        }
    }
    env[@"PATH"] = path;
    task.environment = env;
    // the log next to the save (the app's own folder may be read-only)
    self.logPath = [[save stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"save_editor.log"];
    [[NSFileManager defaultManager] createFileAtPath:self.logPath contents:nil attributes:nil];
    task.standardError = [NSFileHandle fileHandleForWritingAtPath:self.logPath] ?: [NSFileHandle fileHandleWithNullDevice];
    NSPipe* out = [NSPipe pipe];
    task.standardOutput = out;
    __block NSMutableString* buffer = [NSMutableString string];
    __weak PortSaveEditor* weakSelf = self;
    out.fileHandleForReading.readabilityHandler = ^(NSFileHandle* h) {
        NSData* chunk = h.availableData;
        if (chunk.length == 0) {
            h.readabilityHandler = nil;
            return;
        }
        [buffer appendString:[[NSString alloc] initWithData:chunk encoding:NSUTF8StringEncoding] ?: @""];
        NSRange r = [buffer rangeOfString:@"127\\.0\\.0\\.1:[0-9]+" options:NSRegularExpressionSearch];
        if (r.location != NSNotFound) {
            h.readabilityHandler = nil;
            NSInteger port = [[[buffer substringWithRange:r] componentsSeparatedByString:@":"].lastObject integerValue];
            dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf showPort:port]; });
        }
    };
    task.terminationHandler = ^(NSTask* t) {
        dispatch_async(dispatch_get_main_queue(), ^{
            PortSaveEditor* s = weakSelf;
            if (s == nil || s.server != t) return;
            s.server = nil;
            if (s.web == nil) { // ended before the page came up
                NSAlert* a = [[NSAlert alloc] init];
                a.messageText = @"Save editor";
                a.informativeText = [NSString stringWithFormat:@"The save editor could not start (it needs python3).\n"
                                                                @"Details: %@", s.logPath];
                [a runModal];
                [s.window close];
            }
        });
    };
    NSError* err = nil;
    if (![task launchAndReturnError:&err]) {
        if (error) *error = "Needs python3";
        return NO;
    }
    self.server = task;
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1180, 800)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.title = [@"Save editor — " stringByAppendingString:save.lastPathComponent];
    w.representedURL = [NSURL fileURLWithPath:save];
    w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    w.contentMinSize = NSMakeSize(820, 560);
    w.releasedWhenClosed = NO;
    w.delegate = self;
    [w center];
    [w makeKeyAndOrderFront:nil];
    self.window = w;
    return YES;
}

- (void)showPort:(NSInteger)port {
    if (self.window == nil || self.web != nil) return;
    WKWebView* v = [[WKWebView alloc] initWithFrame:self.window.contentView.bounds
                                      configuration:[[WKWebViewConfiguration alloc] init]];
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    v.navigationDelegate = self;
    v.UIDelegate = self;
    self.window.contentView = v;
    self.web = v;
    NSString* url = [NSString stringWithFormat:@"http://127.0.0.1:%ld/", (long)port];
    [v loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:url]]];
}

- (void)stopServer {
    NSTask* t = self.server;
    self.server = nil;
    if (t != nil && t.running) {
        [t terminate];
        [t waitUntilExit];
    }
}

// test hook: PORT_SAVE_EDITOR_SNAPSHOT=<png> saves the page's picture once it has loaded
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    const char* shot = getenv("PORT_SAVE_EDITOR_SNAPSHOT");
    if (shot == nullptr) return;
    NSString* path = [NSString stringWithUTF8String:shot];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [webView takeSnapshotWithConfiguration:nil
                             completionHandler:^(NSImage* image, NSError* error) {
                                 NSData* tiff = image.TIFFRepresentation;
                                 NSBitmapImageRep* rep = tiff ? [NSBitmapImageRep imageRepWithData:tiff] : nil;
                                 [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path
                                                                                                         atomically:YES];
                                 fprintf(stderr, "[save editor] snapshot %s\n", rep ? "written" : "failed");
                             }];
    });
}

// unsaved changes: ask before closing (the page sets window.hasUnsavedChanges)
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (self.web == nil || self.closeConfirmed) return YES;
    [self.web evaluateJavaScript:@"window.hasUnsavedChanges ? window.hasUnsavedChanges() : false"
               completionHandler:^(id result, NSError* error) {
                   if ([result isKindOfClass:[NSNumber class]] && [result boolValue]) {
                       NSAlert* a = [[NSAlert alloc] init];
                       a.messageText = @"Close the save editor without saving?";
                       a.informativeText = [NSString stringWithFormat:@"Your changes to %@ have not been saved.",
                                                                      self.savePath.lastPathComponent];
                       [a addButtonWithTitle:@"Cancel"];
                       [a addButtonWithTitle:@"Close without saving"];
                       if ([a runModal] != NSAlertSecondButtonReturn) return;
                   }
                   self.closeConfirmed = YES;
                   [sender close];
               }];
    return NO;
}

- (void)windowWillClose:(NSNotification*)notification {
    [self stopServer];
    self.web = nil;
    self.window = nil;
    if (sEditor == self) sEditor = nil;
}

// the page's alert() / confirm(): WKWebView shows nothing (and confirm() answers false) without these
- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)(void))completionHandler {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"Save editor";
    a.informativeText = message;
    [a beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) { completionHandler(); }];
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                        initiatedByFrame:(WKFrameInfo*)frame
                       completionHandler:(void (^)(BOOL))completionHandler {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = message;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    [a beginSheetModalForWindow:self.window
              completionHandler:^(NSModalResponse r) { completionHandler(r == NSAlertFirstButtonReturn); }];
}

// the page's "Open save…" file input
- (void)webView:(WKWebView*)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
              initiatedByFrame:(WKFrameInfo*)frame
             completionHandler:(void (^)(NSArray<NSURL*>*))completionHandler {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.directoryURL = [NSURL fileURLWithPath:self.savePath.stringByDeletingLastPathComponent];
    p.allowsMultipleSelection = NO;
    completionHandler([p runModal] == NSModalResponseOK ? p.URLs : nil);
}

@end

// Opens the editor window for this save (or brings the open one forward). False with *status set when it could not
// start; the window shows its own message when the server ends before the page came up.
bool Port_SaveEditorOpen(const std::string& dir, const std::string& save, std::string* status) {
    @autoreleasepool {
        if (sEditor != nil && sEditor.window != nil) {
            [sEditor.window makeKeyAndOrderFront:nil];
            if (status) *status = "Save editor open";
            return true;
        }
        PortSaveEditor* e = [[PortSaveEditor alloc] init];
        if (![e startIn:[NSString stringWithUTF8String:dir.c_str()] save:[NSString stringWithUTF8String:save.c_str()]
                  error:status]) {
            return false;
        }
        sEditor = e;
        if (status) *status = "Save editor open";
        return true;
    }
}

// Closes the window and stops the server (the launcher is closing: the game is about to load the save).
void Port_SaveEditorStop() {
    @autoreleasepool {
        PortSaveEditor* e = sEditor;
        sEditor = nil;
        if (e == nil) return;
        [e stopServer];
        e.closeConfirmed = YES;
        [e.window close];
    }
}
