# TI-84 Plus accessible WabbitEmu skin — work log (started 2026-09-11)

- **Spec:** `~/Downloads/ti84-skin-spec.md`
- **Approved plan:** `~/.claude/plans/users-aaroncole-downloads-ti84-skin-spe-gentle-allen.md`
- **Deliverables go to `~/Documents/WabbitEmu/`:**
  - `TI-84Plus-Official.png`
  - `TI-84Plus-Official-Keymap.png`

Run everything from this folder with `/opt/homebrew/bin/python3.11`. The system `python3` has no Pillow.

## Decisions (confirmed with the user)
- **Colors follow the real photo, not the spec's color list.**
  - Numbers, ÷ × − + and ENTER are light gray.
  - 2ND is blue and ALPHA is green.
  - The keypad face is dark navy and the area around the screen is silver.
- **No model downloads** (metered connection), so there's no ControlNet. Layout is locked by code, and ComfyUI adds only material and texture.
- **WabbitEmu can't be tested here:** the user is on a Mac with no Wine. Loading is simulated by `verify.py`, and the real test happens on the student's Windows PC.
- **Run ComfyUI via the comfy-mcp MCP.**
  - It was registered at user scope in `~/.claude.json` on 2026-09-11. The backup is `~/.claude.json.bak-before-comfy-mcp`.
  - Confirmed working: the `mcp__comfy-mcp__*` tools load in new sessions. Call `server_info` first.
  - `run_workflow` takes API-format JSON. The slot tools (`set_workflow_slot` etc.) only work on frontend-format workflows, so write the JSON directly.
- **One deliberate deviation from the photo, for legibility:**
  - On the real product, the top-row, operator and ENTER keys print white legends on light gray.
  - Here those legends are dark. Toggle with `LEGIBLE_LIGHT_KEYS` in `legends.py`.

- **The user confirmed dark legends on the light keys (2026-09-11).**
- **Target screen: 1080p laptop.**
  - WabbitEmu shows the skin at about 1000 px tall (scale ≈0.27).
  - Judge legibility with `display_sim.py out/... out/sim`, using the `1080p_fit_nearest` crop as the worst case.

## Facts from the WabbitEmu source (sputt/wabbitemu, master)
- **Keymap encoding (`guibuttons.c`):**
  - Pixels are read as ARGB, masked with `& 0xFFFFFF`.
  - The code skips white, and any pixel whose real red channel is not 0.
  - Then `bit = B >> 4` and `group = G >> 4`. The spec's `(0, g*16, b*16)` is correct.
- **Clicks (`gui.c` `WM_LBUTTONDOWN`):**
  - A click reads the **single keymap pixel** under the mouse, and ignores it if R == 0xFF.
  - Any other stray color would write out of bounds into `keys[g][b]`. So the keymap must contain only white, red and the 50 exact colors, and `verify.py` enforces this.
- **Pressed-key feedback:** WabbitEmu darkens every non-white keymap pixel inside the key's bounding box.
- **LCD (`guiskin.c` `FindLCDRect`):**
  - Finds the first pure-red pixel in raster order, walks right, then walks down the last red column.
  - Our LCD is 1152×768, exactly 12× the 96×64 panel.
- **Display size (`guiskin.c`, `guisize.c`). Corrected 2026-09-11 after the laptop test; the old "shrinks to fit" note was wrong:**
  - The default display scale is `sqrt(253750*4/(w*h))`: always about 698×1452 for our aspect ratio, whatever the source size.
  - If the screen is shorter than the skin's *source* height, the "tiny screen" path runs, but a sign bug in `GetSkinScale` makes it return 1.0 (native size). Either way the window opens taller than a 1080p screen and gets clamped to the screen height.
  - **`GetMinMaxInfo` sets the minimum window size to half the source pixels** (`SKIN_WIDTH/2 × SKIN_HEIGHT/2`). With the 1760×3658 v1 that was 880×1829, which is taller than any 1080p screen. The laptop showed only the top half and the window was stuck. The 4K 32" monitor worked because 2160 ≥ 1829.
  - Only **corner** drags resize; edge drags are rejected (`HandleSkinSizingMessage`).
  - The skin scale isn't saved between launches. `SetProcessDPIAware()`, so sizes are physical pixels.
  - It paints with GDI+ `InterpolationModeLowQuality`.
  - **Consequences:** keep source height/2 below about 940 px (checked in `verify.py`), and make legend strokes survive low-quality downscaling (`display_sim.py`).
