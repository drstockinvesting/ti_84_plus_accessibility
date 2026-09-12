/* TI-84 Plus emulator for macOS — Cocoa front end.
 *
 * The skin and keymap are the same PNG pair WabbitEmu uses (keymap pixel =
 * (0, group*16, bit*16), LCD area = pure red), so the accessible skin built in
 * ../ works here unchanged. Unlike WabbitEmu the window has no half-the-skin
 * minimum size, so it scales down to fit a laptop screen and up for magnification.
 */
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <CoreServices/CoreServices.h>
#include "ti84.h"
#include "keytable.h"

/* ---------------- themes ---------------- */

typedef struct { const char *name; uint8_t off[3], on[3]; const char *skin_key; } theme_t;

static const theme_t THEMES[] = {
    { "Classic",     { 158, 171, 136 }, {   0,   0,   0 }, "skinClassic" },
    { "Day",         { 255, 255, 255 }, {   0,   0,   0 }, "skinDay" },
    { "Night",       {   0,   0,   0 }, { 255, 255, 255 }, "skinNight" },
    { "Night Amber", {   0,   0,   0 }, { 255, 190,  60 }, "skinNight" },
};
#define NTHEMES ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

/* ---------------- globals ---------------- */

static calc_t calc;
static int rom_loaded = 0;

typedef struct { int g, b, frames; } queued_key_t;

@interface TIView : NSView
@property (nonatomic) CGImageRef skin;
@property (nonatomic) int themeIndex;
@property (nonatomic) BOOL screenOnly;
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) TIView *view;
@end

static void drawCalc(CGContextRef ctx, CGRect b, CGImageRef skin, int themeIndex, BOOL screenOnly);

static TIView *gView;
static AppDelegate *gApp;

/* keymap pixels, kept at native resolution for hit testing */
static uint8_t *km_pix;
static size_t km_w, km_h;
static int km_lcd_l, km_lcd_t, km_lcd_r, km_lcd_b;        /* red rectangle in keymap pixels */

static queued_key_t kq[32];
static int kq_head, kq_tail, kq_active_g = -1, kq_active_b = -1;

/* ---------------- helpers ---------------- */

static NSString *defaultsPath(NSString *key, NSString *fallback) {
    NSString *p = [[NSUserDefaults standardUserDefaults] stringForKey:key];
    if (p.length && [[NSFileManager defaultManager] fileExistsAtPath:p]) return p;
    return fallback;
}

static NSString *supportDir(void) {
    NSArray *a = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *dir = [a.firstObject stringByAppendingPathComponent:@"TI-84 Mac"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
                                               attributes:nil error:nil];
    return dir;
}
/* one save state per ROM, so several ROMs can be tried without clobbering each other */
static NSString *gRomPath;
static NSString *statePath(void) {
    NSString *name = gRomPath.lastPathComponent.stringByDeletingPathExtension;
    if (!name.length) name = @"calc";
    return [supportDir() stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"t84"]];
}

static CGImageRef loadImage(NSString *path) {
    if (!path) return NULL;
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], NULL);
    if (!src) return NULL;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    return img;
}

