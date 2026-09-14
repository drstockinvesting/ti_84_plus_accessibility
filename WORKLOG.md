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

13. **CEmu issue 2 done, configuration plus two launchers, still no build (2026-09-12): a selectable detached screen, and a way back.**
   - **The open question from item 12 is resolved: the menus are reachable.** `app_menu` now lists `Apple, CEmu, File, Calculator, Capture, Docks, Debug, Extras`, and **Docks** contains `Variables, Capture, Settings, Console, State, Keypad, Screen, Enable UI edit mode`. The earlier `Apple, CEmu`-only reading was macOS not populating a background app's menu bar, not a hidden menu. **So `ui_edit_mode=false` from issue 1 is not a trap** - it can be switched back on from Docks at any time.
   - **There is a second, always-available route to those menus:** `MainWindow::contextLcd` (`mainwindow.cpp:1853`, wired at `mainwindow.cpp:411`) puts File / Calculator / Capture / Docks / Extras into the **right-click menu on the LCD itself**. That works even with the menu bar hidden, and it works in screen-only mode. It is the escape hatch to document.
   - **Dragging was never the answer.** Re-docking a floating `QDockWidget` means dragging its title bar until Qt shows a drop indicator - a fine-motor, fine-vision task. For this student that is the actual barrier, not the `setAllowedAreas` flag. Issue 2 needs a *command*, not a better drag.
   - **The command that already exists is `FULLSCREEN_LCD`** (`settings.cpp:931`). It reparents `ui->lcd` to the main window, calls `setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)` - which is what releases `lcdAdjust`'s fixed size - then `Qt::Tool | FramelessWindowHint` and `showFullScreen()`, and hands it keyboard focus through `keypadBridge`. `FULLSCREEN_NONE` reparents it back and re-runs `lcdAdjust()`. **Fully reversible by design.**
   - **Measured: 1266 x 950, about 4x linear**, aspect preserved, from a 320x240 source. `[Screen] fullscreen=1` is `PreserveAspectRatio` (0 stretch, 2 integer-only) and `upscale=2` is SharpBilinear, which on this Retina panel scales to an exact 8x integer frame and then smooth-corrects the last 1%. Leave both as they are.
   - **The honest limitation: no on-screen keypad while detached.** The LCD window covers the whole display, so the keypad window is behind it. Screen-only means Mac-keyboard-only. `[Keypad] map=cemu`; `wabbitemu` is also a valid value and may suit this user's muscle memory better.
   - **F11 is the only built-in trigger and it has no menu item** - `m_shortcutFullscreen` (`mainwindow.cpp:504`) cycles `NONE -> ALL -> LCD -> NONE`. On a Mac that means **fn+F11**, twice, undiscoverable. Hence the launchers.
   - **Delivered in `~/Documents/CEmu/`** (nothing there was overwritten; it held only the ROM): `tools/cemu-mode.sh` (`screen` / `keypad` / `toggle`), two double-clickable `.app` wrappers built with `osacompile` - *Big Calculator Screen* and *Full Calculator* - and `HOW TO - Big Screen.txt` in plain large-print language. The mode lives in `[Window] fullscreen` (0 docked, 2 screen-only); **note `[Screen]` has a key of the same name** that means something else entirely, so the script edits only inside the `[Window]` section.
   - **CEmu rewrites its whole config on quit** (`save_on_close=true`), so the config may only be edited while CEmu is not running. The script quits it, waits on the real process, and only then edits and relaunches.
   - **Tested, not assumed: SIGTERM does not save.** Sending `TERM` left both `cemu_config.ini` and `cemu_image.ce` untouched, i.e. it throws away the student's in-progress work. The `pkill` fallback was therefore **removed** from the script - failing to switch modes is the better outcome. A clean quit needs the Apple Event, full stop.
   - **The one unverified step is the launchers' first run.** The `.app` wrappers need macOS Automation permission to control CEmu, and the consent prompt cannot be answered from a background session - the applet just hangs until it is. The shell script itself is verified working in both directions (this session already holds that grant). Tell the user to expect one "wants to control CEmu" dialog and to click OK.
   - **A nested `osascript` inside `do shell script` deadlocks** the applet; the quit has to be native AppleScript in the applet itself. Also `application "CEmu" is running` goes false while the process is still tearing down, which swallowed the relaunch - the script now waits on `pgrep` regardless of who asked for the quit, then settles for a second.
   - **What this still is not:** a proper attach/detach toggle. That wants a menu item calling `setFloating()`, a floating screen that is genuinely resizable rather than pinned by `lcdAdjust`'s `setFixedSize`, and the keypad staying visible beside it. All three need the source build, so they should ride along with issues 4 and 5.