- **LCD colours are hard-coded in the emulator (`guilcd.c`, `LCDProc` `WM_CREATE`):**
  - The palette runs from (0x9E,0xAB,0x88) = (158,171,136) green-gray down to black, drawn in a child window over the red rect. **The skin cannot change it.** Contrast against black is about 8.6:1.
  - With the "alpha blend LCD" option on, 42.4% of the skin's pixels under the rect are blended over the LCD (black text turns gray); it should stay off.
  - Our skin has (155,168,145) under the rect.
  - `Wabbitemu.exe` 1.9.5.22 is **UPX-packed**, so the constants can't be patched without unpacking (`upx -d`) first. Untried.
- **Background:** transparent skin pixels show WabbitEmu's gray fill in normal mode, and let the cutout mode shape the window.

## Scripts
| Script | Role |
|---|---|
| `extract_ref.py` | Pulls the 325×602 real photo and 50 key rectangles out of `ti-84plus.skn` (VTI v2.5). |
| `layout.py` | Single source of truth: photo-unit geometry, palette, key table, and `Layout(scale)` for mapping to pixels. |
| `base_render.py` | Code-drawn body and molded key caps, no text. Writes `out/base_full.png` and the ComfyUI input `input/ti84_layout.png` (832×1728). |
| `legends.py` | All legends and wordmarks, drawn in code. Arial Bold, with ▶, ␣ and superscripts built by hand. |
| `composite.py` | Base render, plus optional diffusion detail transfer (luminance high-pass only), plus legends. |
| `keymap.py` | Flat keymap built from `layout.py`. |
| `verify.py` | Port of WabbitEmu's loader plus the spec checks. Writes the overlay image. |
| `display_sim.py` | Shows what the student sees after WabbitEmu's shrink and a magnifier. |
| `themes.py` | Official / Day / Night palettes, applied in place to `PAL` and `STYLE`. |
| `patch_lcd.py` | Patches a copy of Wabbitemu.exe so the LCD palette is black-on-white or white-on-black. |
| `macemu/` | Native macOS TI-84 Plus emulator (z80 + hardware + Cocoa app) that uses this same skin and keymap. `macemu/README.md`. |

## Progress
1. **v0, code only** (`out/skin_v0.png` + `out/keymap_v0.png`):
   - `verify.py` passes every check.
   - Legends stay legible in the 1080p point-sampled simulation.
2. **ComfyUI material pass, done 2026-09-11** (details under "Material pass results").
3. **Delivered 2026-09-11:** v1 skin + keymap copied to `~/Documents/WabbitEmu/`, and `verify.py` passed on those exact files (`out/verify_final.txt`).
4. **User test on Windows (2026-09-11), confirmed working:**
   - The keymap works perfectly: all keys register, and the LCD sits in the right place.
   - The skin "looks great". Keep the look: the photo-matched colors, dark legends on light keys, and code-drawn legends. Keep the RealVisXL d0.38/s84001 material pass with edge-guarded detail transfer.
   - **Problem:** on the 1080p laptop the window showed only its top half and would barely resize. The 32" monitor was fine. The cause is under "Display size" above.
5. **v2 = the laptop fix (2026-09-11):**
   - `out/skin_v2.png` is v1 halved with Lanczos (880×1829), so the look is unchanged. `out/keymap_v2.png` is `keymap.py ... 3.25`.
   - The LCD is 6× (152,160)-(728,544), exactly v1/2. The minimum window is 440×914, and the minimum gap is 5 px.
   - `Layout` margin and LCD padding now scale with `scale`; the 6.5 keymap is byte-identical.
   - `verify.py` passes (`out/verify_final_v2.txt`), and the new window check correctly fails v1. The 1080p sim reads the same as v1 (`out/cmp_sim_v1_v2.png`).
   - Delivered as `TI-84Plus-Official-v2.png` + `TI-84Plus-Official-v2-Keymap.png`. v1 is kept.
   - **Deliberate deviation from the spec's "3000–4000 px tall" target:** that size is what triggers WabbitEmu's minimum-window rule.