/* Decode the keymap: cache its pixels and find the red LCD rectangle. */
static BOOL loadKeymap(NSString *path) {
    CGImageRef img = loadImage(path);
    if (!img) return NO;
    km_w = CGImageGetWidth(img);
    km_h = CGImageGetHeight(img);
    free(km_pix);
    km_pix = calloc(km_w * km_h, 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(km_pix, km_w, km_h, 8, km_w * 4, cs,
                                             (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(0, 0, km_w, km_h), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(img);

    km_lcd_l = (int)km_w; km_lcd_t = (int)km_h; km_lcd_r = -1; km_lcd_b = -1;
    for (size_t y = 0; y < km_h; y++) {
        for (size_t x = 0; x < km_w; x++) {
            uint8_t *p = km_pix + (y * km_w + x) * 4;
            if (p[0] > 200 && p[1] < 60 && p[2] < 60) {       /* LCD marker */
                if ((int)x < km_lcd_l) km_lcd_l = (int)x;
                if ((int)x > km_lcd_r) km_lcd_r = (int)x;
                if ((int)y < km_lcd_t) km_lcd_t = (int)y;
                if ((int)y > km_lcd_b) km_lcd_b = (int)y;
            }
        }
    }
    km_lcd_r++; km_lcd_b++;
    return km_lcd_r > km_lcd_l && km_lcd_b > km_lcd_t;
}

static int keyAtKeymapPixel(int x, int y, int *g, int *b) {
    if (x < 0 || y < 0 || x >= (int)km_w || y >= (int)km_h) return 0;
    uint8_t *p = km_pix + ((size_t)y * km_w + x) * 4;
    if (p[0] != 0) return 0;                                  /* white or red */
    *g = p[1] / 16;
    *b = p[2] / 16;
    return 1;
}

static void enqueueKey(int g, int b, int frames) {
    int next = (kq_tail + 1) % 32;
    if (next == kq_head) return;
    kq[kq_tail] = (queued_key_t){ g, b, frames };
    kq_tail = next;
}

static const tikey_t *keyNamed(const char *name) {
    for (int i = 0; i < TIKEY_COUNT; i++)
        if (!strcmp(TIKEYS[i].name, name)) return &TIKEYS[i];
    return NULL;
}
static const tikey_t *keyForAlpha(char c) {
    for (int i = 0; i < TIKEY_COUNT; i++)
        if (TIKEYS[i].alpha && TIKEYS[i].alpha == c) return &TIKEYS[i];
    return NULL;
}
static void enqueueNamed(const char *name, int frames) {
    const tikey_t *k = keyNamed(name);
    if (k) enqueueKey(k->group, k->bit, frames);
}

static void pumpKeyQueue(void) {
    static int frames_left = 0;
    if (kq_head == kq_tail && kq_active_g < 0 && frames_left <= 0) return;
    if (kq_active_g >= 0) {
        if (--frames_left > 0) return;
        calc_key(&calc, kq_active_g, kq_active_b, 0);
        kq_active_g = kq_active_b = -1;
        frames_left = 2;                                      /* gap between keys */
        return;
    }
    if (frames_left > 0) { frames_left--; return; }
    if (kq_head == kq_tail) return;
    queued_key_t q = kq[kq_head];
    kq_head = (kq_head + 1) % 32;
    kq_active_g = q.g; kq_active_b = q.b;
    frames_left = q.frames;
    calc_key(&calc, q.g, q.b, 1);
}

/* ---------------- view ---------------- */

@implementation TIView {
    uint8_t _gray[LCD_W * LCD_H];
    uint8_t _rgba[LCD_W * LCD_H * 4];
    int _mouse_g, _mouse_b;
}

- (instancetype)initWithFrame:(NSRect)f {
    self = [super initWithFrame:f];
    _mouse_g = _mouse_b = -1;
    _themeIndex = 0;
    return self;
}

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

static CGRect lcdRect(CGRect b, BOOL screenOnly) {
    if (screenOnly) {                                         /* fill, keep 3:2 */
        double sc = fmin(b.size.width / LCD_W, b.size.height / LCD_H);
        double w = LCD_W * sc, h = LCD_H * sc;
        return CGRectMake((b.size.width - w) / 2, (b.size.height - h) / 2, w, h);
    }
    double sx = b.size.width / (double)km_w, sy = b.size.height / (double)km_h;
    return CGRectMake(km_lcd_l * sx, b.size.height - km_lcd_b * sy,
                      (km_lcd_r - km_lcd_l) * sx, (km_lcd_b - km_lcd_t) * sy);
}


- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    drawCalc(ctx, self.bounds, self.skin, self.themeIndex, self.screenOnly);
    if (!rom_loaded) {
        NSDictionary *at = @{ NSForegroundColorAttributeName: NSColor.redColor,
                              NSFontAttributeName: [NSFont systemFontOfSize:16] };
        [@"No ROM loaded - File > Open ROM..." drawAtPoint:NSMakePoint(20, 20) withAttributes:at];
    }
}

/* ---- mouse ---- */

- (void)pointToKeymap:(NSPoint)p x:(int *)kx y:(int *)ky {
    CGRect b = self.bounds;
    *kx = (int)(p.x / b.size.width * km_w);
    *ky = (int)((b.size.height - p.y) / b.size.height * km_h);
}

- (void)mouseDown:(NSEvent *)e {
    if (self.screenOnly) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int kx, ky, g, b;
    [self pointToKeymap:p x:&kx y:&ky];
    if (keyAtKeymapPixel(kx, ky, &g, &b)) {
        _mouse_g = g; _mouse_b = b;
        calc_key(&calc, g, b, 1);
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseDragged:(NSEvent *)e {
    if (_mouse_g < 0) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int kx, ky, g, b;
    [self pointToKeymap:p x:&kx y:&ky];
    if (!keyAtKeymapPixel(kx, ky, &g, &b) || g != _mouse_g || b != _mouse_b) {
        calc_key(&calc, _mouse_g, _mouse_b, 0);
        _mouse_g = _mouse_b = -1;
    }
}
- (void)mouseUp:(NSEvent *)e {
    if (_mouse_g >= 0) calc_key(&calc, _mouse_g, _mouse_b, 0);
    _mouse_g = _mouse_b = -1;
}

/* ---- keyboard ---- */

static const tikey_t *mapCharacter(unichar ch) {
    switch (ch) {
        case '0': return keyNamed("0");
        case '1': return keyNamed("1");
        case '2': return keyNamed("2");
        case '3': return keyNamed("3");
        case '4': return keyNamed("4");
        case '5': return keyNamed("5");
        case '6': return keyNamed("6");
        case '7': return keyNamed("7");
        case '8': return keyNamed("8");
        case '9': return keyNamed("9");
        case '.': return keyNamed(".");
        case '+': return keyNamed("+");
        case '-': return keyNamed("−");
        case '*': return keyNamed("×");
        case '/': return keyNamed("÷");
        case '^': return keyNamed("^");
        case '(': return keyNamed("(");
        case ')': return keyNamed(")");
        case ',': return keyNamed(",");
        case '\r': case 3: return keyNamed("ENTER");
        case 127: return keyNamed("DEL");
        case 27: return keyNamed("CLEAR");
        case '\t': return keyNamed("2ND");
        case '`': return keyNamed("ALPHA");
        case NSUpArrowFunctionKey: return keyNamed("UP");
        case NSDownArrowFunctionKey: return keyNamed("DOWN");
        case NSLeftArrowFunctionKey: return keyNamed("LEFT");
        case NSRightArrowFunctionKey: return keyNamed("RIGHT");
        case NSF1FunctionKey: return keyNamed("Y=");
        case NSF2FunctionKey: return keyNamed("WINDOW");
        case NSF3FunctionKey: return keyNamed("ZOOM");
        case NSF4FunctionKey: return keyNamed("TRACE");
        case NSF5FunctionKey: return keyNamed("GRAPH");
        default: return NULL;
    }
}

- (void)keyDown:(NSEvent *)e {
    if (e.isARepeat) return;                                  /* the OS repeats on its own */
    NSString *s = e.charactersIgnoringModifiers;
    if (!s.length) return;
    unichar ch = [s characterAtIndex:0];

    if (ch >= 'a' && ch <= 'z') ch = (unichar)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') {                             /* letters go through ALPHA */
        const tikey_t *k = keyForAlpha((char)ch);
        if (k) { enqueueNamed("ALPHA", 3); enqueueKey(k->group, k->bit, 3); }
        return;
    }
    if (ch == ' ') { enqueueNamed("ALPHA", 3); enqueueNamed("0", 3); return; }

    const tikey_t *k = mapCharacter(ch);
    if (k) calc_key(&calc, k->group, k->bit, 1);
}

- (void)keyUp:(NSEvent *)e {
    NSString *s = e.charactersIgnoringModifiers;
    if (!s.length) return;
    unichar ch = [s characterAtIndex:0];
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == ' ') return;
    const tikey_t *k = mapCharacter(ch);
    if (k) calc_key(&calc, k->group, k->bit, 0);
}

- (void)flagsChanged:(NSEvent *)e {                            /* shift = 2nd, option = alpha */
    const tikey_t *second = keyNamed("2ND"), *alpha = keyNamed("ALPHA");
    if (second) calc_key(&calc, second->group, second->bit, (e.modifierFlags & NSEventModifierFlagShift) ? 1 : 0);
    if (alpha) calc_key(&calc, alpha->group, alpha->bit, (e.modifierFlags & NSEventModifierFlagOption) ? 1 : 0);
}
@end

/* Draw the whole calculator into any context: the window uses it, and --render
 * uses it to produce a PNG without opening a window. */
static void drawCalc(CGContextRef ctx, CGRect b, CGImageRef skin, int themeIndex, BOOL screenOnly) {
    static uint8_t _gray[LCD_W * LCD_H];
    static uint8_t _rgba[LCD_W * LCD_H * 4];
    const theme_t *th = &THEMES[themeIndex];

    if (screenOnly || !skin) {
        CGContextSetRGBFillColor(ctx, th->off[0] / 255.0, th->off[1] / 255.0, th->off[2] / 255.0, 1);
        CGContextFillRect(ctx, b);
    } else {
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawImage(ctx, b, skin);
    }

    calc_lcd_gray_ex(&calc, _gray, themeIndex == 0);   /* Classic keeps the panel's own contrast */
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        int v = _gray[i];
        for (int c = 0; c < 3; c++)
            _rgba[i * 4 + c] = (uint8_t)(th->off[c] + (th->on[c] - th->off[c]) * v / 255);
        _rgba[i * 4 + 3] = 255;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bm = CGBitmapContextCreate(_rgba, LCD_W, LCD_H, 8, LCD_W * 4, cs,
                                            (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGImageRef img = CGBitmapContextCreateImage(bm);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);   /* crisp pixels */
    CGContextDrawImage(ctx, lcdRect(b, screenOnly), img);
    CGImageRelease(img);
    CGContextRelease(bm);
    CGColorSpaceRelease(cs);
}


/* ---------------- application ---------------- */

@implementation AppDelegate {
    NSTimer *_timer;
    double _last;
}

- (void)startCalcWithRom:(NSString *)rom {
    if (rom_loaded) calc_save_state(&calc, statePath().fileSystemRepresentation);
    gRomPath = rom;
    if (calc_init(&calc, rom.fileSystemRepresentation) == 0) {
        rom_loaded = 1;
        self.window.title = [NSString stringWithFormat:@"TI-84 Plus — %@", rom.lastPathComponent];
        [[NSUserDefaults standardUserDefaults] setObject:rom forKey:@"romPath"];
        if (calc_load_state(&calc, statePath().fileSystemRepresentation) != 0)
            NSLog(@"starting from a cold reset (no usable saved state)");
    } else {
        rom_loaded = 0;
        NSAlert *a = [NSAlert new];
        a.messageText = @"Could not load that ROM";
        a.informativeText = @"A TI-84 Plus ROM is a 1 MB flash image (for example the .rom file "
                             "WabbitEmu uses). Dump it from your own calculator.";
        [a runModal];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    gApp = self;
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSString *home = NSHomeDirectory();
    NSString *bundle = [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84 Accessible"];
    NSString *skinDay = defaultsPath(@"skinDay", [bundle stringByAppendingPathComponent:@"TI-84Plus-Day.png"]);
    NSString *skinNight = defaultsPath(@"skinNight", [bundle stringByAppendingPathComponent:@"TI-84Plus-Night.png"]);
    NSString *skinClassic = defaultsPath(@"skinClassic",
        [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84Plus-Official-v2.png"]);
    NSString *keymap = defaultsPath(@"keymapPath", [bundle stringByAppendingPathComponent:@"TI-84Plus-Keymap.png"]);
    [d setObject:skinDay forKey:@"skinDay"];
    [d setObject:skinNight forKey:@"skinNight"];
    [d setObject:skinClassic forKey:@"skinClassic"];
    [d setObject:keymap forKey:@"keymapPath"];

    if (!loadKeymap(keymap)) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"Could not read the keymap image";
        a.informativeText = [NSString stringWithFormat:@"Expected %@", keymap];
        [a runModal];
        [NSApp terminate:nil];
    }

    double aspect = (double)km_w / km_h;
    double h = 820, w = h * aspect;
    NSRect frame = NSMakeRect(120, 120, w, h);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"TI-84 Plus";
    self.window.delegate = self;
    self.window.contentMinSize = NSMakeSize(160, 160 / aspect);
    self.window.contentAspectRatio = NSMakeSize(km_w, km_h);
    [self.window setFrameAutosaveName:@"TI84Window"];

    self.view = [[TIView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    self.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    gView = self.view;
    self.window.contentView = self.view;
    [self.window makeFirstResponder:self.view];
    [self.window makeKeyAndOrderFront:nil];

    [self setTheme:[d integerForKey:@"theme"]];

    NSString *rom = defaultsPath(@"romPath",
        [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84 Plus.rom"]);
    gRomPath = rom;
    [self startCalcWithRom:rom];

    _last = CACurrentMediaTime();
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self selector:@selector(tick:)
                                   userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)tick:(NSTimer *)t {
    double now = CACurrentMediaTime();
    double dt = now - _last;
    _last = now;
    if (dt > 0.25) dt = 0.25;                                  /* never try to catch up forever */
    if (rom_loaded) {
        pumpKeyQueue();
        calc_run(&calc, (int64_t)(dt * calc.freq));
        calc_frame(&calc);
    }
    [self.view setNeedsDisplay:YES];
}

- (void)setTheme:(NSInteger)i {
    if (i < 0 || i >= NTHEMES) i = 0;
    self.view.themeIndex = (int)i;
    NSString *path = [NSUserDefaults.standardUserDefaults stringForKey:
                      [NSString stringWithUTF8String:THEMES[i].skin_key]];
    CGImageRef img = loadImage(path);
    if (img) {
        if (self.view.skin) CGImageRelease(self.view.skin);
        self.view.skin = img;
    }
    [NSUserDefaults.standardUserDefaults setInteger:i forKey:@"theme"];
    [self.view setNeedsDisplay:YES];
    for (NSMenuItem *m in [[[NSApp mainMenu] itemWithTitle:@"View"] submenu].itemArray)
        if (m.tag >= 100 && m.tag < 100 + NTHEMES) m.state = (m.tag - 100 == i) ? NSControlStateValueOn : NSControlStateValueOff;
}

/* ---- menu actions ---- */

- (void)chooseTheme:(NSMenuItem *)sender { [self setTheme:sender.tag - 100]; }

- (void)toggleScreenOnly:(NSMenuItem *)sender {
    BOOL on = !self.view.screenOnly;
    self.view.screenOnly = on;
    sender.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    if (on) {
        self.window.contentAspectRatio = NSMakeSize(LCD_W, LCD_H);
        self.window.contentMinSize = NSMakeSize(192, 128);
    } else {
        self.window.contentAspectRatio = NSMakeSize(km_w, km_h);
        self.window.contentMinSize = NSMakeSize(160, 160.0 * km_h / km_w);
    }
    [self.view setNeedsDisplay:YES];
}

- (void)openRom:(id)sender {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.allowsMultipleSelection = NO;
    p.message = @"Choose a TI-84 Plus ROM image";
    if ([p runModal] == NSModalResponseOK) [self startCalcWithRom:p.URL.path];
}

- (void)resetCalc:(id)sender {
    if (rom_loaded) {
        calc_reset(&calc);
        NSLog(@"calculator reset");
    }
}

- (void)saveStateNow:(id)sender { if (rom_loaded) calc_save_state(&calc, statePath().fileSystemRepresentation); }

- (void)pressOn:(id)sender { enqueueNamed("ON", 4); }
- (void)contrastUp:(id)sender { enqueueNamed("2ND", 3); enqueueNamed("UP", 3); }
- (void)contrastDown:(id)sender { enqueueNamed("2ND", 3); enqueueNamed("DOWN", 3); }

- (void)actualSize:(id)sender {
    NSRect f = self.window.frame;
    NSSize content = NSMakeSize(km_w, km_h);
    NSRect r = [self.window frameRectForContentRect:NSMakeRect(f.origin.x, f.origin.y, content.width, content.height)];
    [self.window setFrame:r display:YES animate:YES];
}
- (void)fitScreen:(id)sender {
    NSRect vis = self.window.screen.visibleFrame;
    double aspect = self.view.screenOnly ? (double)LCD_W / LCD_H : (double)km_w / km_h;
    double h = vis.size.height, w = h * aspect;
    if (w > vis.size.width) { w = vis.size.width; h = w / aspect; }
    NSRect r = [self.window frameRectForContentRect:NSMakeRect(vis.origin.x, vis.origin.y, w, h)];
    r.size.height = fmin(r.size.height, vis.size.height);
    [self.window setFrame:r display:YES animate:YES];
}

- (void)applicationWillTerminate:(NSNotification *)n {
    if (rom_loaded) calc_save_state(&calc, statePath().fileSystemRepresentation);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

/* ---------------- menus ---------------- */

static NSMenuItem *item(NSMenu *m, NSString *title, SEL sel, NSString *key) {
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title action:sel keyEquivalent:key];
    [m addItem:i];
    return i;
}

static void buildMenu(void) {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu *appMenu = [NSMenu new];
    item(appMenu, @"About TI-84 Plus", @selector(orderFrontStandardAboutPanel:), @"");
    [appMenu addItem:[NSMenuItem separatorItem]];
    item(appMenu, @"Hide", @selector(hide:), @"h");
    item(appMenu, @"Quit", @selector(terminate:), @"q");
    appItem.submenu = appMenu;

    NSMenuItem *fileItem = [NSMenuItem new];
    [bar addItem:fileItem];
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    item(file, @"Open ROM…", @selector(openRom:), @"o");
    item(file, @"Save State Now", @selector(saveStateNow:), @"s");
    [file addItem:[NSMenuItem separatorItem]];
    item(file, @"Reset Calculator", @selector(resetCalc:), @"r");
    fileItem.submenu = file;

    NSMenuItem *viewItem = [NSMenuItem new];
    [bar addItem:viewItem];
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    for (int i = 0; i < NTHEMES; i++) {
        NSMenuItem *m = item(view, [NSString stringWithUTF8String:THEMES[i].name],
                             @selector(chooseTheme:), [NSString stringWithFormat:@"%d", i + 1]);
        m.tag = 100 + i;
    }
    [view addItem:[NSMenuItem separatorItem]];
    item(view, @"Screen Only (big screen)", @selector(toggleScreenOnly:), @"0");
    item(view, @"Actual Size", @selector(actualSize:), @"=");
    item(view, @"Fit to Screen", @selector(fitScreen:), @"f");
    viewItem.submenu = view;

    NSMenuItem *calcItem = [NSMenuItem new];
    [bar addItem:calcItem];
    NSMenu *cm = [[NSMenu alloc] initWithTitle:@"Calculator"];
    item(cm, @"Press ON", @selector(pressOn:), @"\r");
    item(cm, @"Screen Darker (2nd ▲)", @selector(contrastUp:), @"]");
    item(cm, @"Screen Lighter (2nd ▼)", @selector(contrastDown:), @"[");
    calcItem.submenu = cm;

    NSMenuItem *winItem = [NSMenuItem new];
    [bar addItem:winItem];
    NSMenu *win = [[NSMenu alloc] initWithTitle:@"Window"];
    item(win, @"Minimize", @selector(performMiniaturize:), @"m");
    item(win, @"Zoom", @selector(performZoom:), @"");
    winItem.submenu = win;
    NSApp.windowsMenu = win;
    NSApp.mainMenu = bar;
}

/* Offscreen render of exactly what the window draws — used to check the app
 * without a display: ti84mac --render out.png WIDTH THEME SCREENONLY "KEYS" */
static int renderToPNG(const char *out, int width, int theme, int screenOnly, const char *keys) {
    NSString *home = NSHomeDirectory();
    NSString *bundle = [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84 Accessible"];
    NSString *kmPath = [bundle stringByAppendingPathComponent:@"TI-84Plus-Keymap.png"];
    NSString *skinPath = theme == 2 || theme == 3
        ? [bundle stringByAppendingPathComponent:@"TI-84Plus-Night.png"]
        : (theme == 1 ? [bundle stringByAppendingPathComponent:@"TI-84Plus-Day.png"]
                      : [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84Plus-Official-v2.png"]);
    if (!loadKeymap(kmPath)) { fprintf(stderr, "keymap?\n"); return 1; }
    CGImageRef skin = loadImage(skinPath);
    NSString *rom = [home stringByAppendingPathComponent:@"Documents/WabbitEmu/TI-84 Plus.rom"];
    gRomPath = rom;
    if (calc_init(&calc, rom.fileSystemRepresentation)) { fprintf(stderr, "rom?\n"); return 1; }
    rom_loaded = 1;

    void (^run)(double) = ^(double s) {
        double left = s;
        while (left > 0) { double sl = left > 0.01 ? 0.01 : left; calc_run(&calc, (int64_t)(sl * calc.freq)); calc_frame(&calc); left -= sl; }
    };
    run(1.0);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", keys ? keys : "ON");
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        const tikey_t *k = keyNamed(tok);
        if (!k) { fprintf(stderr, "unknown key %s\n", tok); continue; }
        calc_key(&calc, k->group, k->bit, 1); run(0.10);
        calc_key(&calc, k->group, k->bit, 0); run(0.15);
    }
    run(2.5);

    int height = (int)(width * (screenOnly ? (double)LCD_H / LCD_W : (double)km_h / km_w));
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, width, height, 8, 0, cs,
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    drawCalc(ctx, CGRectMake(0, 0, width, height), skin, theme, screenOnly);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFURLRef url = (__bridge CFURLRef)[NSURL fileURLWithPath:[NSString stringWithUTF8String:out]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)@"public.png", 1, NULL);
    CGImageDestinationAddImage(dst, img, NULL);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    if (skin) CGImageRelease(skin);
    printf("wrote %s (%dx%d) theme=%s\n", out, width, height, THEMES[theme].name);
    return 0;
}

int main(int argc, const char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--render")) {
        @autoreleasepool {
            return renderToPNG(argv[2], argc > 3 ? atoi(argv[3]) : 500,
                               argc > 4 ? atoi(argv[4]) : 0, argc > 5 ? atoi(argv[5]) : 0,
                               argc > 6 ? argv[6] : "ON");
        }
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
        buildMenu();
        AppDelegate *del = [AppDelegate new];
        NSApp.delegate = del;
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}