14. **CEmu issue 3 diagnosed and mitigated; the proper fix needs the build (2026-09-12).** In the user's words: *"The keyboard display is off when resizing. The skin approach was much better suited to our needs."*
   - **Reproduced exactly, by writing window sizes straight into the `geometry` blob** (`scratchpad/setgeom.py` on top of `qini.py`) rather than guessing. At 760x900 the screen sits **top-left** with a dark void to its right, and the keypad sits **bottom-centre** with dark bands either side. The calculator visibly comes apart into two pieces. Screenshot pair in `scratchpad/i3_compare.png`.
   - **The cause is two different alignment rules on two widgets that are supposed to read as one object.**
     - Screen: `ui->screenWidget->setFixedSize(...)` in `settings.cpp:lcdAdjust` - a *fixed* size, pinned left, that does not grow with the window at all.
     - Keypad: `keypad/keypadwidget.cpp:322 resizeEvent` builds `mTransform` with `origin.width()` for dx but a **literal `0` for dy** - so it is centred horizontally and anchored to the *top*, and it is aspect-locked to 162:238 so it never fills.
     - They therefore coincide at exactly one window size and nowhere else.
   - **The fitted height is `2.2668 * W`** (screen `351/440 = 0.7977`, keypad `238/162 = 1.4691`). At `scale=90`, `W=396` gives `897`. The display allows `W <= 420`, and W must be `440*scale/100` with scale a multiple of 10, so **scale 90 / 396x897 is the only fit that fits** - scale 100 wants 1025 px of height. The docked calculator genuinely cannot be made larger on this Mac; that is the same ceiling recorded in item 12, arrived at from the other direction.
   - **Not fixable by configuration.** `dy = 0` is compiled in, and nothing recalculates `Screen/scale` from the window size. So this is the issue that forces the build decision.
   - **Mitigation shipped instead:** *Full Calculator.app* now also restores the fitted geometry, via `tools/cemu_fitsize.py` + `tools/cemu_qini.py`. Verified: deliberately broke the window to 820x780, one run of `cemu-mode.sh keypad` put it back to 396x925. The `HOW TO` gained a "do not drag the window bigger" section - dragging cannot make the calculator bigger, only misaligned.
   - **The real fix, for the build, in two steps.** (a) *Cheap:* give `resizeEvent` `origin.height()` for dy and centre `screenWidget`, so the calculator stays coherent at any size. (b) *What the user actually asked for:* drive `Screen/scale` from the window width so the whole calculator scales as one unit on resize. `lcdAdjust()` already derives everything from that single scale, so this is a small hook, not a rewrite - **and it delivers "the skin approach" without a skin**, keeping the vector keypad, which stays crisp at 10x where a PNG would not.
   - **CORRECTION to the "350-600 MB" figure in item 12's section below, which was wrong and was repeated to the user.** That assumed `brew install qt`, the meta-package - 38 sub-formulae including `qtwebengine`, a Chromium build. **CEmu needs only `qtbase`**: `gui/qt/CMakeLists.txt:83` asks for `Qt6 COMPONENTS DBus Gui Network Widgets` plus `Core`, and nothing else - no LinguistTools, no Svg, `LibArchive` and `PNG` are `QUIET`/optional. Measured the real cost by HEAD-ing each Homebrew bottle: `qtbase` 16 MB, `cmake` 20 MB, `ninja` 0.2 MB, plus 25 runtime deps (7 already installed), **74 MB compressed in total**. CEmu.app ships no Qt frameworks or headers - it is statically linked - so there is no way to skip the download entirely, but 74 MB is a different decision from 600 MB.