6. **Day + Night build (2026-09-11). The user asked for the exe patch plus night mode per the advice.** Awaiting the Windows test.
   - **Bundle:** `~/Documents/WabbitEmu/TI-84 Accessible/`. The original `Wabbitemu.exe` is untouched (sha256 `6cebce1e…c829`). Contents:
     - `Wabbitemu-Day.exe`, `Wabbitemu-Night.exe`, `Wabbitemu-NightYellow.exe`
     - `Wabbitemu.ini` (Day) and `NightMode.ini` (Night)
     - `TI-84Plus-Day.png`, `TI-84Plus-Night.png`, and `TI-84Plus-Keymap.png` (= `keymap_v2`)
     - a copy of the ROM, and `HOW TO USE.txt` (CRLF)
     - Hashes are in `out/bundle_sha256.txt`.
   - **Exe patch (`patch_lcd.py`):**
     - `brew install upx` (3.3 MB installed), then `upx -d`.
     - The palette loops in `LCDProc` are strength-reduced accumulators with 32-bit immediates at VA 0x140069B20–0x140069CAB: 11 sites, covering both `bi` and `contrastbi`.
     - A channel ramp v0→v255 is start = 255·v0, step = v0−v255, which divides exactly.
     - The script refuses unless every site is byte-exact, and emulates the x86 divide to prove the palette. It reproduces the original (158,171,136) and contrastbi (158,171,152).
     - The disassembly after patching has an identical opcode sequence. Day changes 20 bytes, Night 37.
   - **Two exes in one folder:**
     - Night variants use `--ini NightMode.ini`, a same-length rename of the `\Wabbitemu.ini` string.
     - So each copy keeps its own skin setting, but both share the auto-save. gui.c writes it to the **parent** folder as `<folder>wabbitemu.sav`, because of a path-trimming bug, then sets rom_path to it before `SaveRegistrySettings`.
     - One-time setup: open and close Day, then Night, before storing anything; otherwise Night's first run overwrites Day's save.
     - Only one instance at a time, because of `FindWindow` on the class name.
   - **Portable mode:** triggered by `Wabbitemu.ini` in the **current directory**. The INIs set `alphablend_lcd=0` (default is ON, which blends 42% skin over the LCD), `check_updates=0`, `startY=0` and relative paths.
   - **Contrast interplay (`lcd.c` `LCD_update_image`):**
     - The background index is 0 only at calculator contrast ≤ 21. Above that, an overlay greys it: about 243 at 24.
     - Below 20, the "on" pixels lighten.
     - The how-to tells the user to adjust with 2nd+▼/▲.
   - **Skins (`themes.py`, `composite.py --theme day|night --half`):**
     - `STYLE` in `layout.py` adds glass gradient, outline ring, rim alpha and texture keep-out for the glass. The official theme still reproduces v1 byte-for-byte.
     - Day = v2 with a pure-white glass (255 under the LCD).
     - Night: near-black body and caps, pure-white legends, light blue and green 2nd/alpha inks, deep blue/green 2nd/ALPHA caps, grey outline rings (blue/green on 2nd/ALPHA), black glass.
     - Both pass `verify.py` against `keymap_v2` (`out/verify_day.txt`, `verify_night.txt`); `out/cmp_themes.png` and `out/sim_night_*` are the checks.
   - **Not testable here:** the patched exes, portable INIs and shared save all need the Windows test.

