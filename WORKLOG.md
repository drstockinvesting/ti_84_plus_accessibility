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