15. **CEmu issue 3 FIXED in a source build (2026-09-12).** The user chose the Qt build over a Cocoa rewrite once the real download cost was known.
   - **Toolchain: `brew install qtbase cmake ninja`, 74 MB compressed, ~146 MB on disk.** Qt 6.11.2, cmake 4.4.3, ninja 1.13.2. Source: the v2.0 tarball (1.1 MB) plus the two submodules the tarball omits - `zdis` @ `7eb89e5` and `tivars_lib_cpp` @ `f627164`, 0.45 MB together, both fetched at the commits the tag pins (the GitHub contents API reports a submodule's sha).
   - **Two upstream fixes were needed before it would compile at all**, kept in a separate patch (`cemu/qt6.11-compat.patch`) because they are not accessibility work: Qt 6.7 renamed `qt_generate_deploy_app_script`'s `FILENAME_VARIABLE` to `OUTPUT_SCRIPT` and 6.11 rejects the old spelling; and Qt 6.11 added `QWidget::childAt(QPointF)`, which made the braced initialiser at `mainwindow.cpp:973` ambiguous.
   - **The accessibility patch (`cemu/accessible-cemu.patch`, ~190 lines) is three changes:**
     1. `keypadwidget.cpp:resizeEvent` - dy `0` -> `origin.height()`. One line; it is the literal cause of "the keyboard display is off".
     2. `settings.cpp:lcdAdjust` - `screenWidget` goes from `setFixedSize` to `setFixedHeight` + `setMinimumWidth`, and `calcSkinTop`/`lcd` are offset by `(screenWidget->width() - w)/2`, so a window wider than the calculator letterboxes symmetrically instead of pinning the faceplate left.
     3. **New `MainWindow::scaleToWindow()`, called from a new `resizeEvent`** via `QTimer::singleShot(0, ...)` - dock geometry is not final until the event returns. It derives `Screen/scale` from `min(availW/440, availH/997.4)`, so **the whole calculator scales as one object.** That is "the skin approach" without a skin, keeping the vector keypad crisp at 10x. Gated on `Screen/scale_with_window` (default true) and skipped in `FULLSCREEN_LCD`, where the LCD is reparented out and the dock geometry means nothing.
   - **Termination, not a guess:** `scaleToWindow` returns early when the computed percent and the screen dock height already match, so the queued callback converges instead of looping. Verified by measuring CPU against the stock app - both 0.0% idle.
   - **Measured:** window 280x640 -> scale 64 (predicted 63); 340x780 -> 77 (77); 420x954 -> 91 (95, the difference being the display height clamp). At the previously broken 760x900 the calculator is now one centred object. Survived a live resize to full screen (420x947 -> 1512x949) without breaking. Functional check: `9x8 = 72` on the on-screen keypad of the patched build.
   - **Installed as `~/Documents/CEmu/CEmu Accessible.app`, 72 MB, self-contained; `/Applications/CEmu.app` was NOT touched** and remains the fallback. `cemu-mode.sh` now prefers the accessible build and quits **by bundle id**, since both apps are named "CEmu".
   - **`macdeployqt` leaves the bundle's signature broken** ("invalid signature... libbrotlicommon") and arm64 macOS requires a valid one, so the build script re-signs ad hoc with `codesign --force --deep --sign -`. Without that the app will not launch.
   - **A trap worth remembering: `cmd | grep` reports grep's exit status, not the compiler's.** I read one build as successful when ninja had actually failed. Redirect to a log and check `$?` on the command itself.
   - Reproducible end to end with `cemu/build-cemu.sh`; the CEmu source is deliberately **not** vendored into the repo, only the two patches.

16. **CEmu issue 4 DONE (2026-09-12): high-contrast screen colours.** In the user's words: *"The screen color needs to be a black background with stroke colors of white, blue, purple, and green. If more colors are necessary, cool colors get priority while warm colors are deprioritized."*
   - **The model is ink on paper, not a hue rotation.** A TI pixel is some coverage of one coloured ink laid over a white field, so `alpha = 1 - min(r,g,b)/255` and the ink itself is the pixel un-mixed from that white: `ink = (pixel - 255)/alpha + 255`. By construction `min(ink) == 0`, so an ink carries exactly two free numbers, hue and value, and black ink falls out as value 0. Output is `alpha * newInk` over black. **Coverage surviving the remap is what makes this work:** the white field goes to true black, and an anti-aliased edge stays faint instead of blooming into a bright ring around every glyph. A plain lightness inversion does not have that property.
   - **Hues snap to seven cool anchors; they are not interpolated.** red->pink 325, orange->indigo 248, yellow->aqua 163, green->green 138, ltblue->cyan 190, blue->blue 222, magenta->purple 277. Interpolating looks tidier but the table is deliberately **not monotone** - red sits beside magenta at one end and orange at the other - so a smooth sweep between two anchors passes straight through a third anchor's colour. That is not theoretical: brown, whose hue is 9 degrees off orange's, interpolated to within 12 degrees of navy and came out indistinguishable from it.
   - **Folding 360 degrees of source into 190 degrees of cool costs separation, so it is spent where it does least harm.** TI-OS assigns Y1..Y5 blue, red, black, magenta, green, and those are the ones that must be told apart at a glance: after the remap their closest pair is **72 apart in RGB**. Orange and brown - the two warmest, and the two the user explicitly deprioritised - end up 38 apart.
   - **Every ink is held above 4.5:1 against black**, by raising HSV value first and desaturating only when there is no headroom left, so the hue survives as long as possible. 7:1 was the first target and had to be abandoned: blue is intrinsically dark, and forcing it that high desaturated navy and blue into the same colour. Measured: black ink 21:1, green 15:1, ltblue 15:1, yellow 17:1, red/pink 9.2:1, blue 8.8:1, magenta 8.2:1, orange 6.7:1, navy 5.1:1, brown 5.0:1.
   - **Two bugs found by measuring the result on screen, neither visible by reading the code.**
     1. **Un-mixing makes every ink fully saturated by construction**, so a source that was only faintly tinted came back at full strength. The panel gives each channel its own gamma curve (`panel.c`, PVGAMCTRL/NVGAMCTRL), so a backlit white is never exactly neutral. Saturation now ramps in with the source's own chroma via a smoothstep over `(hi-lo)/hi`.
     2. **The whole background turned saturated purple after two minutes.** In the DMA path `panel.c:482` bakes `backlight.factor` into the gamma table, so TI-OS dimming the screen on idle hands the GUI a tinted grey where white used to be - and grey backgrounds are the exact thing this remap exists to remove. The dimming is now undone before the lookup and re-applied after, which leaves the screen fading **toward black** instead of toward grey.
   - **Cost: 1.8 points of CPU against a 25% baseline** (26.8% vs 25.0%, same binary, palette on vs off, six samples each). A 64K-entry table keyed on the RGB565 form makes the per-pixel work a single load; the 565 index is exact, because `lcd_argb8888out` widens 565 by bit replication.
   - **Three settings, and it is reachable without the menu bar.** Original / High Contrast (cool on black) / High Contrast (white on black), stored as `Screen/palette`, defaulting to **1** in this build. In the settings dock under "Display", and also as a **Screen Colors** submenu on the LCD right-click menu - the one menu that still works with the menu bar hidden and in screen-only mode.
   - **Verified on screen, not by inspection.** Graphed Y2=2X, Y3=3X, Y4=4X, Y5=5X and sampled the window pixels: graph field exactly `#000000`; axes and tick marks (TI BLACK) `#FFFFFF`; GREEN `#00E269`, RED `#FF75CD` pink, MAGENTA `#DB7AFF` purple - all distinct and bright. Toggling the setting to 0 and relaunching restored the original white field, which also proves the setting persists across CEmu's save-on-close. Comparison image: `cemu/issue4-screen-colors.png`.
   - ~~**Known cosmetic consequence, left as-is:**~~ **fixed the next day, see item 17.** TI-OS draws its status bar as a *dark* bar with white text, so the inversion made it the brightest large area on the screen - `#515551` before, `#737373` after (4.4:1 against black). The "about 8% of the display" figure here was an estimate and was **too small**: measured against the pixels, the bar is **30 rows of 240, an eighth of the screen**.

17. **CEmu issue 4b DONE (2026-09-13): the status bar is left dark instead of inverted.** The user's instruction was *"darken the status bar"*, taking up the item flagged at the end of item 16.
   - **The bar is the one thing TI already draws the right way round.** Everything else on the CE screen is dark ink on a white page, which is what the ink-coverage model in item 16 is for. The status bar is the inverse - a dark field with light glyphs - so inverting it does exactly the wrong thing twice over: it turns a full-width, 30-row band (**an eighth of the display**, sitting above everything the student reads) into the brightest large area on screen, and it knocks that band's own text down from white to 4.4:1.
   - **So the fix is not to darken the bar as a special case; it is to stop inverting it.** A second table drives output from the mark's **brightness** rather than from ink coverage. The bar's field falls below the floor and goes to true black; its glyphs carry their own brightness through unchanged. Measured: field `#000000`, glyphs `#FFFFFF`, **21:1, up from 4.4:1** - the bar is now *more* legible than before, not merely dimmer.
   - **A glyph is anything standing clear of the field by brightness *or* by colour.** Gating on brightness alone was the first attempt and it dimmed the battery and low-battery icons - which are solid marks, not half-covered ones - to 4.8:1 and 3.8:1, the second of those below the floor the rest of the display holds to. Adding the chroma gate puts them back at 7.6:1 and 4.9:1 in the model, 4.7:1 for the battery as measured on screen.
   - **Both tables now share `inkOver()`, which is item 16's tail verbatim**, so the hue anchors, the 4.5:1 floor and the chroma guard are unchanged and **the inverting map's output is bit-identical to before**. That is provable rather than hopeful: the un-mixed ink's HSV value is exactly the `(hi-lo)/(255-lo)` the old code computed, since `ink_hi = 255*(hi-lo)/(255-lo)`.
   - **The band is detected per frame, not assumed.** It is hidden on some screens and a full-screen program owns the top of the display outright. A band still running after 64 rows is taken to be a dark screen rather than a bar and is left alone.
   - **The detector's first version failed on screen, and the reason is worth keeping.** It probed the three leftmost pixels of each row, on the assumption that a full-width bar has a clear left margin. **TI-OS starts the bar's text hard against the left edge**, so the probe read the `N` of `NORMAL` and the feature silently did nothing. The fix is to take the most common colour along a sparse sample of the top row, with a **stride of 7 - coprime with the 8-pixel character cell** - so the sample cannot land on the same part of every glyph and mistake the text for the field.
   - **Verified on the MODE screen, which is the hardest case**, because there the bar meets a white page directly and a detector that over-ran the bar by even a few rows would paint a blinding white band. Bar field `#000000`, bar glyphs `#FFFFFF` (21:1), battery green 4.7:1, page below `#FFFFFF` text on `#000000` - and **no seam**, because the bar's black and the inverted page's black are the same black. Comparison image: `cemu/issue4b-statusbar.png`.
   - **Two things the verification itself taught.** The calculator **auto-powers-off** while a session is idle, and CEmu then draws its own centred "LCD OFF" straight in `paintEvent`, bypassing the palette entirely - a capture taken then looks like a catastrophic failure and is not one. And **idle backlight dimming scales the whole frame**, so a screen sampled without waking it first reads every white as `#616161`; the axes and the bar's glyphs matching each other exactly is the tell that the frame is dimmed rather than broken.
   - **Still open, and deliberately not touched:** a program that draws a genuinely **dark full-screen** page still gets inverted to bright, which for this student is the same glare problem in a different place. Nothing in TI-OS does it, so there is no way to test a fix against real content yet.

18. **CEmu issue 5 DONE (2026-09-13): the press-and-hold magnifier.** *(The screen no longer magnifies with the keypad - see item 20.)* The user's wording: *"A long-click zoom-in/out feature as we have created previously"* - the one from `macemu/`, 4x, 550 ms.
   - **Press and hold the SCREEN for 550 ms and the whole calculator goes to 4x** - screen and keypad together - centred on the pointer and following it. Hold again, press Escape, or use **Extras > Magnifier** or the screen's right-click menu to come back.
   - **The gesture is deliberately not on the keypad, and this was a real fork, not a detail.** `macemu` could put it anywhere because it only ever enqueued a key for six frames. **CEmu delivers real press and release to the emulated hardware, so holding a key repeats it the way a physical TI-84 does** - and a long press on a key would have to take that away. For a low-vision student, pressing an arrow once per step across a long expression is a bad trade, and aiming at a small target repeatedly is worse for her than for anyone else. **Put to the user, who chose key repeat**; the screen carries the gesture for both panels, which is fine because it is the biggest target on the window and has no keys on it.
   - **One shared `Magnifier` drives both widgets**, because the calculator is a single object to the person using it. Each keeps its **own centre**: the screen parks on what was being read while the pointer goes down to the keypad, and **returns to where the zoom started when the pointer leaves**, rather than being dragged to whichever edge it left by - without that, she would come back to a blank margin.
   - **The transform is Apple's pointer-centred mapping**, origin at `c * (1 - 1/Z)`. The source point under the pointer is exactly `c`, so the whole widget stays reachable, the magnified rect never leaves the bounds, and - the part that actually matters - **hit testing needs no inverse anywhere**. Neither the keypad's key lookup nor the LCD's drag targets were touched. Verified on screen at 4x: clicked where `8` sits *unmagnified*, and an `8` appeared.
   - **It shares the press with the existing screenshot drag** rather than displacing it: moving cancels the hold and the drag proceeds as before; holding still cancels the drag and toggles the zoom.
   - **Three latent problems it surfaced, all fixed.** (a) `m_screenshotDrag` was set on press and only ever cleared by a *completed* drag - harmless while moves arrived solely with a button down, but the magnifier turns mouse tracking on, so a flag left standing after a plain click would have had the next hover start a drag with no button held. (b) The keypad routes `MouseMove` straight into `mouseUpdate()`, which presses whatever is underneath - with tracking on, **hovering would type**. (c) `updateKey()` repaints one key's mapped rect, which is not where the zoom put it.
   - **Escape is claimed through an application filter installed and removed with the state**, not a shortcut. It is also the calculator's own quit key, so it can only be borrowed while the magnifier is up; and none of these widgets takes focus on click, so a per-widget filter never saw it - which is exactly how the first attempt failed.
   - **The faceplate does not magnify, only the screen and the keys.** They are separate widgets with separate origins, so magnifying the bezel about the pointer would slide it out of register with the screen sitting in it. Leaving it still reads as a frame, which is the better outcome anyway.
   - **Two harness traps worth remembering for any future on-screen test.** `left_mouse_down` *with a coordinate* warps and presses in one step, and the press lands before the widget sees the move - a long press tested that way silently does nothing. Move first as its own action. And **synthetic key events never reach the app while Claude's own window holds keyboard focus**, which is why Escape appeared broken through the display tools and worked immediately through the accessibility path.
   - Comparison image: `cemu/issue5-magnifier.png` (the keypad, normal and at 4x).


19. **CEmu issue 2 DONE (2026-09-13): attach/detach the screen, by command.** The user's wording: *"I like having a resizable detached screen as a selectable feature. Right now, the screen is detached with no way to attach it."* The config stopgap from item 13 is now backed by a real toggle.
   - **`Extras > Detach Screen`, and the same entry in the screen's own right-click menu.** The context menu is the one that matters: detached, with the menu bar hidden, it is the only control still in reach. Both show the check state.
   - **Why a command and not a drag.** `DockWidget::setState()` sets `NoDockWidgetArea` whenever UI edit mode is off, which is how this build ships so the student cannot pull the calculator apart by accident - and a screen that is already out then has nowhere to be dropped. The toggle opens the areas for the duration of the move and hands control straight back to `setState()`, so the drag handles stay off.
   - **"Resizable" meant un-pinning everything, and each pin failed differently.** A *minimum* is a floor the floating window cannot be dragged below; a *fixed height* is a ceiling it cannot be dragged above. Detached, the size leads and `scaleToWindow()` derives `Screen/scale` from it, with the artwork centred in whatever it is given. A floor of 25% stays, because the widget positions its children by hand and so has no size hint of its own - **at zero the first detached window collapsed to nothing and was invisible on screen.**
   - **The third pin was not a size at all.** `screenDock` is marked `expandable=false` in the .ui, and `DockWidget::updateExpandability()` turns that into a `QSizePolicy::Maximum` *vertical* policy - "the hint is a ceiling". Docked that is right, and is what stops the screen stealing the keypad's height. Detached it is why the window would **widen but flatly refuse to grow taller**. `setExpandable(detached)` is the dock's own knob for it.
   - **The detached screen is a real window, not Qt's float.** `QDockWidget` floats a dock as a `Qt::Tool` window, and **macOS hides tool windows whenever their application is not the active one** - so the thing the student is reading vanished the moment she clicked a browser. The flags have to be set inside `DockWidget::setState()`, because `setFeatures()` re-derives them for a floating dock: flags set at the call site are thrown away, and `setup()`'s trailing `setUIDockEditMode()` was undoing them on **every launch that started out detached** (the first fix looked right and silently did nothing after a restart).
   - **Its close button re-attaches** rather than losing the screen - the complaint behind the issue, in the one place someone would reach for it.
   - **Three quit bugs, found only because the detached screen is a second top-level window.** (a) `MainWindow::closeEvent` now hides a floating screen, or the main window closing leaves the application alive with nothing but a screen. (b) The re-attach on close is deferred *and tests `m_shutdown` when it fires*, because quitting closes the dock's window **before** `closeEvent` has set that flag - testing at emit time put the calculator back together mid-quit **and ate the quit**, which is why `osascript ... to quit` kept coming back `User canceled (-128)`. (c) `DockWidget::closeEvent` re-centres at `minimumSize()` only a dock that *was docked*; one already floating has a position and size the user chose, and closing all windows on quit was throwing them away, so **the detached screen came back smaller after every quit**.
   - **The keypad keeps the main window to itself**, given back the height the screen was using; attaching restores it. Verified: keys pressed in the keypad window render on the detached screen, the magnifier still zooms both windows at once (only the keypad zooms as of item 20), and issue 3's scale-with-window is unaffected.
   - **Deploy lesson, cost an hour.** Copying just the rebuilt binary into an already-deployed bundle is not enough: it still names Homebrew's Qt by absolute path, so **two sets of Qt frameworks load and the app dies with "Could not load the Qt platform plugin cocoa"**. `macdeployqt` has to run over the whole bundle every time. That, the arm64 re-sign and the quit-relaunch cycle are now one script, `tools/cemu-deploy.sh`.
   - Comparison image: `cemu/issue2-detach.png` (attached, then the two windows detached).
   - **`pgrep -f "MacOS/CEmu"` was matching this session's own shell loops**, which made the launcher scripts refuse to act on a CEmu that was not running. Both scripts now use `pgrep -x CEmu`, and both re-send the quit while waiting, since CEmu's quit is asynchronous - `closeEvent` starts the calculator-image save and ignores the close, and only the save finishing calls `close()` again. Still **no `pkill` fallback anywhere**: SIGTERM loses the student's work.

20. **Magnifier narrowed to the keypad (2026-09-13), and shipped to Windows too.** The user's instruction: *"Only the keypad should zoom, not the screen. This zoom action should apply to both the MacOS version ... and the Windows version."* This supersedes the "screen and keys together" behaviour in item 18.
   - **Why it is the right call and not just a preference.** The screen has two size controls of its own by now - it grows with the window (item 15) and it goes out into a window the size of the display (item 19). The keys have neither: they get whatever height is left after the screen has taken its share. Zooming the screen 4x on top of those only crops away the line being read, which for this student is the opposite of help.
   - **The gesture does not move.** Still a long press on the SCREEN. The keypad must keep delivering real press and release events for key repeat, so a long press cannot live there; and starting it from the screen means the hand is never over the thing that is about to change size.
   - **What that let us delete.** `LCDWidget` keeps only the hold timer. Gone: the transform, the amber frame, its own centre and home point, the `activeChanged` handler, `recentre()`, and the mouse tracking that existed solely to follow the pointer - so the screen now repaints no differently than it did before the magnifier existed. `KeypadWidget` is untouched. **The `m_screenshotDrag` fix from item 18 stays** even though tracking is off again: clearing it on release is correct either way.
   - **Both menu entries now read "Magnify Keypad"**, in Extras and in the screen's context menu. The gesture starts on the screen, so a label that just says "Magnifier" now points at the wrong thing.
   - **The keypad's zoom centre is now clamped to the keys.** The drawing keeps its aspect ratio and is centred in whatever the layout gives the widget, so a window wider than the calculator has dead space either side. Starting the magnifier from the menu with the pointer parked out there filled the entire magnified view with empty background - survivable while the screen magnified alongside it, useless once the keypad is all there is. Caught on screen, not in review.
   - Comparison image: `cemu/magnify-keypad-only.png` (keys at 4x, screen untouched above them).
   - **Windows is built by GitHub Actions, not on this Mac.** `.github/workflows/build-windows.yml` in `drstockinvesting/cemu-accessible-build` builds MSVC x64 against Qt 6.6.3 and uploads a `windeployqt`-packaged `dist/` as the `CEmu-Windows-x64` artifact; a push to `main` is the trigger, so the same source commit produces both editions. Download is `gh run download`, about 3 MB for the exe.
   - **The CI build arrives unsigned, and is re-signed here.** GitHub Actions does not sign anything, so the artifact's exe has no Authenticode signature and Windows calls it an unknown publisher even with `CEmu-Accessible.cer` installed.
   - **Where the signing key actually was, which cost a search.** `CEmu Windows/` holds only the *public* `CEmu-Accessible.cer`, and nothing under `~/Documents` or `~/Downloads` matched `*.pfx`/`*.p12` - the first conclusion was that the key only existed on the Windows side. It did not: Antigravity had generated the certificate on this Mac and left the whole set in its own working directory, `~/.gemini/antigravity-ide/brain/<session-uuid>/scratch/`. **Lesson: search the whole home directory for key material, not just the document tree - an IDE agent's scratch space is a real location.**
   - **The key now lives in `~/Documents/CEmu/codesign/` (mode 700), moved there the same day.** A per-session scratch folder belonging to another tool is no home for the one thing in this project that cannot be regenerated: if it were cleaned up, every future build would need a brand-new certificate and the student would have to install a new `.cer` by hand. The folder holds `cemu_codesign.key` (unencrypted PEM, mode 600 - this is the secret), `cemu_codesign.crt`, `cemu_codesign.pfx` (password-protected and the password is not recorded, so the PEM pair is the usable one), `codesign.cnf`, and a `README.txt` carrying the signing command and the certificate's serial and fingerprint. It sits deliberately **outside `CEmu Windows/`** so it is never handed to the student, and `~/Documents/CEmu` is not a git repo - only `src/CEmu-2.0` within it is - so neither repo can pick it up. The copies were verified byte-identical and test-signed from the new path *before* the originals were deleted; `find ~ -iname "*.pfx" -o -iname "cemu_codesign.*"` now returns that folder and nothing else.
   - **Signing on macOS, no Windows and no signtool needed.** `osslsigncode` (Homebrew, already installed) signs a PE with a PEM key/cert pair, matching what was applied before:
     ```
     cd "$HOME/Documents/CEmu/CEmu Windows"
     osslsigncode sign \
       -certs "$HOME/Documents/CEmu/codesign/cemu_codesign.crt" \
       -key   "$HOME/Documents/CEmu/codesign/cemu_codesign.key" \
       -h sha256 -n "CEmu Accessible" \
       -i "https://github.com/drstockinvesting/cemu-accessible-build" \
       -ts "http://timestamp.digicert.com" \
       -in CEmu-unsigned.exe -out CEmu.exe
     ```
     `osslsigncode verify` then reports *Signature verification: ok* and *Timestamp Server Signature verification: ok*. The `-CAfile` argument wants **PEM**; pointing it at the DER `CEmu-Accessible.cer` fails with "no certificate found", which looks like a bad signature and is not.
   - **Keep `CEmu-unsigned.exe` as the pristine CI output.** Signing is not idempotent - re-signing an already-signed exe appends or replaces a certificate table - so every signature is applied to the unsigned copy and written out as `CEmu.exe`.
   - **The timestamp matters more than the signature here.** The certificate is self-signed and expires in 2036; an RFC3161 countersignature from DigiCert (a few KB over the wire) means the signature still validates after that date. The cert the student installs is unchanged - same serial `3101...9673`, same SHA1 `FA:29:1D:13:...:0B:60` - so `Install-Certificate.bat` did not need reissuing.


21. **CEmu now runs in a browser, as WebAssembly (2026-09-14).** The user's instruction: *"The exe is a problem that I am trying to get whitelisted with the tech department. In the meantime, could I host this emulator on Cloudflare and have the user supply the ROM file to avoid licensing issues?"* Answered directly: **`.exe` is the wrong filetype for web access** - a Windows PE cannot run in a browser - and WASM dissolves the whitelisting problem instead of routing around it. The signed-exe track is untouched and stays as the fallback.
   - **Upstream already had a wasm target, and it had rotted.** `core/emscripten.mk` and `core/os/os-emscripten.c` shipped with CEmu 2.0 but had not been built in years. Two separate breakages: `core/asic.c:117` calls `gui_handle_reset()` and the emscripten shim never defined it, so the link failed outright (the plain-C implementation in `gui/sdl/main.c:32` is the one to copy); and the 2024-era emcc flags are gone - standalone `.bc` objects, `--memory-init-file`, `EXTRA_EXPORTED_RUNTIME_METHODS`. **Finding the target already there is what made this a day's work instead of a month's.**
   - **`EMSCRIPTEN_KEEPALIVE` is not enough under `-flto`,** which will strip a keepalive symbol nothing internal references. `emu_save`/`emu_load` were never marked at all, and without those two there is no way to persist the student's work - so the exports are now listed explicitly in `EXPORTS`.
   - **That export list must stay on one line with no spaces.** make passes the recipe to the shell verbatim; a backslash-continued list arrives with spaces, the shell splits it, and emcc reads the first fragment as a finished list: `error parsing "-s" setting "EXPORTED_FUNCTIONS=_main,_malloc,_free,": empty value in string list`. Cost one red CI run; every C file had already compiled.
   - **`EXPORT_ES6` is deliberately off.** The core's `EM_ASM` blocks assign undeclared globals (`emul_is_inited = true`), which an ES module's strict mode turns into a ReferenceError at the exact moment the ROM finishes loading. The core loads as a classic script; our own frontend is still a module.
   - **The framebuffer needs a channel swizzle.** The core draws RGBA8888 as 32-bit words - `0xAARRGGBB`, confirmed by `QImage::Format_RGB32` in `lcdwidget.cpp:30` and `SDL_PIXELFORMAT_RGB888` in `gui/sdl/main.c:96` - which on a little-endian machine is **B,G,R,A** in memory. Canvas `ImageData` wants R,G,B,A. Blitting the heap straight through renders the whole calculator with red and blue swapped, and looks almost plausible. Tested deliberately with a stub core drawing known red/green/blue bands. **This is also exactly where the issue-4 palette LUT hooks in.**
   - **`panel.display[PANEL_NUM_COLS][PANEL_NUM_ROWS]` reads like a transposed buffer and is not.** `PANEL_NUM_ROWS` is 320 and `PANEL_NUM_COLS` is 240, so the array is `[240][320]` - ordinary row-major, 320 wide. `emu_lcd_drawframe()` is still the better API than `lcd_get_frame()` because it covers the non-DMA path too.
   - **The keypad was generated, not retyped.** `layout.py`'s `KEYS` table - written and hardware-verified during the WabbitEmu work - carries every legend, second- and alpha-label and matrix position. WabbitEmu numbers the matrix the other way up from CEmu:
     ```
     CEmu row = 7 - Wabbit group        CEmu col = Wabbit bit
     ```
     `web/tools/gen-keypad.py` applies that and emits `web/src/keypad-layout.js`. **All 50 slots were then cross-checked against CEmu's own `gui/sdl/keymap.c`: exact match, nothing missing, nothing extra.** Worth the effort because a transposed row/col is silent - it produces a wrong but plausible character, the failure most likely to reach the student unnoticed.
   - **The browser is an upgrade, not a compromise, for this student.** The Qt keypad is vector-painted and invisible to assistive technology; these are real `<button>`s with `aria-label`s, so browser zoom, OS magnification and screen readers work without us writing anything. `image-rendering: pixelated` keeps the LCD hard-edged at 10x, which matters more than smooth scaling here. Issue 3 (scale with the window) stops being C++ and becomes CSS. Ctrl/Cmd-+ is deliberately left alone by the key handler.
   - **Built in CI, never on this Mac.** emsdk is about a gigabyte and the connection may be metered, so `.github/workflows/build-web.yml` builds it on GitHub's bandwidth and pushes the result to the `web-deploy` branch. Cloudflare Pages only *serves* - build command empty, output directory `web/dist`. Putting emsdk inside the Pages build image instead is the fragile arrangement: a 20-minute limit and a toolchain install on every deploy.
   - **Payload: 265 KB for the whole site, of which 153 KB is the wasm and 86 KB the JS glue.** Cloudflare applies brotli on top. This is lighter than a single photo and should pass a school network without comment.
   - **The ROM never touches Cloudflare.** `os-emscripten.c`'s `main()` calls `emu_load(EMU_DATA_ROM, "CE.rom")`, reading from the in-browser Emscripten filesystem - **BYO-ROM is the design upstream intended**, not a workaround. The student picks their own file; it is cached in IndexedDB so they are not asked twice, and is never uploaded, committed or served. GPLv3 means the page carries a visible source link.
   - **Verified end to end against the real wasm and the real ROM**, served locally: boots to *TI-84 Plus CE 5.3.1.0058 / RAM Cleared*; `5` `+` `3` `ENTER` gives `8`; holding LEFT repeats across `12345` rather than moving one column, proving press *and* release reach the core; on-screen and physical keys produce identical matrix coordinates; and after a reload both the stored ROM **and the full calculator state** come back, cursor included. The test ROM copy was deleted afterwards.
   - **State persistence carries the desktop's hardest lesson.** The deploy script refuses to SIGTERM CEmu because that loses the student's work; a browser tab closes far more casually than an app quits, so `emu_save(EMU_DATA_IMAGE)` runs on a timer, on `visibilitychange` and on `pagehide`, into IndexedDB. A corrupt or stale image is caught and ignored rather than blocking the boot.
   - **Still to do:** the user connects Cloudflare Pages to the repo themselves (Git integration, production branch `web-deploy`) - **credentials are theirs to handle, never mine**. Then someone loads the `pages.dev` URL from a school device, because the filter may block it the same way it blocks GitHub; a custom domain is the fix if so. Only after that is confirmed do the five accessibility features get rebuilt in the DOM (Milestone 2).

## Facts from the CEmu source (CE-Programming/CEmu, tag v2.0)
Read without cloning - about 250 KB fetched from `raw.githubusercontent.com` and the contents API. Copies are in the session scratchpad under `cemu/`.

- **The UI is Qt dock widgets.** `screenDock` and `keypadWidget_dock` are separate `DockWidget`s inside one `QMainWindow` (`gui/qt/dockwidget.cpp`, `settings.cpp:setUIDocks`). "Two windows" means the screen dock was dragged out and floated; it is not a second program.
- **`DockWidget::setState()`.** With UI edit mode *off* it calls `setAllowedAreas(Qt::NoDockWidgetArea)`, so a dock that happens to be floating at that moment can never be dropped back in. With edit mode *on*, any stray drag pops the screen out. There is no attach/detach command anywhere - only dragging. **Correction from issue 2 (item 13): this is not what stranded the screen.** Edit mode defaults to *on*, so the docks were re-dockable all along; what defeated the user was the drag itself. ~~The fix is a command, and `FULLSCREEN_LCD` is the one CEmu already has.~~ **The fix is a command, but not that one - see item 19.** `FULLSCREEN_LCD` reparents the LCD into a borderless full-display window and is a separate feature; detaching is `setFloating()` on the dock. `setState()` is also where the detached screen's window flags have to be applied, since `setFeatures()` re-derives them.
- ~~**The menu bar defaults to hidden**~~ - **wrong, and disproved in item 13.** `menubar` is a *hide* flag and is `false`, so the menu bar is shown; File / Calculator / Capture / Docks / Debug / Extras are all present. On top of that, `MainWindow::contextLcd` mirrors those menus into the LCD's right-click menu, so they are reachable even when the menu bar is hidden.
- **The LCD is a fixed-size widget, not a scaling one** (`settings.cpp:lcdAdjust`): `ui->lcd->setFixedSize(...)` and `ui->screenWidget->setFixedSize(...)`, driven by a percent slider (`Screen/scale`). Resizing the window does **not** grow the screen. That is the real content of issue 3. **Item 19 adds the other half:** detached, every one of those pins has to come off, and `expandable=false` turns into a `QSizePolicy::Maximum` vertical policy that is a fourth pin hiding in the .ui.
- **The keypad is vector-drawn, not a bitmap skin** (`keypad/keypadwidget.cpp`; base rect 162x238, `mTransform` built with `Qt::KeepAspectRatio`). For a 10x magnification user this is *better* than a PNG skin - it stays crisp at any size. Its actual flaw is that `resizeEvent` centres horizontally only (`origin.width(), 0`), so the keypad top-anchors and leaves dead space below.
- **Issue 4's insertion point, confirmed and now used (item 16):** `LCDWidget::draw()` in `gui/qt/lcdwidget.cpp` calls `emu_lcd_drawframe(m_renderedFrame.bits())` into an RGB32 QImage. A palette LUT applied straight after that recolours every frame. Since the CE panel is RGB565 there are only 65,536 source colours, so a precomputed 256 KB lookup table makes the per-pixel cost a single load. **Not reachable by configuration** - it needs a source build.
- ~~**Issue 5** lands in `LCDWidget::paintEvent` plus `KeypadWidget::paintEvent`~~ - **done, item 18.** Both were the right places; the screenshot-drag conflict was real and is shared rather than displaced. What this note missed is that the keypad's key repeat comes from real press/release reaching the emulated hardware, which is what kept the gesture off the keypad.
- **The geometry ceiling, which matters more than any of the above.** With the skin on, the screen widget is `440x351 * scale` and the keypad's natural aspect is 162:238. Stacked at the same scale the calculator wants about **440x997**, against a usable display of roughly **1512x950**. A *magnified* screen and a full keypad therefore **cannot both fit in one window on this Mac**. They coexist only at a moderate scale, because the screen is fixed-size and the keypad takes what is left: at `Screen/scale = 160` the window is about 704x982, holding a 512x384 LCD and a 286-px-wide keypad. **So issues 2, 4 and 5 are not extras - they are how the student gets past the size ceiling.**
- **The core is Qt-free C** - `core/`, about 400 KB across ~50 files, with its own `Makefile`. That keeps a second option open: a Cocoa frontend over CEmu's core, reusing `macemu/`'s window, skin, keymap and magnifier, with no Qt download at all. It would discard CEmu's UI, debugger and file-sending.
- Building the Qt frontend here needs Qt 6, cmake and ninja; Homebrew has none installed. ~~roughly **350-600 MB**~~ - **that estimate was wrong, see item 14.** It assumed `brew install qt` (38 sub-formulae, including `qtwebengine`). CEmu needs only `qtbase`, and the measured total is **74 MB compressed** (qtbase 16, cmake 20, ninja 0.2, plus 25 runtime deps of which 7 are already installed). Xcode command line tools and clang 21 are present. Still **state the size before starting** - see the `metered-connection` memory.

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