7. **Mac emulator (2026-09-11). The user asked to be able to test ROMs on the Mac.** Built from scratch in `macemu/`; see `macemu/README.md` for the full write-up.
   - **Why:** WabbitEmu is Windows-only and there is no Wine here, so nothing in this project could be tried on the Mac. Now the same skin + keymap pair drives a native app.
   - **What it is:** a Z80 core (`z80.c`), TI-84 Plus hardware (`ti84.c`), a Cocoa front end (`main.m`), a headless CLI driver (`headless.c`) and a ZEX harness (`zextest.c`). Only the Xcode command line tools are needed — `./build.sh`.
   - **Verified:**
     - `zexdoc`: **67/67 tests OK, 0 errors** (5.76 billion instructions, ~2 min).
     - Boots the real `TI-84 Plus.rom` (OS 2.55MP, boot 1.03): RAM-cleared splash, home screen, arithmetic, MATH menu, greyscale, save-state round trip.
     - `ti84mac --render out.png W THEME SCREENONLY "KEYS"` draws through the same code as the window, so the GUI is checkable without a display.
   - **Facts that cost time, worth keeping:**
     - Ports with no device must read **0xFF**, not 0 (`device_input` in wabbitemu's `core/device.c`). With 0, the OS boots and draws its splash but never scans the keyboard — it sits in a USB/link poll loop on ports 09/45/55/56/82/84/86/8F.
     - The 84+ needs the fake-USB values TilEm supplies: port 4C = 0x22, 55 = 0x1F, 4D = line state 0xA5, 56 = 0x00.
     - A static screen never enqueues a greyscale frame, because frames are enqueued when the OS *pauses* between LCD writes. `calc_frame()` samples at 30 Hz as the fallback, like `LCD_image`.
     - At reset bank 0 is the **boot page** (0x3F), and the first execution from a port-6 page swaps it to page 0.
     - Save states carry `sizeof(calc_t)` and an FNV-1a fingerprint of the ROM; a state from another build or another ROM is refused rather than loaded as garbage (an earlier stale state silently "reset" the calculator).
   - **Delivered:** `~/Documents/WabbitEmu/TI-84 Accessible/TI-84 Plus.app` (nothing existing was overwritten). Themes Classic/Day/Night/Night Amber switch skin + LCD palette together; Day and Night render pure black-on-white and white-on-black because the accessible themes skip the panel contrast model. **Screen Only (⌘0)** fills the window with the 96×64 screen for magnification, and the window has no WabbitEmu-style minimum size.
   - **Not done:** sending .8xp programs over the link port, sound, USB.

8. **Magnifier and detached LCD (2026-09-11). The user asked for both.**
   - **Click-and-hold magnifier (4×).** Hold the mouse ~0.55 s anywhere on the calculator; hold again, or press Escape (or ⌘Z), to leave. `MAG_Z`, `MAG_HOLD`, `MAG_SLOP` in `main.m` are the knobs.
     - The transform is Apple's pointer-centred zoom: origin `= c * (1 - 1/Z)`. That has three properties worth keeping: the source point under the pointer is exactly `c`, so **hit testing needs no inverse at all** (the existing keymap lookup is untouched and still correct while zoomed); the magnified rect can never leave the view; and sweeping the pointer across the window reaches every corner.
     - **The gesture costs mouse auto-repeat.** A hold now means "magnify", so a click had to move to *press on release* — otherwise holding over `5` would type `5` (or a run of them) before zooming. Holding a key on the Mac keyboard still auto-repeats, which is the documented replacement.
     - `TI84_MAG="fx,fy" ti84mac --render …` runs the same transform offscreen, so the zoom is checkable without a display.
   - **Detached LCD window** replaces Screen Only, which had the flaw of taking the calculator away while it was on. A second window draws only the LCD from the same emulator state each frame, keeps 3:2 so pixels stay square, uses `kCGInterpolationNone`, follows the theme, reopens with the app (`lcdDetached` default), and takes the keyboard so you can type while looking only at the big screen. Actual Size / Fit to Screen now act on whichever window is in front; for the LCD, Actual Size snaps to a whole-pixel scale.
   - **Verified in the running app** (computer-use, which the earlier session did not have): clicks type the right keys; hold toggles the magnifier and does **not** type the key it was held on; the zoom stays locked to the pointer (moved the pointer onto `5`, toggled, `5` was still exactly under the pointer); the detached LCD mirrored `2+3→5` and `6×7→42` live; it reopened after a relaunch.
   - **Escape is code-checked only.** macOS automation will not deliver a synthetic Escape to this app — an `NSLog` in `keyDown:` showed `9` arriving (`ch=57`) and *no* event at all for Escape, twice. The handler now matches on the character **and** key code 53.

9. **Porting both features to Windows, from source (2026-09-12). The user chose to build WabbitEmu from source rather than keep byte-patching.**
   - Everything is in `winsrc/`: `accessible-wabbitemu.patch` (+383 / -51, 21 hunks, five files), `README.md` (the build guide), and `ini/{Day,Night,Amber}/Wabbitemu.ini`.
   - **The detached LCD needed no work.** WabbitEmu has always had it (`gui/guidetached.c`, View menu); `patch_lcd.py` only rewrote palette immediates, so the three delivered exes already have it. It also already follows the Day/Night/Amber colors, because `bi`/`contrastbi` are shared globals in `guilcd.c` and every draw goes through one `GetLCDColorPalette`. Verify on the laptop; do not build for it.
   - **The magnifier reuses the Mac transform.** `d = Z*(p - c*k)`, `k = 1 - 1/Z`. Because the centre tracks the mouse, `p == c` at click time, so WabbitEmu's keymap lookup (client point / `skin_scale`, read the keymap pixel) is untouched. Same deferred-keypress trade-off as the Mac: a click types on the way up, so mouse hold-to-repeat is gone and the PC keyboard is the replacement.
   - The LCD is a child HWND, not something the frame paints, so it is moved to its magnified rect (`MagPlaceLCD`) instead of being scaled by the frame; it already stretches its contents to its client rect.
   - **`lcd_theme` replaces `patch_lcd.py`**: a normal setting (0 green, 1 day, 2 night, 3 amber), read once at LCD creation. No UPX, no hard-coded VAs, nothing that breaks when upstream recompiles. The accessibility themes also hold their hue past contrast 37, which the stock palette does not.
   - Deliberately touches **no** `.rc` or `resource.h` — no new menu items — because that is where an untested build breaks.
   - **Resolved 2026-09-12, the user chose the shared save:** the flavour now comes from the command line (`-theme day|night|amber|green`, `-skin`, `-keymap`), so it is one folder, one `Wabbitemu.ini` and three shortcuts, and the auto-save stays shared exactly as it was with the three patched exes. The override is applied after the settings load and is **never written back** — `SaveRegistrySettings` skips `skin_path`/`keymap_path` when they came from the command line — otherwise the last window closed would redefine the flavour for the other two. Parsed from `__argc`/`__targv` so one code path serves both Unicode and ANSI builds.
   - **Not compiled and not run.** No MSVC and no Wine on this Mac. The patch is verified only to apply cleanly to `master` and to balance braces. Most likely first-build problems, in order: `MagCaptureLCDRect` assumes `hwndLCD` is a child of `hwndFrame`; the `LCDProc` `WM_MOUSEMOVE` forwarding is optional and can be dropped.

10. **The Windows exes will not launch, and the WabbitEmu path is now at risk (2026-09-12).**
   - The error on every one of the three is *"Windows cannot access the specified device, path, or file. You may not have the appropriate permissions to access the item."*
   - Ruled out: the binaries themselves (valid x64 PE, 5,682,176 bytes, three distinct md5s `810736369ed1` / `ff749ef2dfd2` / `79c87146797b` on this Mac); Defender (nothing in Protection history); AppLocker (no events). **The same error occurs from `C:\Temp`**, which eliminates every folder-based explanation — OneDrive placeholders, folder ACLs, a stale shortcut Target or Start-in.
   - So the block follows the *binary*, not its location. Two candidates remain: **Mark of the Web** (the `Zone.Identifier` alternate data stream copies along with the file, and zone 4 / Restricted produces exactly this wording, where zone 3 would instead give the "publisher could not be verified" prompt), and **Smart App Control**. MOTW is the cheap check — `Get-Item <exe> -Stream *`, then `Unblock-File`, or the **Unblock** box in Properties → General.
   - **This decides whether building WabbitEmu from source is worth anything.** A self-compiled exe is unsigned too, so if the cause is Smart App Control or an SRP that blocks unsigned binaries as a class, the source build is blocked identically and the ~2-5 GB Visual Studio download would be wasted. Do not start that download until MOTW is ruled out. Note also that Smart App Control cannot be re-enabled once switched off without reinstalling Windows.
   - **Unanswered by the user:** whether an Unblock checkbox appears in Properties, and the Smart App Control state.

11. **Pivot to the TI-84 Plus CE and CEmu (2026-09-12).** In the user's words: *"I have obtained a ROM file for the TI-84 Plus CE and CEmu emulator that it runs on. We will pick up our development with the new rom and emulator."* A different calculator (eZ80, 320x240 colour LCD) and a different, cross-platform, open-source emulator - which sidesteps the Windows-binary and MSVC problems entirely. The WabbitEmu work above is not deleted, but it is no longer the active line.
   - ROM at `~/Documents/CEmu/TI-84Plus_CE_ROM.rom` (4 MB; a copy is on the Desktop, which is what CEmu's config points at). **Never commit it** - same rule as the 84 Plus ROM.
   - CEmu 2.0 is installed prebuilt at `/Applications/CEmu.app` (Qt6, arm64, Sept 2024). Config lives in `~/Library/Preferences/cemu-dev/CEmu/` as `cemu_config.ini` plus `cemu_image.ce` (the 5 MB saved calculator state).
   - **The user's five issues, in their order:** (1) screen and keyboard in one window; (2) detaching the screen should be a selectable feature, and reversible; (3) the keypad display is wrong when resizing, the skin approach suited them better; (4) black background with white / blue / purple / green strokes, cool colours prioritised; (5) the long-click magnifier we already built twice. They asked to take these **one at a time, checking in between**.

12. **CEmu issue 1 done, configuration only, no build (2026-09-12): one window in the real CE form factor.**
   - **It was already one window.** Screen and Keypad are dock *panels* in a single `CEmu | Calculator` window. Verified live: turned the calculator on and ran `5+3` then `7*6` on the on-screen keypad, got 8 and 42, and the saved state restores across restarts.
   - **A wrong turn worth remembering: `menubar` and `statusbar` are HIDE flags, not show flags** (`settings.cpp:setMenuBarState` does `ui->menubar->setHidden(state)`, wired to `actionHideMenuBar`). Both were already `false`, i.e. shown. Setting them `true` hid the status bar. They are back as found.
   - Separately, `app_menu` only ever reports `Apple, CEmu` for this app from the background, so **CEmu's File / Calculator / Docks menus could not be confirmed** - unresolved, and it matters for issue 2. Check it when CEmu can be frontmost.
   - **The form factor falls out of CEmu's own numbers.** Screen widget `440x351 * scale`, keypad base rect 162x238 kept at aspect. Stack them at one common scale and the keypad is as wide as the screen at `440 x 646.4`, so the whole calculator is `440 x 997.4` - **aspect 0.4411 against the real CE's 84/190 = 0.4421**. The stock layout looked wrong only because the two docks were not given their natural heights, so the keypad letterboxed narrower than the screen.
   - **The largest scale that fits a 982-px display is 90.** Window `396 x 925` (content 396 x 897 = screen 315 + keypad 582), LCD `288x216`, keypad 396 wide. Scale 100 would need 1025 px of height.
   - **The user chose this over a bigger LCD**, in their words: *"Stacked should look proportionally like the original TI-84 Plus CE form factor. The later-developed zoom feature will handle resizing beyond what fits on the screen."* So the LCD is deliberately smaller than the 320x240 default (and much smaller than the 512x384 that scale 160 gives) and the magnifier, issue 5, is what makes it readable.
   - **Settings applied** in `~/Library/Preferences/cemu-dev/CEmu/cemu_config.ini` (backup at `cemu_config.ini.bak`): `[Screen] scale=90`, `[Window] statusbar=true` (hides the FPS strip), `[Window] boundaries=false`, `[General] ui_edit_mode=false`.
   - **`ui_edit_mode=false` is the fix for the original complaint.** `DockWidget::setState(false)` drops both the dock title bars and the drag handles, so the screen cannot be pulled out by accident again - and it is also what buys back the 42 px the title bars used.
   - **The window geometry had to be written by hand**, because CEmu only saves it and the dock split has to land exactly on 315 + 582. `scratchpad/qini.py` implements QSettings' `@ByteArray()` escaping (`iniEscapedString` / `iniUnescapedString`, including the `escapeNextIfDigit` rule that keeps the minimal-width `\xNN` escapes unambiguous) and round-trips both the `geometry` and `state` blobs byte-for-byte. `geometry` is 66 bytes: magic `0x1D9D0CB`, version 3.0, then frameGeometry, normalGeometry, screenNumber, maximized, fullScreen, screenWidth, and a trailing v3 rect - QRects stored as left, top, right, bottom. Set frame `(2,33)-(397,957)` and both other rects `(2,61)-(397,957)`; the 28 px difference is the macOS title bar.
   - Verified stable across a quit and relaunch, including CEmu's own save-on-close rewrite of the file.

## Facts from the CEmu source (CE-Programming/CEmu, tag v2.0)
Read without cloning - about 250 KB fetched from `raw.githubusercontent.com` and the contents API. Copies are in the session scratchpad under `cemu/`.

- **The UI is Qt dock widgets.** `screenDock` and `keypadWidget_dock` are separate `DockWidget`s inside one `QMainWindow` (`gui/qt/dockwidget.cpp`, `settings.cpp:setUIDocks`). "Two windows" means the screen dock was dragged out and floated; it is not a second program.
- **`DockWidget::setState()` is the trap behind issue 2.** With UI edit mode *off* it calls `setAllowedAreas(Qt::NoDockWidgetArea)`, so a dock that happens to be floating at that moment can never be dropped back in. With edit mode *on*, any stray drag pops the screen out. There is no attach/detach command anywhere - only dragging.
- **The menu bar defaults to hidden** (`mainwindow.cpp:618`, default `false`). The *Docks* menu, *Settings*, and *Enable UI edit mode* all live there, so a hidden menu bar is almost certainly why there was "no way to attach it".
- **The LCD is a fixed-size widget, not a scaling one** (`settings.cpp:lcdAdjust`): `ui->lcd->setFixedSize(...)` and `ui->screenWidget->setFixedSize(...)`, driven by a percent slider (`Screen/scale`). Resizing the window does **not** grow the screen. That is the real content of issue 3.
- **The keypad is vector-drawn, not a bitmap skin** (`keypad/keypadwidget.cpp`; base rect 162x238, `mTransform` built with `Qt::KeepAspectRatio`). For a 10x magnification user this is *better* than a PNG skin - it stays crisp at any size. Its actual flaw is that `resizeEvent` centres horizontally only (`origin.width(), 0`), so the keypad top-anchors and leaves dead space below.
- **Issue 4 has exactly one clean insertion point:** `LCDWidget::draw()` in `gui/qt/lcdwidget.cpp` calls `emu_lcd_drawframe(m_renderedFrame.bits())` into an RGB32 QImage. A palette LUT applied straight after that recolours every frame. Since the CE panel is RGB565 there are only 65,536 source colours, so a precomputed 256 KB lookup table makes the per-pixel cost a single load. **Not reachable by configuration** - it needs a source build.
- **Issue 5** lands in `LCDWidget::paintEvent` plus `KeypadWidget::paintEvent`, both of which already carry `mTransform` / `mInverseTransform`. Also needs a source build. Note `LCDWidget::mousePressEvent` currently starts a screenshot drag, which a long-press will have to share with.
- **The geometry ceiling, which matters more than any of the above.** With the skin on, the screen widget is `440x351 * scale` and the keypad's natural aspect is 162:238. Stacked at the same scale the calculator wants about **440x997**, against a usable display of roughly **1512x950**. A *magnified* screen and a full keypad therefore **cannot both fit in one window on this Mac**. They coexist only at a moderate scale, because the screen is fixed-size and the keypad takes what is left: at `Screen/scale = 160` the window is about 704x982, holding a 512x384 LCD and a 286-px-wide keypad. **So issues 2, 4 and 5 are not extras - they are how the student gets past the size ceiling.**
- **The core is Qt-free C** - `core/`, about 400 KB across ~50 files, with its own `Makefile`. That keeps a second option open: a Cocoa frontend over CEmu's core, reusing `macemu/`'s window, skin, keymap and magnifier, with no Qt download at all. It would discard CEmu's UI, debugger and file-sending.
- Building the Qt frontend here needs Qt 6, cmake and ninja. Homebrew has none of them installed; that is roughly **350-600 MB** to download (~1.5 GB on disk). Xcode command line tools and clang 21 are present. **State the size before starting it** - see the `metered-connection` memory.

## Material pass results (2026-09-11)
- **MCP fix first:** every comfy-mcp tool failed with a bare "Error executing tool". The real cause, seen by probing the server over stdio, was `` `comfy` not found on PATH ``: the app launches MCP servers with a PATH lacking `~/Library/Python/3.11/bin`. Fixed two ways:
  - Symlink `/opt/homebrew/bin/comfy` → `~/Library/Python/3.11/bin/comfy` (works immediately).
  - `"env": {"COMFY_BIN": ".../Python/3.11/bin/comfy"}` on the comfy-mcp entry in `~/.claude.json` (backup `~/.claude.json.bak-before-comfy-bin`).
- **Workflows:** `make_wf.py` writes the sweep into `wf/` (plus `wf_img2img.json`), and `make_wf.py up IMAGE` writes `wf/upscale.json`. All ran via `run_workflow` and validated clean. Outputs are copied to `out/diff/`.
- **Sweep verdict** (`out/diff/sheet.png`, `sheet_crops.png`, `toprow.png`):
  - MPS produced real images, not noise. No render moved keys.
  - d45: the start of glyph-like tick marks on the top-row keys, and seed 84002 turned 2ND gray. Rejected.
  - d38_s84002: a crack artifact near the arrow surround. Rejected.
  - **d38_s84001 chosen**, then 4x-UltraSharp gave `out/diff_up.png` (3328×6912).
- **Detail transfer had to change:**
  - The first attempt (radius 6, strength 1) printed dark double outlines around every key, because the diffusion redraws edges 1–2 px off the code edges, and it over-streaked the silver.
  - Now `detail_transfer(strength=0.7, radius=3, clamp=0.05)` and `edge_guard()` zero the transfer within ~10 px of the base's edges.
  - Result: v1 = v0 plus a subtle matte grain on the plastic. No halos, no ghost lettering, legends untouched.
- **v1 chosen over v0.** It passes verify, and `out/sim_v1_1080p_fit_nearest.png` is as legible as v0. Comparisons are in `out/cmp_keys.png`, `cmp_numbers.png` and `cmp_full_ref_v1.png`.
- **If the user wants more "photo" feel:** raise `strength` or `clamp` a little, re-run `composite.py`, and re-check `cmp_*` for halos. Anything at d45 and above needs a cap mask for the transfer.

## Rebuild commands (run from this folder)
The default scale is 6.5, which gives a 1760×3658 canvas. Every script takes the same scale, so a different size is one argument away.

```bash
/opt/homebrew/bin/python3.11 base_render.py                      # out/base_full.png + ComfyUI/input/ti84_layout.png
/opt/homebrew/bin/python3.11 composite.py out/skin_v0.png         # code-only skin (base + legends)
/opt/homebrew/bin/python3.11 composite.py out/skin_v1.png out/diff_up.png   # with diffusion detail transfer
/opt/homebrew/bin/python3.11 keymap.py out/keymap_v0.png
/opt/homebrew/bin/python3.11 verify.py out/skin_v1.png out/keymap_v0.png 6.5 out/overlay_v1.png   # exit 0 = pass
/opt/homebrew/bin/python3.11 display_sim.py out/skin_v1.png out/sim       # legibility after WabbitEmu's shrink
```

- `composite.py` **re-renders the base every run**, so edits to `layout.py`, `base_render.py` or `legends.py` all flow through.
- `composite.py OUT [DIFFUSION|-] [SCALE]`.
- `detail_transfer(strength=1.0, radius=6.0)` in `composite.py` controls how much diffusion texture comes through.

## Next step in detail: the ComfyUI material pass
1. **Build an API-format img2img graph and save it here as `wf_img2img.json`:**
   - `CheckpointLoaderSimple` loading `RealVisXL_V5.0_fp16.safetensors`.
   - `LoadImage` loading `ti84_layout.png`. It is already in `ComfyUI/input/`, at 832×1728.
   - `VAEEncode`, then `KSampler`, then `VAEDecode`, then `SaveImage`, with prefix `ti84/diff_d{dn}_s{seed}`.
   - KSampler settings: cfg 6, 35 steps, `dpmpp_2m` / `karras`. That's the wizard_dragon recipe.
   - **Positive prompt:** "studio product photograph of a graphing calculator, straight-on, matte molded plastic keys, soft even lighting".
   - **Negative prompt:** "text, letters, numbers, logo, watermark, perspective, tilted, blur".
2. **Sweep denoise 0.30 / 0.38 / 0.45 with 2 seeds each** (6 runs):
   - Use `run_workflow(wait=False)`, then `job(action="wait")`, then `fetch_outputs`.
   - Look at each result. This Mac's MPS backend has produced pure noise before; see the `comfyui-mps-produces-noise` memory.
   - **Reject any render that moved keys or changed the body shape.**
3. **Upscale the best render:** a second graph, `LoadImage` → `UpscaleModelLoader` (`4x-UltraSharp.safetensors`) → `ImageUpscaleWithModel` → `SaveImage`. Copy the result to `out/diff_up.png`. `composite.py` resizes it to the canvas.
4. **Watch for ghost lettering.**
   - SDXL often invents fake glyphs on blank keys, and the high-pass would imprint them faintly under the real legends.
   - If you see any, zero or cut the transfer inside key caps. `out/cap_masks.npz` has the cap masks.
   - Alternatively, lower `strength` so only the body gets texture.
5. **Compare v1 with v0:**
   - Full view next to `ref_photo_3x.png`.
   - The legend crops.
   - `display_sim.py` output, with `sim_1080p_fit_nearest` as the worst case.
   - Keep whichever reads as more real without losing legibility. **v0 is an acceptable fallback.**

## Delivery, when the skin is chosen
1. Copy the final skin and keymap into `~/Documents/WabbitEmu/` as `TI-84Plus-Official.png` and `TI-84Plus-Official-Keymap.png`. Nothing there gets overwritten.
2. Re-run `verify.py` on those two exact files and paste the summary into this log.
3. Send the user the skin, `overlay_*.png` and a 1080p sim crop with SendUserFile.
4. Tell the user how to load it on the Windows PC: WabbitEmu's Options, Skin tab, custom skin, pointing at both files. The user tests it there; it can't be tested on this Mac.
5. Update this log and the `ti84-wabbitemu-skin` memory to say the skin is delivered.
