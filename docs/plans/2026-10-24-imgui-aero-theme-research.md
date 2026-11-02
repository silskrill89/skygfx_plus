# ImGui Aero / Big Bento Theme Research

**Date:** 2026-10-24  
**Status:** Research + implementation spec. No production code changed.  
**Target:** `src/core/debugmenu_ui.cpp`, `external/imgui`, `imgui_impl_dx9`, ImGui **1.92.9 WIP**, D3D9 game hook.  
**Brief:** Windows Vista Aero glass + Winamp Big Bento density + a restrained 2026 update, with GTA San Andreas **taxi yellow** as the main accent against dark glass.

**TL;DR**

- Copy Aero's *grammar*, not its 2006 cosplay: tinted glass chrome, 1px inner highlight, top specular band, soft edge, glowing state changes. Drop the heavy bevels, giant gradients, and blue-everywhere.
- Blur only the chrome, never the content. Real backdrop blur is unavailable/unwise in this D3D9 overlay; fake it with a high-opacity tint, two-stop gradient, 3% noise, and a 1px light border.
- Use Big Bento for *layout*: one shell, left nav rail, stacked panels, dense list rows, LED/status strip, color themes. Do not copy its 9px fonts or brushed-metal bevels.
- The accent is the real carcols taxi yellow `#D78E10` (carcols index 6). It passes 6.5:1 on the proposed dark surface, so it can be used unchanged for active states; lighten it for hover.
- The single biggest correctness risk is **device reset**: the DX9 backend's font texture and vertex/index buffers are `D3DPOOL_DEFAULT`, and this repo's `Reset` hook currently never calls `ImGui_ImplDX9_InvalidateDeviceObjects()`. That can make `IDirect3DDevice9::Reset` fail and leave the menu with dead resources.

---

## 0. Ground truth in this repo

| Fact | Evidence |
|---|---|
| The unified menu is `DrawUnifiedDebugMenu(IDirect3DDevice9*)`. | `src/core/debugmenu_ui.cpp:3397` |
| It opens at 1280x760 and clamps to >= 1000x620. | `src/core/debugmenu_ui.cpp:3419-3424` |
| It currently uses a standard ImGui title bar and a top tab bar. | `src/core/debugmenu_ui.cpp:3424`, `:3457` |
| There are 9 top tabs: Main, All Settings, Vehicle, Vegetation, PostFX, Utility, Profiler, Presets, INI Keys. | `src/core/debugmenu_ui.cpp:3469-3579` |
| No custom style is applied. `EnsureImGuiInit()` only creates the context, enables keyboard nav, enables the software cursor, and initializes the backends. | `src/core/debugmenu_ui.cpp:392-410` |
| Fonts are the ImGui default (ProggyClean, 13 px). No TTF is loaded. | no `AddFont*` call in `src/` |
| The build compiles **`E:/SDKs/imgui-master`**, not the vendored `external/imgui`. Both report 1.92.9 WIP. | `premake5.lua:21-23`, `E:/SDKs/imgui-master/imgui.h:32` |
| The DX9 backend captures/restores a `D3DSBT_ALL` state block on every `RenderDrawData`. | `external/imgui/backends/imgui_impl_dx9.cpp:206`, `:311` |
| The backend uses straight alpha (`SRCALPHA` / `INVSRCALPHA`), plus a separate alpha blend. | `external/imgui/backends/imgui_impl_dx9.cpp:117-123` |
| The backend's font/user textures are created as `D3DPOOL_DEFAULT`. | `external/imgui/backends/imgui_impl_dx9.cpp:347-352` |
| Device reset is hooked, but ImGui is not invalidated. | `src/render/depthhook.cpp:329`; `src/render/postfx.cpp:2264` |
| The menu is toggled with **Ctrl+4**. | `src/core/main.cpp:3610-3623` |
| `io.IniFilename` is not disabled, so ImGui may write `imgui.ini` into the process working directory. | no assignment in `EnsureImGuiInit` |

This is the same device-reset gap already flagged as risk #4 in `docs/plans/2026-10-24-imgui-settings-coverage.md`. The theme work is a good time to fix it.

### Design consequences

1. The theme must work on **ImGui 1.92.9**, not 1.89/1.90 tutorials. `AddRect` has a new argument order, `ImTextureID` is `ImU64`, and `AddImage` takes `ImTextureRef`.
2. The menu is large and dense; Big Bento's layout vocabulary fits better than a generic "settings cards" dashboard.
3. The game is the background. Any translucent surface has unpredictable contrast; design for the worst case (bright sky, fire, headlights), not the best case (night).
4. The existing `D3DSBT_ALL` state block means procedural `ImDrawList` work is safe; custom D3D state changes inside callbacks are not.

---

## 1. Vista Aero, dissected for imitation in a D3D9 overlay

### 1.1 What Aero actually is

Aero Glass is the Windows Vista/7 non-client window frame rendered by the Desktop Window Manager (DWM). The signature is not "a blurry rectangle"; it is a composite of several layers:

1. **Backdrop blur** — DWM blurs the desktop content behind the window's glass region, using the window's alpha to decide where glass applies ([Microsoft DWM Blur Behind overview](https://learn.microsoft.com/en-us/windows/win32/dwm/blur-ovw), [MSDN Magazine: Aero Glass](https://learn.microsoft.com/en-us/archive/msdn-magazine/2007/april/aero-glass-create-special-effects-with-the-desktop-window-manager)).
2. **Colorization tint** — the user's system color is mixed into the glass ([DwmGetColorizationColor](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmgetcolorizationcolor)).
3. **Saturation/contrast lift** — the blurred backdrop is made more vivid so it reads as glass, not gray mud.
4. **Reflection/specular layer** — the polished "Aero stripes" / aurora reflection and the bright top edge. DWM theme resources and community Aero reimplementations call this the reflection/glass layer ([OpenGlass](https://github.com/ALTaleX531/OpenGlass), [Aero Glass changelog](https://www.softpedia.com/progChangelog/Aero-Glass-Changelog-229994.html)).
5. **1px inner highlight** — a bright inner edge that makes the glass look thick.
6. **Outer border + soft outer glow/shadow** — a dark 1px edge and a wide, low-opacity shadow, not a hard drop shadow.
7. **Content area is opaque** — Aero deliberately keeps the document/content surface solid. Glass is architecture, not a card style. Microsoft's own guidance says to use glass strategically in small regions touching the frame, and *not* where a plain background is easier to use ([Window Frames](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-window-frames)).
8. **Caption text legibility** — Aero paints a soft light glow behind the title text so it survives an unpredictable wallpaper. In a dark game overlay, use a dark scrim instead of a white glow.

The most useful one-line description comes from [NameThatUI's Aero entry](https://namethatui.com/styles/windows-aero): "transparent blurred window frame, specular sweeps, glowing hot controls, luminous depth gradients." The hover language is *glow*, not a flat color swap.

### 1.2 Window chrome metrics

Vista's exact metrics are theme- and DPI-dependent; there is no single official "Aero overlay spec." The values below are a **reference reconstruction** from the widely used [MakeAero Window Glass generator](https://makeaero.com/window-glass) and Microsoft's themed-metrics guidance (`GetThemeSysSize`, `NONCLIENTMETRICS`). Use them as starting values, not gospel.

| Metric | Vista/Aero reference | This overlay target | Why |
|---|---:|---:|---|
| Outer window radius | 6-8 px | **8 px** | Aero's soft corner, still crisp at 1080p. |
| Glass frame inset | ~8-9 px | **8 px visual inset** (padding, not a real frame) | Enough to read as chrome without wasting menu space. |
| Title bar height | ~30 px | **36 px** | Modern hit target + room for title/subtitle. |
| Caption button size | 29x20 px | **32x24 px** | Slightly larger for mouse/gamepad; still compact. |
| Close button width | 46 px | **44 px** | Preserves the "close is wider" Aero silhouette. |
| Caption glyph | ~10 px | **12 px, 1.5 px stroke** | Readable at 1080p. |
| Title text | Segoe UI 9 pt (~12 px at 96 DPI) | **15 px Selawik semibold** | 12 px is too small over a game. |
| Title inset | 8-12 px | **14 px from left, 8 px from top** | Aligns with an 8 px grid. |
| Inner highlight | 1 px | **1 px white 18-30%** | The Aero "thick glass" cue. |
| Outer border | 1 px dark | **1 px #34474D + 3-layer shadow** | Separation without a heavy shadow. |
| Focus ring | blue glow | **2 px #F0A61C + 2 px offset** | Glow alone fails keyboard accessibility. |
| Button height | ~23-25 px | **28 px** (dense rows 24 px) | Matches ImGui frame padding. |

`WindowBorderHoverPadding` in ImGui 1.92 should be **8 px** so the resize edge is easy to grab with a custom title bar.

### 1.3 Caption buttons and states

Aero's caption buttons are small glass pills hanging from the top edge, with a 1 px dark outline and a 1 px white inner highlight. Min/max glow blue/cyan on hover; close floods red. The close button is wider and visually separated.

| State | Min/Max | Close | Modern overlay translation |
|---|---|---|---|
| Rest | Glass gradient, dark glyph, 1 px outline | Red-tinted glass gradient, white glyph | 24 px high, 1 px #34474D, transparent fill, #A9B7BB glyph. |
| Hover | Cyan/blue radial from bottom + linear glass; outer glow ~8 px | Brighter red + red glow | Fill `#5E767E` at 35%, glyph #E9EEEE, 0 0 0 4 px accent 12% glow. |
| Pressed | Darker gradient, inset 1 px, no outer glow | Dark red, inset | Fill #192428, glyph #F0A61C, 1 px inset black 30%. |
| Disabled | 40% opacity, no glow | 40% opacity | `BeginDisabled()` / `DisabledAlpha = 0.45`. |
| Keyboard focus | Aero uses a glow | same | 2 px accent ring + 2 px offset; never glow-only. |

Canonical Aero colors from the CSS reconstruction: close rest is a red vertical gradient around `#e6a99f -> #d16b5b -> #cf4b34 -> #d8654c`; hover pushes toward `#ffc0b5 -> #d40a0a` with a red halo; min/max hover is a cyan/blue gradient around `#c3e3f4 -> #1f79b0` with a cyan halo ([MakeAero](https://makeaero.com/window-glass)). Do **not** use those exact 2006 colors at full strength; they are too saturated for a 2026 dark UI. Use them as the *state language*: close = red, min/max = cool cyan, both glow.

Glyphs should be drawn procedurally with `ImDrawList` (one line for minimize, a 10x10 rect for maximize, two lines for close). Do not depend on a font glyph being present.

### 1.4 Composition layers: what Aero does vs. what an overlay can do

| # | Aero layer | In a D3D9 game overlay | Practical imitation |
|---|---|---|---|
| 1 | DWM backdrop blur | **Not available** on the game's swapchain. `DwmEnableBlurBehindWindow` is a top-level-window API and produces no blur on Windows 8+ ([Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow)). | High-opacity tint + gradient; optionally a manual backbuffer blur pass (not recommended). |
| 2 | Colorization tint | Achievable | 6-10% accent tint over a neutral dark base. |
| 3 | Saturation lift | Cannot sample the scene for free | Slightly saturated teal/blue surfaces; don't overdo it. |
| 4 | Reflection/specular | Achievable | Top 1-2 px white line + 24-32 px white-to-transparent band; optional diagonal streak texture. |
| 5 | 1 px inner highlight | Achievable | `AddRect` 1 px white 20-30%, inset 1 px. |
| 6 | Outer border/shadow | Achievable with caveats | 1 px dark border + 3-layer shadow on `GetBackgroundDrawList()` or inset padding. |
| 7 | Noise/grain | Achievable | 64x64 white-noise tile at 3% alpha, one extra draw call. |
| 8 | Opaque content | Achievable and recommended | Content panels at 92-96% opacity; never glass behind dense text. |

### 1.5 What is genuinely not achievable (or not worth it)

| Effect | Why not | Workaround |
|---|---|---|
| Real-time blur of the game behind the menu | DWM does not blur a fullscreen D3D swapchain; the game's backbuffer is not a desktop window behind your overlay. | Opaque tint + layered gradients + noise. |
| Dynamic saturation of the scene behind the glass | Requires sampling the scene; no cheap fixed-function path. | Saturated dark-teal palette. |
| Refraction/lensing ("liquid glass") | Requires scene sample + distortion + correct depth; not available in ImGui's 2D draw list. | None; don't pretend. |
| True outer drop shadow outside the ImGui window | ImGui clips each window's draw list to its rect. | Draw shadow on `GetBackgroundDrawList()` before/after Begin, or reserve 8 px inset padding. |
| Rounded multi-color gradient (`AddRectFilledMultiColor`) | The API does not take a rounding parameter. | Solid rounded cap + gradient starting below the radius; or 1 px horizontal strips with varying widths. |
| ClearType/subpixel text | ImGui uses a grayscale antialiased atlas. | Larger type (15-16 px), high contrast, no pure-white-on-pure-black. |
| Per-pixel glass thickness | No depth/thickness model. | One inner highlight + one outer border is enough. |

If someone insists on real blur: copy the backbuffer to a quarter-res texture, run two separable Gaussian passes, then draw it clipped behind the menu. It costs a resolve + two passes every frame, forces render-target/state changes inside the game's frame, interacts badly with MSAA/HDR/tonemapping, and needs its own device-reset handling. **Do not put this in v1.** Aero's readability came from blur *plus* tint *plus* a bright edge; the tint and edge do most of the work at menu sizes.

### 1.6 The fake-glass recipe (what to actually draw)

Draw in this order, per window:

1. **Contact shadow** (outside the window, `GetBackgroundDrawList`): 3 rounded rects, 2/4/6 px larger, black at 10/8/5%, radius + 2/4/6. This is a soft edge, not a 2006 drop shadow.
2. **Base fill**: `#121A1D` at 94% (`ImGuiCol_WindowBg`). This is deliberately opaque enough that a bright sky does not destroy text.
3. **Top specular band**: from y+radius to y+28, white alpha 46 -> 0. Start below the radius so the rounded corners stay clean.
4. **Bottom shade**: from y-24 to y-radius, black alpha 0 -> 64, to give the glass a slight bottom weight.
5. **Inner highlight**: 1 px white at 25%, inset 1 px, radius-1.
6. **Outer border**: 1 px `#34474D` at 80%.
7. **Title-bar tint**: 6% taxi yellow at the top 36 px, fading out by y+72. This is the only place the accent tint appears at low alpha.
8. **Noise**: 64x64 tile at 3% white, stretched over the window (one draw call). It kills banding on the gradients and makes the glass feel like a material.
9. **Content scrim**: if a text block sits over a busy area, draw a black 20-25% rounded rect behind it. Do not rely on the glass tint alone.

Alpha budget: body 0.92-0.96, title/nav 0.82-0.90, popups 0.96-0.98, sheen 0.10-0.18, noise 0.03, shadow 0.30-0.40. If the body alpha drops below ~0.88, the game will show through enough to make small text flicker.

---

## 2. Winamp Big Bento, dissected

### 2.1 What Bento / Big Bento is

Winamp 5.5 introduced the **Bento** skin: a unified player + media library in one window, replacing the classic multi-window layout. It exposes the Media Library, Playlist Editor, Video, Visualizations, Media Monitor Browser, EQ, Skin Options, and 20+ color themes from a single shell ([WinampHeritage overview](https://winampheritage.com/overview), [Wikipedia: Winamp](https://en.wikipedia.org/wiki/Winamp)). **Big Bento** is the larger variant. **Big Bento Modern** (Victhor, 2018) is a community redesign explicitly aimed at high-resolution displays: bigger fonts, a light mode, smoother animations, better album-art and close-button behavior ([WACUP thread](https://getwacup.com/community/index.php?topic=327.0)). The WACUP screenshot gallery shows the current look ([WACUP screenshots](https://getwacup.com/screenshots)).

The important point for this project: Bento is a **dense desktop control panel**, not a consumer dashboard. It solved the same problem this menu has — hundreds of controls, a handful of categories, one window, a lot of state.

### 2.2 Layout vocabulary

| Bento/Big Bento element | What it does | ImGui mapping |
|---|---|---|
| One unified shell | Player + library + playlist in one window | One 1280x760 ImGui window; no floating tool windows. |
| Left vertical tab strip | Categories on the left edge, content on the right | **Left nav rail**, 184 px, 40-44 px rows, 3 px active bar. |
| Stacked panels/drawers | Each category contains several collapsible groups | `CollapsingHeader` with a custom 28 px framed header; 8 px gaps. |
| Playlist/list view | Dense, scannable rows; selection is obvious | `BeginTable` with 24-28 px rows, `RowBg`, one horizontal separator; no vertical grid. |
| Title bar with menu | App identity + global actions | 36 px custom title bar: name, profile/pipeline subtitle, menu/close. |
| Transport/status strip | Persistent state at the bottom | 24 px status strip: LED + READY + profile + FPS/ms/draws. |
| Color themes | 20+ palettes from one skin | 4 theme presets: Taxi, Aero Blue, Bento Graphite, High Contrast. |
| LED/segmented accents | Tiny bright state indicators, segmented meters | 4 px status dots, 2 px accent bars, 1-2 px segmented meters. |
| Dense control rows | Label left, control right, small gaps | 2-column table rows, label left, right-aligned value, 8 px gutter. |
| Small caps section labels | Hierarchy without big headers | 11 px uppercase `#A9B7BB` labels + 1 px separator. |

### 2.3 Proposed wireframe

```
┌──────────────────────────────────────────────────────────────────────┐
│  ▎SkyGFX   PBR / Modern · 4.2.0                         [≡]   [×]   │ 36
├───────────────┬──────────────────────────────────────────────────────┤
│  Dashboard    │  Pipeline & Presets                                  │
│  Vehicle      │  ┌────────────────────────────────────────────────┐  │
│  World        │  │ Building pipe        [ PBR          v ]        │  │
│  PostFX       │  │ Vehicle pipe         [ Modern       v ]        │  │
│  Performance  │  └────────────────────────────────────────────────┘  │
│  Presets      │  Lighting & Ambient                        12 rows   │
│  Settings     │  ┌────────────────────────────────────────────────┐  │
│  INI Keys     │  │ Ambient floor        [ 0.03        ]           │  │
│  Utility      │  │ Sun glare            [ on           ]           │  │
│               │  └────────────────────────────────────────────────┘  │
│               │                                                      │
├───────────────┴──────────────────────────────────────────────────────┤
│  ● READY    Profile: default    Pipeline: PBR    58 fps   4.2 ms     │ 24
└──────────────────────────────────────────────────────────────────────┘
```

The nav rail replaces the current top tab bar. The content area keeps the existing Main two-column split (switchboard left, effect tabs right) for the Dashboard, but the outer chrome is now Bento-like rather than "ImGui demo window with 9 tabs."

### 2.4 Density rules

- **Row height:** 24 px for dense lists, 28 px for controls with 28 px buttons. Big Bento is dense; 40 px rows waste the 760 px height.
- **Gutter:** 8 px between label and control, 16 px between groups, 24 px between sections.
- **Type:** 11 px uppercase section labels, 13 px secondary, 15-16 px body, 15 px title, 13 px mono numerals.
- **Separators:** 1 px `#34474D` + 1 px white 4% below; no boxes around every group.
- **Selection:** 3 px accent left bar + 8-10% accent fill. Hover is 5-6% white, not a color change.
- **Accents:** one accent (taxi yellow) for active/focus/primary. Status colors are separate and always paired with a label/icon.
- **Tabs:** inside a panel, use a segmented control (2-4 items) rather than a second tab bar. The global navigation is the nav rail.
- **Icons:** 16 px line icons or procedural glyphs; do not mix filled and outline styles.

### 2.5 What to steal, what not to

**Steal:** the left rail, stacked panels, list-first density, persistent status strip, 20-theme idea (reduced to 4), the "one shell" discipline, the LED/segmented state vocabulary.

**Do not steal:** 9 px fonts, brushed-metal gradients, beveled every-control skeuomorphism, 2000s blue everywhere, multiple competing accent colors, fake chrome around content, or album-art-sized decorative panels. Big Bento Modern already moved away from the worst of this; follow the modernized version, not the 2007 original.

---

## 3. 2026 modernization

### 3.1 Principles that fit this brief

1. **Aero grammar, 2026 restraint.** Keep the glass chrome, 1 px inner highlight, and glow states; remove heavy gloss, bevels, and text shadows.
2. **Radii 6-10 px.** Window 8, panel 8, control 6, small chips 4 (or full pill only for status chips). This matches Aero 8, Fluent 2's 4/8/12, and M3's 8/12/16 scales.
3. **8 pt spacing grid, 4 pt half-step.** Use 4/8/12/16/24/32; this is the current industry default ([Atlassian spacing](https://atlassian.design/foundations/spacing)).
4. **Low-contrast surfaces, high-contrast text.** Four surface levels separated by 5-8% luminance, not by shadows. Text is 15:1 / 8.5:1 / 4.4:1 on the base surface.
5. **1 px borders instead of heavy shadows.** Shadows only on the outer window; controls use border + luminance.
6. **Accent only for state.** Active nav, focus, primary CTA, selected tab, key metric. Never paint large surfaces taxi yellow. Fluent explicitly warns that overusing brand color dilutes hierarchy ([Fluent 2 Color](https://fluent2.microsoft.design/color)); M3 dark theme says to keep color accents limited ([M3 dark theme](https://m2.material.io/design/color/dark-theme.html)).
7. **Mono numerals for data.** Profiler/status values use tabular figures so digits do not jitter ([Maersk monospaced numbers](https://designsystem.maersk.com/content/numbers/monospaced-numbers), [UIPrep data tables](https://www.uiprep.com/blog/the-ultimate-guide-to-designing-data-tables)).
8. **Motion 120-180 ms, ease-out, no bounce.** M3 Expressive's spring system is great for mobile; in a game overlay, a 150 ms ease-out hover is enough ([M3 Expressive](https://m3.material.io/blog/building-with-m3-expressive)).
9. **Glass is a functional layer, not content.** Apple's Liquid Glass guidance says not to use the material in the content layer; use it for chrome/navigation ([Apple HIG Materials](https://developer.apple.com/design/human-interface-guidelines/materials)).
10. **Nostalgia is a seasoning.** Aero/Vista revival is visible in 2024-2026 community themes ([Aero7-11](https://github.com/LittleFox2024/Aero7-11), [Aero11](https://winclassic.net/thread/3047/aero11-accurate-comprehensive-vista-theme), [OpenGlass](https://github.com/ALTaleX531/OpenGlass)), but the successful modern versions pair it with flat surfaces and dark mode, not full skeuomorphism.

### 3.2 Reference-to-application table

| Trend / reference | Source (2024-2026) | Application here |
|---|---|---|
| Fluent 2 corner scale: 2/4/8/12 | [Fluent 2 Shapes](https://fluent2.microsoft.design/shapes) | Window 8, panel 8, control 6, badges 4. |
| Fluent 2 materials: acrylic for transient, mica for long-lived | [Fluent 2 Material](https://fluent2.microsoft.design/material) | Acrylic-like title/nav/status; solid content panels. |
| Fluent 2 neutral ramp + semantic color | [Fluent 2 Color](https://fluent2.microsoft.design/color) | 4 surface levels; semantic colors with labels/icons. |
| M3 shape scale: 4/8/12/16/28 | [M3 corner radius scale](https://m3.material.io/styles/shape/corner-radius-scale) | 8/12 for panels; no 28 px sheets in an overlay. |
| M3 Expressive shape/motion (May 2025) | [M3 Expressive](https://m3.material.io/blog/building-with-m3-expressive) | 150 ms ease-out, subtle shape change; no bouncy springs. |
| Apple Liquid Glass (June/Sept 2025) | [Apple HIG Materials](https://developer.apple.com/design/human-interface-guidelines/materials) | Glass only on chrome; content stays opaque. |
| 8 px spacing base unit | [Atlassian](https://atlassian.design/foundations/spacing) | ItemSpacing 8, WindowPadding 16, panel padding 12. |
| Dark-first surface hierarchy | [Muzli 2026 UI patterns](https://muz.li/blog/whats-changing-in-mobile-app-design-ui-patterns-that-matter-in-2026/) | No pure black; 4 surfaces; borders for separation. |
| Dark mode text tiers 87/60/38 | [Dark Mode UI guide 2026](https://uxmagic.ai/blog/dark-mode-ui-design-guide) | `#E9EEEE` / `#A9B7BB` / `#718287`. |
| Glassmorphism contrast discipline | [NN/g](https://www.nngroup.com/articles/glassmorphism), [Axess Lab](https://axesslab.com/glassmorphism-meets-accessibility-can-frosted-glass-be-inclusive) | Opaque scrim behind text; don't rely on blur. |
| Winamp community modern skins | [Big Bento Modern](https://getwacup.com/community/index.php?topic=327.0), [Quinto Black CT](https://quinto-black-ct.info/), [ONYX v2](https://getwacup.com/community/index.php?topic=2680.0) | Dense layout, strong title bar, dark/light themes, restrained skeuomorphism. |

### 3.3 Token table

| Token | Value | Notes |
|---|---:|---|
| `radius.window` | 8 px | Aero 8, Fluent 8, M3 8. |
| `radius.panel` | 8 px | Use 6 for nested child panels. |
| `radius.control` | 6 px | Buttons, inputs, combos, tabs. |
| `radius.small` | 4 px | Badges, color swatches, scrollbar grabs. |
| `space.1` | 4 px | Half-step: icon-to-label, tight gaps. |
| `space.2` | 8 px | Base: item spacing, row gutter. |
| `space.3` | 12 px | Panel padding. |
| `space.4` | 16 px | Window padding, section gap. |
| `space.5` | 24 px | Major section gap. |
| `space.6` | 32 px | Page-level separation (rare). |
| `row.height` | 24 / 28 px | Dense list / control row. |
| `title.height` | 36 px | Custom title bar. |
| `status.height` | 24 px | Status strip. |
| `border` | 1 px | Always. |
| `focus` | 2 px + 2 px offset | Accessibility minimum. |
| `motion.fast` | 120 ms | Hover/active. |
| `motion.normal` | 160 ms | Nav indicator, panel open. |

### 3.4 Do not copy

- Heavy top-to-bottom gloss on every control; use one specular band on the chrome only.
- Beveled/embossed buttons; 1 px border + hover luminance is enough.
- Glassmorphism cards everywhere; glass only on title/nav/status.
- 2006 blue gradients; use the GTA palette.
- Giant 0 20px 60px shadows; use a 3-layer 35% shadow at most.
- White text glow; use a dark scrim.
- Pill-shaped everything; only status chips.
- Animated window/size/panel layout; animate color/alpha/indicator only.
- Tiny 9-11 px body text; 15-16 px at 1080p.
- More than one accent hue for state; semantic colors are separate and labeled.

---

## 4. GTA carcols accent

### 4.1 File and format

Local game file: `E:/games/gtasa_skygfx_plus/data/carcols.dat` (ASCII, CRLF).

Format ([GTAMods Wiki](https://gtamods.com/wiki/Carcols.dat), [Grand Theft Wiki](https://www.grandtheftwiki.com/Carcols.dat)):

```
col
R,G,B        # index, name, category
...
end
car
model, primaryIndex, secondaryIndex
...
end
car4
...
end
```

In this file:
- `col` starts at line 22, ends at line 159.
- `car` starts at line 160, ends at line 359.
- `car4` starts at line 361, ends at line 365.
- `taxi, 6,1` is line 331; it confirms the taxi uses colour index **6** as primary.

The values are sRGB display colours; do not linearize them for ImGui. ImGui's 8-bit colours and the D3D9 fixed-function blend path expect display-referred values.

### 4.2 Raw GTA hues extracted from the file

| Name | Index | RGB | Hex | Local line | Palette role |
|---|---:|---|---:|---:|---|
| Taxi Yellow | 6 | 215,142,16 | **#D78E10** | 29 | Primary accent |
| Police Blue | 2 | 42,119,161 | **#2A77A1** | 25 | Cool companion / info base |
| Cherry Red | 3 | 132,4,16 | **#840410** | 26 | Error base |
| Midnight Blue | 4 | 38,55,57 | **#263739** | 27 | Surface tint family |
| Temple Purple | 5 | 134,68,110 | **#86446E** | 28 | Optional "experimental" tag |
| Striking Blue | 7 | 76,117,183 | **#4C75B7** | 30 | Link/info base |
| Light Blue Grey | 8 | 189,190,198 | **#BDBEC6** | 31 | Neutral text/border |
| Frost White | 14 | 214,218,214 | **#D6DAD6** | 38 | Highlight/text neutral |
| Black Poly | 36 | 37,37,39 | **#252527** | 62 | Surface base |

The taxi hex is independently confirmed by the SAMP colour table (`id 6 = d78e10`) ([gtasrv.ru](https://www.gtasrv.ru/readme/servers_vehicle_colors.html)). This is the *actual* taxi yellow, not a generic "GTA yellow."

### 4.3 Semantic palette

The raw carcols are often too dark for small text on a dark surface. The accent is an exception: raw taxi yellow already passes 6.5:1 on `#121A1D`. The semantic colors keep the carcols hue family but are lightened for dark-UI contrast.

#### Surfaces and structure

| Role | Hex | Alpha | Derivation / use |
|---|---|---:|---|
| Surface 0 — app canvas | **#0B1012** | 100% | Midnight blue family, darkened; window shadow/background. |
| Surface 1 — window glass | **#121A1D** | 94% | Main window base; `ImGuiCol_WindowBg`. |
| Surface 2 — panel | **#192428** | 100% | Child/panel fill; `ChildBg`, table header. |
| Surface 3 — raised | **#223034** | 100% | Buttons, title bar, active tab. |
| Surface 4 — hover | **#2B3C41** | 100% | Hover/selected row, frame hover. |
| Border | **#34474D** | 80% | Resting 1 px borders, separators. |
| Border strong | **#5E767E** | 100% | Input borders, focus-adjacent, 3:1 on panel. |
| Text primary | **#E9EEEE** | 100% | Body/title. |
| Text secondary | **#A9B7BB** | 100% | Labels, descriptions. |
| Text disabled | **#718287** | 100% | Disabled controls, placeholders. |
| Accent | **#D78E10** | 100% | Taxi yellow; active/focus/primary. |
| Accent hover | **#F0A61C** | 100% | Hover/focus ring. |
| Accent pressed | **#B87408** | 100% | Pressed/darker. |
| Accent foreground | **#161006** | 100% | Text on accent fill (6.99:1). |
| Success | **#74C06D** | 100% | From GTA green family, lightened. |
| Warn | **#E3B341** | 100% | Amber; always paired with "!"/label. |
| Error | **#F07A75** | 100% | Cherry red family, lightened. |
| Info | **#6F9FE6** | 100% | Striking blue family, lightened. |

#### Glass-specific values

| Role | Hex | Alpha | Use |
|---|---|---:|---|
| Glass sheen | **#FFFFFF** | 18% | Top specular band / inner highlight. |
| Glass noise | **#FFFFFF** | 3% | 64x64 grain tile. |
| Glass scrim | **#000000** | 25% | Behind dense text over bright game areas. |
| Window shadow | **#000000** | 35% | 3-layer outer shadow. |
| Title tint | **#D78E10** | 6% | Warm accent wash on the title bar only. |
| Glass tint (cool) | **#2A77A1** | 4% | Optional cool cast in nav/status. |

#### Contrast check (against Surface 1 `#121A1D`)

| Foreground | Ratio | WCAG AA text (4.5:1) | UI component (3:1) |
|---|---:|---|---|
| Text primary `#E9EEEE` | **15.05:1** | Pass | Pass |
| Text secondary `#A9B7BB` | **8.54:1** | Pass | Pass |
| Text disabled `#718287` | **4.40:1** | Borderline (disabled exempt) | Pass |
| Accent `#D78E10` | **6.52:1** | Pass | Pass |
| Accent hover `#F0A61C` | **8.54:1** | Pass | Pass |
| Success `#74C06D` | **7.98:1** | Pass | Pass |
| Warn `#E3B341` | **9.06:1** | Pass | Pass |
| Error `#F07A75` | **6.50:1** | Pass | Pass |
| Info `#6F9FE6` | **6.53:1** | Pass | Pass |
| Accent foreground on accent | **6.99:1** | Pass | Pass |

`#34474D` is only ~1.8:1 on the panel: use it for decorative separators, not for input borders. Use `#5E767E` (3.18:1 on Surface 2) for any border that must be perceivable, and `#F0A61C` for the focus ring.

### 4.4 Derivation rules

1. **Accent = raw taxi yellow.** `#D78E10` is already readable on the dark surface; do not "modernize" it into a pale yellow. The 2026 update comes from how it is used, not from desaturating it.
2. **Hover = +12% lightness.** `#F0A61C` keeps the same hue and is used for focus rings and hover.
3. **Pressed = -12% lightness.** `#B87408` gives a physical press without a bevel.
4. **Surfaces are blue-green-tinted neutrals derived from midnight blue**, not pure gray. This is what makes the theme feel GTA-specific rather than generic dark ImGui.
5. **Semantic colors keep the carcols hue** but are lightened for contrast: cherry red -> `#F07A75`, striking blue -> `#6F9FE6`, GTA green -> `#74C06D`. Warn is the taxi hue lightened so it is distinguishable from the accent by *label/icon*, not hue alone.
6. **Accent fill uses dark text**, not white. White on `#D78E10` is only 2.70:1; `#161006` is 6.99:1.
7. **Never use raw carcols for large surfaces.** `#840410` and `#263739` are display colors, not UI surfaces; they are too saturated/dark at scale.

### 4.5 Theme presets

| Preset | Accent | Surface tint | Use |
|---|---|---|---|
| **Taxi** (default) | `#D78E10` | Teal-black (above) | The brief's "taxi yellow against dark glass." |
| **Aero Blue** | `#2A77A1` / hover `#6F9FE6` | Cooler blue-black | Vista nostalgia; less GTA-specific. |
| **Bento Graphite** | `#BDBEC6` | Neutral graphite | Winamp/Bento look; accent is a light gray. |
| **High Contrast** | `#F0B54A` | `#000000` | Accessibility/debug; 2 px borders, no glass. |

Keep theme switching in the title-bar menu or a Settings panel. Do not add 20 themes; 4 is enough and each must be contrast-checked.

---

## 5. ImGui implementation spec

### 5.1 `ImGuiStyle` fields

Apply once in `EnsureImGuiInit()` after `ImGui::CreateContext()`, before the first frame. Start from `ImGui::StyleColorsDark()` and override.

| Field | Value | Why |
|---|---:|---|
| `FontSizeBase` | **16.0f** | 1080p body size; scale with DPI. |
| `FontScaleMain` | 1.0f | User scale hook. |
| `FontScaleDpi` | `clamp(displayH/1080, 0.85, 1.5)` | 1.92 global font scale. |
| `Alpha` | 1.0f | Don't fade the whole UI. |
| `DisabledAlpha` | 0.45f | Disabled controls still readable. |
| `WindowPadding` | (16, 14) | 8 pt grid + title bar breathing room. |
| `WindowRounding` | **8.0f** | Aero/Fluent/M3 8. |
| `WindowBorderSize` | 0.0f (pushed) / 1.0f global | Custom window draws its own border; popups get 1. |
| `WindowBorderHoverPadding` | 8.0f | Easy resize edge. |
| `WindowMinSize` | (1000, 620) | Preserve current constraint. |
| `WindowTitleAlign` | (0.0, 0.5) | Left-aligned title. |
| `WindowMenuButtonPosition` | `ImGuiDir_None` | No collapse arrow; custom title. |
| `ChildRounding` | 6.0f | Nested panels. |
| `ChildBorderSize` | 1.0f | Panel separation. |
| `PopupRounding` | 8.0f | Combos/tooltips. |
| `PopupBorderSize` | 1.0f | Popup edge. |
| `FramePadding` | (10, 5) | 28 px control height with 16 px font. |
| `FrameRounding` | **6.0f** | Modern control radius. |
| `FrameBorderSize` | 1.0f | Crisp control edge. |
| `ItemSpacing` | (8, 8) | 8 pt grid. |
| `ItemInnerSpacing` | (6, 6) | Label/control gap. |
| `CellPadding` | (8, 6) | Table density. |
| `IndentSpacing` | 20.0f | Tree/panel indentation. |
| `ScrollbarSize` | 12.0f | Visible but slim. |
| `ScrollbarRounding` | 6.0f | Match controls. |
| `ScrollbarPadding` | 3.0f | Grab inset. |
| `GrabMinSize` | 12.0f | Slider/scrollbar grab. |
| `GrabRounding` | 6.0f | Match controls. |
| `TabRounding` | 6.0f | Segmented tabs. |
| `TabBorderSize` | 1.0f | Tab edge. |
| `TabBarBorderSize` | 1.0f | Tab strip separator. |
| `TabBarOverlineSize` | 2.0f | Active tab accent. |
| `TabMinWidthBase` | 80.0f | Compact tabs. |
| `TabMinWidthShrink` | 60.0f | Fitting policy. |
| `SeparatorSize` | 1.0f | Hairline. |
| `SeparatorTextBorderSize` | 1.0f | Section label rule. |
| `SeparatorTextPadding` | (12, 6) | Label breathing room. |
| `ButtonTextAlign` | (0.5, 0.5) | Centered. |
| `SelectableTextAlign` | (0.0, 0.5) | Left-aligned rows. |
| `AntiAliasedLines` | true | Crisp 1 px borders. |
| `AntiAliasedLinesUseTex` | true | Faster AA where possible. |
| `AntiAliasedFill` | true | Smooth rounded corners. |
| `CircleTessellationMaxError` | 0.30f | Smooth LEDs without excess verts. |

`WindowBorderSize=0` globally is tempting, but then popups/tooltips lose their edge. Keep the global value at 1 and push `WindowBorderSize=0` around the custom main window only.

If you call `style.ScaleAllSizes(dpiScale)` for 1440p/4K, do it **once** from a cached base style, then force the border sizes back to 1 px. `ScaleAllSizes` multiplies the current values, so calling it every frame compounds. Also re-apply `WindowMinSize` in the scaled units; otherwise the 1280x760 menu can be clamped up at 125-150% scaling.

### 5.2 Color table

Use `ImVec4` via `ImGui::ColorConvertU32ToFloat4(IM_COL32(...))`. Hex is `#RRGGBBAA`.

| `ImGuiCol_` | Hex | Use |
|---|---|---|
| `Text` | `#E9EEEEFF` | Body/title. |
| `TextDisabled` | `#718287FF` | Disabled, not secondary. |
| `WindowBg` | `#121A1DF0` | Main window 94%. |
| `ChildBg` | `#192428B8` | Panel 72%; glass shows through slightly. |
| `PopupBg` | `#192428FA` | Popups 98%. |
| `Border` | `#34474DCC` | 80% hairline. |
| `BorderShadow` | `#00000000` | No shadow border; draw our own. |
| `FrameBg` | `#0B1012E6` | Inputs darker than panel. |
| `FrameBgHovered` | `#172329F2` | Hover. |
| `FrameBgActive` | `#223034F2` | Active input. |
| `TitleBg` | `#121A1DF0` | Default title (unused if custom). |
| `TitleBgActive` | `#223034F5` | Default active title. |
| `TitleBgCollapsed` | `#0B1012E0` | Collapsed. |
| `MenuBarBg` | `#121A1DF0` | Menu bar. |
| `ScrollbarBg` | `#00000000` | Transparent track. |
| `ScrollbarGrab` | `#34474DCC` | Rest. |
| `ScrollbarGrabHovered` | `#5E767ECC` | Hover. |
| `ScrollbarGrabActive` | `#D78E10CC` | Active. |
| `CheckMark` | `#F0A61CFF` | Check/radio. |
| `CheckboxSelectedBg` | `#D78E104D` | 30% accent fill. |
| `SliderGrab` | `#D78E10FF` | Slider. |
| `SliderGrabActive` | `#F0A61CFF` | Active slider. |
| `Button` | `#223034FF` | Surface 3. |
| `ButtonHovered` | `#2B3C41FF` | Surface 4. |
| `ButtonActive` | `#192428FF` | Surface 2. |
| `Header` | `#D78E102E` | 18% accent. |
| `HeaderHovered` | `#D78E1042` | 26% accent. |
| `HeaderActive` | `#D78E105C` | 36% accent. |
| `Separator` | `#34474DCC` | Hairline. |
| `SeparatorHovered` | `#5E767EA6` | Hover. |
| `SeparatorActive` | `#D78E10FF` | Active. |
| `ResizeGrip` | `#34474D99` | Rest. |
| `ResizeGripHovered` | `#5E767ECC` | Hover. |
| `ResizeGripActive` | `#D78E10CC` | Active. |
| `InputTextCursor` | `#F0A61CFF` | Caret. |
| `Tab` | `#192428CC` | Unselected. |
| `TabHovered` | `#223034E6` | Hover. |
| `TabSelected` | `#223034FF` | Selected. |
| `TabSelectedOverline` | `#D78E10FF` | Active accent. |
| `TabDimmed` | `#121A1DA6` | Unfocused. |
| `TabDimmedSelected` | `#192428D9` | Unfocused selected. |
| `TabDimmedSelectedOverline` | `#D78E1099` | Unfocused accent. |
| `PlotLines` | `#6F9FE6FF` | Info blue. |
| `PlotLinesHovered` | `#F0A61CFF` | Hover. |
| `PlotHistogram` | `#D78E10FF` | Accent. |
| `PlotHistogramHovered` | `#F0A61CFF` | Hover. |
| `TableHeaderBg` | `#192428FF` | Header. |
| `TableBorderStrong` | `#34474DFF` | Outer/header. |
| `TableBorderLight` | `#26363BFF` | Inner horizontal. |
| `TableRowBg` | `#00000000` | Transparent. |
| `TableRowBgAlt` | `#FFFFFF08` | 3% alternate. |
| `TextLink` | `#F0A61CFF` | Link. |
| `TextSelectedBg` | `#D78E1059` | 35% selection. |
| `TreeLines` | `#34474DCC` | Tree lines. |
| `DragDropTarget` | `#F0A61CFF` | Drop outline. |
| `DragDropTargetBg` | `#D78E1033` | 20% fill. |
| `UnsavedMarker` | `#F0A61CFF` | Modified marker. |
| `NavCursor` | `#F0A61CFF` | Keyboard focus. |
| `NavWindowingHighlight` | `#FFFFFFB3` | Window switcher. |
| `NavWindowingDimBg` | `#00000099` | Dim. |
| `ModalWindowDimBg` | `#000000A6` | Modal dim. |

ImGui has only `Text` and `TextDisabled`; there is no built-in "secondary text." Use a helper:

```cpp
#include <stdarg.h>

static void TextMuted(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::C(Theme::g_pal.textDim));
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}
```

Reserve `TextDisabled` for actually disabled controls so users learn the distinction.

### 5.3 Custom `ImDrawList` effects

| Effect | API | Implementation notes |
|---|---|---|
| Window glass base | `AddRectFilled` + `AddRectFilledMultiColor` | Base solid + top band; start band below radius. |
| Title-bar gradient | `AddRectFilled` (rounded top) + `AddRectFilledMultiColor` | Solid top 8 px cap, gradient below; `ImDrawFlags_RoundCornersTop`. |
| 1 px inner highlight | `AddLine` / `AddRect` | White 20-30%, inset 1 px. |
| Outer border | `AddRect` | 1 px `#34474D`, radius 8. |
| Outer shadow | `GetBackgroundDrawList()->AddRectFilled` | 3 layers, black 10/8/5%, radius + 2/4/6. |
| Separator | `AddLine` + second line | 1 px border + 1 px white 4% at y+1. |
| Focus glow | `AddRect` x3 | 6 px 18% accent, 3 px 35%, 2 px 100%; or 2 px solid + 2 px offset. |
| Status LED | `AddCircleFilled` x2 | 4 px dot + 8 px 25% glow; label always present. |
| Noise | `AddImage` | 64x64 tile, 3% alpha, stretched. |
| Diagonal sheen | `AddImage` | 128x64 soft streak texture at 6-10%; optional. |
| Nav active bar | `AddRectFilled` | 3x18 px accent, 2 px radius. |
| Segmented meter | `AddRectFilled` loop | 3x8 px segments, 2 px gap; accent for active, border for inactive. |
| Close glyph | `AddLine` x2 | 1.5 px stroke; no icon font. |
| Minimize glyph | `AddLine` | 10 px horizontal. |
| Maximize glyph | `AddRect` | 10x9 px, 1.5 px stroke. |

**ImGui 1.92.9 signature trap:** `AddRect` is now

```cpp
dl->AddRect(p_min, p_max, col, rounding, thickness, flags); // 1.92.8+
```

The old `(p_min, p_max, col, rounding, flags, thickness)` overload is deleted. Use the new order or the compiler will reject it. `AddRectFilled` is still `(p_min, p_max, col, rounding, flags)`.

### 5.4 Custom window and title bar

Because the default ImGui title bar cannot be gradient-filled or given Aero caption buttons, use `ImGuiWindowFlags_NoTitleBar | NoBackground | NoCollapse` and draw the chrome yourself. Keep resize enabled (do **not** set `NoResize`).

Implementation outline:

1. `SetNextWindowSize` and `SetNextWindowSizeConstraints` exactly as today.
2. `Begin(name, open, NoTitleBar | NoBackground | NoCollapse)`.
3. Draw the shadow on `GetBackgroundDrawList()` using the actual window pos/size.
4. Draw the glass base + border in the window draw list.
5. Draw a 36 px title bar: gradient, bottom hairline, 3 px accent bar, title 15 px, subtitle 11 px.
6. Submit an `InvisibleButton` drag area from x+8 to x+w-48. On `IsItemActive() && IsMouseDragging(0)`, `SetWindowPos(pos + io.MouseDelta)`.
7. Submit close button (40x24) at the right; hover red, glyph white; click sets `*open=false`.
8. `SetCursorPos(0, 36)` and `BeginChild("##content", ...)`; the child holds the existing tabs/panels.
9. `EndGlassWindow()` calls `EndChild()` then `End()`.

Pitfalls:
- `NoTitleBar` removes dragging; you must reimplement it.
- `NoBackground` still draws a border if `WindowBorderSize > 0`; push 0 around `Begin`.
- The drag area must exclude the caption buttons, or the close button will never receive hover.
- `SetWindowPos` while dragging can fight ImGui's own positioning if you also call `SetNextWindowPos` every frame; only set the initial position once.
- Outer shadow is clipped by the window draw list; use the background list.
- If you add a "minimize" button, decide what it means for a game overlay (collapse to a title bar) and implement that; don't fake an OS minimize.

### 5.5 Nav rail, panels, status strip

**Nav rail** (184 px):

```cpp
bool NavItem(const char* label, bool active, int badge) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(184.0f, 40.0f);
    bool pressed = ImGui::InvisibleButton("##nav", sz);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active)      dl->AddRectFilled(p, p + sz, Theme::A(Theme::g_pal.accent, 0.10f), 6.0f);
    else if (hovered) dl->AddRectFilled(p, p + sz, IM_COL32(255,255,255,12), 6.0f);
    if (active)      dl->AddRectFilled(p + ImVec2(0, 9), p + ImVec2(3, 31), Theme::g_pal.accent, 2.0f);
    dl->AddText(ImGui::GetFont(), 14.0f, p + ImVec2(16, 12),
                active ? Theme::g_pal.text : Theme::g_pal.textDim, label);
    if (badge > 0) { /* right-aligned 11 px mono count */ }
    ImGui::PopID();
    return pressed;
}
```

- Use `ImGui::GetStateStorage()` only for animation, not for selection; selection belongs in a single `int g_currentPage`.
- Keyboard focus: `NavItem` should use `ImGui::InvisibleButton` + `ImGui::SetItemDefaultFocus()` when the page changes, and honor `ImGuiCol_NavCursor`.
- Add a tooltip with the full label if the rail is narrow.

**Panels:**

Use `ImGui::CollapsingHeader` with `ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen`. With `FramePadding.y = 5` and 16 px font, the header is ~28 px. Push `Header`/`HeaderHovered`/`HeaderActive` colors from the palette. Inside, use a 2-column table:

```cpp
if (ImGui::BeginTable("##rows", 2,
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthStretch, 0.38f);
    // TableNextRow(0, 24.0f); AlignTextToFramePadding(); SetNextItemWidth(160);
    ImGui::EndTable();
}
```

**Status strip (24 px):**

```cpp
ImGui::BeginChild("##status", ImVec2(0, 24), ImGuiChildFlags_None,
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
ImDrawList* dl = ImGui::GetWindowDrawList();
ImVec2 p = ImGui::GetWindowPos();
dl->AddLine(p, p + ImVec2(ImGui::GetWindowWidth(), 0), Theme::g_pal.border, 1.0f);
// LED at (p.x+12, p.y+12), 4 px + 8 px glow
// left: READY / MODIFIED; center: profile + pipeline; right: fps + ms + draws
ImGui::EndChild();
```

Reserve the strip height from the content area (`GetContentRegionAvail().y - 24`), exactly like the current `kBottomReserve` pattern.

### 5.6 Texture atlas / nine-slice: recommendation

**Do not ship a nine-slice window skin in v1.** Draw the frame procedurally.

| Approach | Use | Cost | Verdict |
|---|---|---|---|
| Procedural `ImDrawList` | Frame, title, buttons, separators, LEDs | A few thousand vertices | **Default.** Crisp at any DPI, no asset/reset pipeline. |
| 64x64 noise tile | Glass grain | One texture, one draw call | **Use.** Kills gradient banding. |
| 128x64 sheen streak | Optional diagonal reflection | One texture | Optional; keep at 6-10% alpha. |
| 256x256 icon atlas | 16 px icons | One texture, one atlas | Optional; procedural glyphs first. |
| Nine-slice painted frame | Brushed-metal/photo-real chrome | One texture, 9 quads | Not v1. 1 px borders scale poorly; adds device-reset handling. |
| Full-window background image | Wallpaper | Large texture | No; the game is the background. |

If you do use a texture:
- Author **straight alpha**, not premultiplied. The DX9 backend uses `SRCALPHA`/`INVSRCALPHA`.
- Use `D3DFMT_A8B8G8R8` when `ImGui_ImplDX9_CheckFormatSupport` says it is supported; otherwise the backend converts RGBA to BGRA. Do not manually swizzle unless you also change the backend.
- Create the texture as `D3DPOOL_MANAGED` (survives reset) or release/recreate it in the device-reset hook.
- Pass it as `ImTextureID` (an `ImU64`); `AddImage` accepts `ImTextureRef` via implicit construction.
- Cache it; never create a D3D texture per frame.

A nine-slice frame is only worth it if you want a *painted* Aero reflection texture. The 2026 look is flatter; procedural + noise is closer to the target and much less fragile.

### 5.7 Fonts and DPI

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| ImGui default ProggyClean 13 px | Zero assets, crisp at 13 px | Tiny, pixelated, instantly "2014 tool" | Fallback only |
| **Selawik** | OFL, metric-compatible with Segoe UI, 5 weights, Vista feel | ~300-500 KB TTF; needs embedding | **Default UI font** |
| Inter | Excellent screen font, modern | Less Vista, slightly larger x-height | Alternative |
| JetBrains Mono | True tabular numerals, excellent at 13 px | Extra atlas space | **Numbers only** |
| Segoe UI | Exact Vista look | Proprietary; do not redistribute | Do not embed |

Selawik is Microsoft's open-source Segoe UI replacement, licensed under SIL OFL 1.1 ([Microsoft Typography](https://learn.microsoft.com/en-us/typography/font-list/selawik), [GitHub](https://github.com/microsoft/Selawik)). It is the right compromise: Vista metrics without a licensing problem. Segoe UI was the Aero default at 9 pt, increased from 8 pt for readability ([Microsoft Fonts guide](https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-fonts), [Wikipedia](https://en.wikipedia.org/wiki/Windows_Aero)); 9 pt ≈ 12 px at 96 DPI, which is too small for a game overlay. Use 15-16 px at 1080p.

Recommended sizes:

| Role | Size | Font |
|---|---:|---|
| Window title | 15 px semibold | Selawik Semibold |
| Body / labels | 15-16 px regular | Selawik Regular |
| Secondary / captions | 12-13 px | Selawik Regular |
| Section labels | 11 px uppercase | Selawik Semibold |
| Nav rail | 14 px | Selawik Regular/Semibold |
| Profiler/status numbers | 13 px | JetBrains Mono |
| Caption glyphs | 12 px / 1.5 px stroke | Procedural lines |

**1.92 dynamic fonts:** since 1.92, glyph ranges are unnecessary and fonts can be used at any size; `PushFont(nullptr, size)` changes size on the fly ([FONTS.md](https://github.com/ocornut/imgui/blob/master/docs/FONTS.md)). The atlas starts at 512x128 and grows. To avoid a startup hitch, set:

```cpp
io.Fonts->TexMinWidth  = 1024;
io.Fonts->TexMinHeight = 512;

// Embed Selawik (OFL) as a compressed C array, generated with
// misc/fonts/binary_to_compressed_c.cpp. Do not ship Segoe UI.
ImFont* ui = io.Fonts->AddFontFromMemoryCompressedTTF(
    selawik_compressed_data, selawik_compressed_size);
io.FontDefault = ui;

// Optional: one mono font for profiler/status numerals.
ImFont* mono = io.Fonts->AddFontFromMemoryCompressedTTF(
    jetbrains_mono_compressed_data, jetbrains_mono_compressed_size);
```

Memory: 1024x512 RGBA = **2 MB**; 1024x1024 = **4 MB**; 2048x2048 = **16 MB**. A UI font + mono font at 16/13 px with Latin-1 coverage fits comfortably in 1024x512-1024x1024. The TTF itself is ~300-500 KB. This is negligible for a game process, but do not load five weights and CJK ranges "just in case."

DPI:
- Compute `scale = clamp(displayH / 1080.0f, 0.85f, 1.5f)`.
- Apply `style.FontScaleDpi = scale` and `style.ScaleAllSizes(scale)` **once** from a cached base style.
- Force 1 px borders back to 1 px after scaling; a 1.5 px border looks blurry.
- Do not scale at 720p/1080p by more than 1.0; the menu is already 1280x760 and must fit.
- Test 1280x720, 1920x1080, 2560x1440, 3840x2160.

### 5.8 Animation

ImGui has no built-in animation system; the official answer is user-side timers ([issue #1857](https://github.com/ocornut/imgui/issues/1857)). A 20-line helper is enough; do not add ImAnim as a dependency for this.

Recommended approach: exponential smoothing per item ID.

```cpp
float Smooth(ImGuiID id, float target, float speed, float dt) {
    float& v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    dt = ImClamp(dt, 1.0f / 240.0f, 1.0f / 30.0f); // loading spikes
    float k = 1.0f - expf(-speed * dt);
    v += (target - v) * k;
    return v;
}
```

- Use speed ~14-18 for hover, ~10 for focus glow.
- Animate **color alpha**, **glow alpha**, and a **1-2 px offset**; do not animate layout size, row height, or window size. Layout animation causes scrollbar flicker and text jitter.
- Push colors per item (`PushStyleColor`) instead of mutating global `style.Colors`, or every widget animates together.
- Reset storage when the menu closes (`config->debugMenuOpen` transition) so reopening does not show a half-finished hover.
- Clamp `io.DeltaTime`; a loading hitch can produce a 0.5 s delta and a visible snap.
- Respect a `uiAnimations=0` INI key: if disabled, return `target` immediately.
- Do not animate the title bar gradient or the window shadow; they should be stable.

Nav indicator: animate the 3 px active bar's alpha/width from 0 to 3 over ~120 ms when the page changes. Do not slide a 184 px highlight across the rail; it reads as a carousel, not a menu.

### 5.9 Code skeleton

This is a paste-and-refine skeleton, not production code. It assumes ImGui 1.92.9 and the DX9 backend. It intentionally avoids `imgui_internal.h`.

**`src/core/debugmenu_theme.h`**

```cpp
#pragma once
#include "imgui.h"

namespace DebugMenuTheme {

struct Palette {
    ImU32 surface0, surface1, surface2, surface3, surface4;
    ImU32 border, borderStrong;
    ImU32 text, textDim, textDisabled;
    ImU32 accent, accentHover, accentPressed, accentFg;
    ImU32 success, warn, error, info;
    ImU32 glassSheen, glassNoise, glassScrim, shadow;
    ImU32 titleTop, titleBottom;
};

extern Palette g_pal;

// Call once after ImGui::CreateContext(), before the first NewFrame().
void ApplyTheme(float dpiScale = 1.0f);

// Draw a glass panel. `a`/`b` are screen-space. `alpha` is 0..1.
void DrawGlassWindow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float alpha);

// Optional: draw the outer shadow on the background draw list.
void DrawWindowShadow(ImVec2 a, ImVec2 b, float rounding);

// Custom 36 px title bar. Returns false if the close button was pressed.
bool DrawTitleBar(const char* title, const char* subtitle, bool* open, float height = 36.0f);

// Begin/End pair. If Begin returns false, do NOT call EndGlassWindow().
bool BeginGlassWindow(const char* name, bool* open, ImVec2 size,
                      ImGuiWindowFlags flags = 0);
void EndGlassWindow();

// Frame-rate independent smoothing for hover/glow.
float Smooth(ImGuiID id, float target, float speed, float dt);

// Helpers used by the UI code.
ImU32  A(ImU32 col, float alphaMul);
ImVec4 C(ImU32 col, float alphaMul = 1.0f);

} // namespace DebugMenuTheme
```

**`src/core/debugmenu_theme.cpp`**

```cpp
#include "debugmenu_theme.h"
#include <math.h>

namespace DebugMenuTheme {

// Pack an sRGB 0xRRGGBB + alpha into ImGui's ABGR order.
static ImU32 Hex(unsigned rgb, unsigned a = 255) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

ImU32 A(ImU32 col, float alphaMul) {
    ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
    a = (ImU32)ImClamp((float)a * alphaMul, 0.0f, 255.0f);
    return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImVec4 C(ImU32 col, float alphaMul) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
    v.w *= alphaMul;
    return v;
}

Palette g_pal = {
    /* surface0      */ Hex(0x0B1012),
    /* surface1      */ Hex(0x121A1D),
    /* surface2      */ Hex(0x192428),
    /* surface3      */ Hex(0x223034),
    /* surface4      */ Hex(0x2B3C41),
    /* border        */ Hex(0x34474D),
    /* borderStrong  */ Hex(0x5E767E),
    /* text          */ Hex(0xE9EEEE),
    /* textDim       */ Hex(0xA9B7BB),
    /* textDisabled  */ Hex(0x718287),
    /* accent        */ Hex(0xD78E10),
    /* accentHover   */ Hex(0xF0A61C),
    /* accentPressed */ Hex(0xB87408),
    /* accentFg      */ Hex(0x161006),
    /* success       */ Hex(0x74C06D),
    /* warn          */ Hex(0xE3B341),
    /* error         */ Hex(0xF07A75),
    /* info          */ Hex(0x6F9FE6),
    /* glassSheen    */ Hex(0xFFFFFF, 0x2E), // 18%
    /* glassNoise    */ Hex(0xFFFFFF, 0x08), // 3%
    /* glassScrim    */ Hex(0x000000, 0x40), // 25%
    /* shadow        */ Hex(0x000000, 0x59), // 35%
    /* titleTop      */ Hex(0x2B3C41),
    /* titleBottom   */ Hex(0x121A1D),
};

void ApplyTheme(float dpiScale) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    s.FontSizeBase    = 16.0f;
    s.FontScaleMain   = 1.0f;
    s.FontScaleDpi    = dpiScale;

    s.WindowPadding   = ImVec2(16.0f, 14.0f);
    s.WindowRounding  = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.WindowBorderHoverPadding = 8.0f;
    s.WindowMinSize   = ImVec2(1000.0f, 620.0f);
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;

    s.ChildRounding   = 6.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding   = 8.0f;
    s.PopupBorderSize = 1.0f;

    s.FramePadding    = ImVec2(10.0f, 5.0f);
    s.FrameRounding   = 6.0f;
    s.FrameBorderSize = 1.0f;
    s.ItemSpacing     = ImVec2(8.0f, 8.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    s.CellPadding     = ImVec2(8.0f, 6.0f);
    s.IndentSpacing   = 20.0f;

    s.ScrollbarSize   = 12.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScrollbarPadding = 3.0f;
    s.GrabMinSize     = 12.0f;
    s.GrabRounding    = 6.0f;

    s.TabRounding     = 6.0f;
    s.TabBorderSize   = 1.0f;
    s.TabBarBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.TabMinWidthBase = 80.0f;
    s.TabMinWidthShrink = 60.0f;

    s.SeparatorSize   = 1.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(12.0f, 6.0f);

    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.DisabledAlpha   = 0.45f;

    s.AntiAliasedLines = true;
    s.AntiAliasedLinesUseTex = true;
    s.AntiAliasedFill = true;
    s.CircleTessellationMaxError = 0.30f;

    // Optional one-time DPI scale. Do not call every frame.
    if (dpiScale != 1.0f)
        s.ScaleAllSizes(dpiScale);

    // Keep hairlines crisp after scaling.
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize  = 1.0f;
    s.PopupBorderSize  = 1.0f;
    s.FrameBorderSize  = 1.0f;
    s.TabBorderSize    = 1.0f;
    s.SeparatorSize    = 1.0f;

    s.Colors[ImGuiCol_Text]                 = C(g_pal.text);
    s.Colors[ImGuiCol_TextDisabled]         = C(g_pal.textDisabled);
    s.Colors[ImGuiCol_WindowBg]             = C(g_pal.surface1, 0.94f);
    s.Colors[ImGuiCol_ChildBg]              = C(g_pal.surface2, 0.72f);
    s.Colors[ImGuiCol_PopupBg]              = C(g_pal.surface2, 0.98f);
    s.Colors[ImGuiCol_Border]               = C(g_pal.border, 0.80f);
    s.Colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_FrameBg]              = C(g_pal.surface0, 0.90f);
    s.Colors[ImGuiCol_FrameBgHovered]       = C(g_pal.surface4, 0.95f);
    s.Colors[ImGuiCol_FrameBgActive]        = C(g_pal.surface3, 1.00f);
    s.Colors[ImGuiCol_TitleBg]              = C(g_pal.surface1, 0.94f);
    s.Colors[ImGuiCol_TitleBgActive]        = C(g_pal.titleTop, 0.96f);
    s.Colors[ImGuiCol_TitleBgCollapsed]     = C(g_pal.surface0, 0.88f);
    s.Colors[ImGuiCol_MenuBarBg]            = C(g_pal.surface1, 0.94f);
    s.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab]        = C(g_pal.border, 0.80f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = C(g_pal.borderStrong, 0.80f);
    s.Colors[ImGuiCol_ScrollbarGrabActive]  = C(g_pal.accent, 0.80f);
    s.Colors[ImGuiCol_CheckMark]            = C(g_pal.accentHover);
    s.Colors[ImGuiCol_CheckboxSelectedBg]   = C(g_pal.accent, 0.30f);
    s.Colors[ImGuiCol_SliderGrab]           = C(g_pal.accent);
    s.Colors[ImGuiCol_SliderGrabActive]     = C(g_pal.accentHover);
    s.Colors[ImGuiCol_Button]               = C(g_pal.surface3);
    s.Colors[ImGuiCol_ButtonHovered]        = C(g_pal.surface4);
    s.Colors[ImGuiCol_ButtonActive]         = C(g_pal.surface2);
    s.Colors[ImGuiCol_Header]               = C(g_pal.accent, 0.18f);
    s.Colors[ImGuiCol_HeaderHovered]        = C(g_pal.accent, 0.26f);
    s.Colors[ImGuiCol_HeaderActive]         = C(g_pal.accent, 0.36f);
    s.Colors[ImGuiCol_Separator]            = C(g_pal.border, 0.80f);
    s.Colors[ImGuiCol_SeparatorHovered]     = C(g_pal.borderStrong, 0.65f);
    s.Colors[ImGuiCol_SeparatorActive]      = C(g_pal.accent);
    s.Colors[ImGuiCol_ResizeGrip]           = C(g_pal.border, 0.60f);
    s.Colors[ImGuiCol_ResizeGripHovered]    = C(g_pal.borderStrong, 0.80f);
    s.Colors[ImGuiCol_ResizeGripActive]     = C(g_pal.accent, 0.80f);
    s.Colors[ImGuiCol_InputTextCursor]      = C(g_pal.accentHover);
    s.Colors[ImGuiCol_Tab]                  = C(g_pal.surface2, 0.80f);
    s.Colors[ImGuiCol_TabHovered]           = C(g_pal.surface4, 0.90f);
    s.Colors[ImGuiCol_TabSelected]          = C(g_pal.surface3);
    s.Colors[ImGuiCol_TabSelectedOverline]  = C(g_pal.accent);
    s.Colors[ImGuiCol_TabDimmed]            = C(g_pal.surface1, 0.65f);
    s.Colors[ImGuiCol_TabDimmedSelected]    = C(g_pal.surface2, 0.85f);
    s.Colors[ImGuiCol_TabDimmedSelectedOverline] = C(g_pal.accent, 0.60f);
    s.Colors[ImGuiCol_PlotLines]            = C(g_pal.info);
    s.Colors[ImGuiCol_PlotLinesHovered]     = C(g_pal.accentHover);
    s.Colors[ImGuiCol_PlotHistogram]        = C(g_pal.accent);
    s.Colors[ImGuiCol_PlotHistogramHovered] = C(g_pal.accentHover);
    s.Colors[ImGuiCol_TableHeaderBg]        = C(g_pal.surface2);
    s.Colors[ImGuiCol_TableBorderStrong]    = C(g_pal.border);
    s.Colors[ImGuiCol_TableBorderLight]     = C(g_pal.surface4);
    s.Colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.03f);
    s.Colors[ImGuiCol_TextLink]             = C(g_pal.accentHover);
    s.Colors[ImGuiCol_TextSelectedBg]       = C(g_pal.accent, 0.35f);
    s.Colors[ImGuiCol_TreeLines]            = C(g_pal.border, 0.80f);
    s.Colors[ImGuiCol_DragDropTarget]       = C(g_pal.accentHover);
    s.Colors[ImGuiCol_DragDropTargetBg]     = C(g_pal.accent, 0.20f);
    s.Colors[ImGuiCol_UnsavedMarker]        = C(g_pal.accentHover);
    s.Colors[ImGuiCol_NavCursor]            = C(g_pal.accentHover);
    s.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.70f);
    s.Colors[ImGuiCol_NavWindowingDimBg]    = ImVec4(0, 0, 0, 0.60f);
    s.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.65f);
}

void DrawWindowShadow(ImVec2 a, ImVec2 b, float rounding) {
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    for (int i = 3; i >= 1; --i) {
        float s = (float)i * 2.0f;
        bg->AddRectFilled(a - ImVec2(s, s + 1.0f), b + ImVec2(s, s + 3.0f),
                          A(g_pal.shadow, 0.35f / (float)i), rounding + s);
    }
}

void DrawGlassWindow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float alpha) {
    // Base glass: intentionally high opacity so bright game areas do not kill text.
    dl->AddRectFilled(a, b, A(g_pal.surface1, alpha), rounding);

    // Top specular band, starting below the radius so corners stay clean.
    float band = ImMin(28.0f, (b.y - a.y) * 0.30f);
    dl->AddRectFilledMultiColor(
        ImVec2(a.x + 1.0f, a.y + rounding),
        ImVec2(b.x - 1.0f, a.y + band),
        A(g_pal.glassSheen, alpha), A(g_pal.glassSheen, alpha),
        A(g_pal.glassSheen, 0.0f), A(g_pal.glassSheen, 0.0f));

    // Bottom shade for weight.
    float shade = ImMin(24.0f, (b.y - a.y) * 0.20f);
    dl->AddRectFilledMultiColor(
        ImVec2(a.x + 1.0f, b.y - shade),
        ImVec2(b.x - 1.0f, b.y - rounding),
        A(g_pal.glassScrim, 0.0f), A(g_pal.glassScrim, 0.0f),
        A(g_pal.glassScrim, alpha), A(g_pal.glassScrim, alpha));

    // Outer + inner edge.
    dl->AddRect(a, b, A(g_pal.border, alpha), rounding, 1.0f);
    dl->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f),
                A(g_pal.glassSheen, alpha), rounding - 1.0f, 1.0f);
}

bool DrawTitleBar(const char* title, const char* subtitle, bool* open, float height) {
    ImVec2 pos  = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Base + vertical gradient. Top 8 px solid cap keeps the rounded corners clean.
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + height),
                      A(g_pal.surface3, 1.0f), 8.0f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x + 1.0f, pos.y + 8.0f),
        ImVec2(pos.x + size.x - 1.0f, pos.y + height),
        A(g_pal.titleTop, 1.0f), A(g_pal.titleTop, 1.0f),
        A(g_pal.titleBottom, 1.0f), A(g_pal.titleBottom, 1.0f));

    // Bottom hairline + top highlight.
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1.0f),
                ImVec2(pos.x + size.x, pos.y + height - 1.0f),
                A(g_pal.border, 0.90f), 1.0f);
    dl->AddLine(ImVec2(pos.x + 8.0f, pos.y + 0.5f),
                ImVec2(pos.x + size.x - 8.0f, pos.y + 0.5f),
                A(g_pal.glassSheen, 0.60f), 1.0f);

    // Accent mark + title/subtitle.
    dl->AddRectFilled(ImVec2(pos.x + 8.0f, pos.y + 10.0f),
                      ImVec2(pos.x + 11.0f, pos.y + 26.0f), g_pal.accent, 2.0f);
    dl->AddText(ImGui::GetFont(), 15.0f, ImVec2(pos.x + 16.0f, pos.y + 8.0f),
                g_pal.text, title);
    if (subtitle && subtitle[0])
        dl->AddText(ImGui::GetFont(), 11.0f, ImVec2(pos.x + 16.0f, pos.y + 22.0f),
                    g_pal.textDim, subtitle);

    // Drag area excludes the caption button.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + 4.0f));
    ImGui::InvisibleButton("##title_drag", ImVec2(size.x - 56.0f, height - 8.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(pos.x + d.x, pos.y + d.y));
    }

    // Close button.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 44.0f, pos.y + 4.0f));
    ImGui::InvisibleButton("##title_close", ImVec2(40.0f, height - 8.0f));
    bool hovered = ImGui::IsItemHovered();
    ImVec2 bbMin = ImGui::GetItemRectMin();
    ImVec2 bbMax = ImGui::GetItemRectMax();
    if (hovered)
        dl->AddRectFilled(bbMin, bbMax, A(g_pal.error, 0.85f), 4.0f);
    ImU32 glyph = hovered ? IM_COL32_WHITE : A(g_pal.textDim, 1.0f);
    ImVec2 c((bbMin.x + bbMax.x) * 0.5f, (bbMin.y + bbMax.y) * 0.5f);
    dl->AddLine(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), glyph, 1.5f);
    dl->AddLine(ImVec2(c.x + 4.0f, c.y - 4.0f), ImVec2(c.x - 4.0f, c.y + 4.0f), glyph, 1.5f);
    if (ImGui::IsItemClicked()) {
        if (open) *open = false;
        return false;
    }
    return true;
}

bool BeginGlassWindow(const char* name, bool* open, ImVec2 size, ImGuiWindowFlags flags) {
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    bool visible = ImGui::Begin(name, open, flags | ImGuiWindowFlags_NoTitleBar |
                                                  ImGuiWindowFlags_NoBackground |
                                                  ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar(2);
    if (!visible) {
        ImGui::End();
        return false;
    }

    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 sz  = ImGui::GetWindowSize();
    DrawWindowShadow(pos, pos + sz, 8.0f);
    DrawGlassWindow(ImGui::GetWindowDrawList(), pos, pos + sz, 8.0f, 1.0f);
    DrawTitleBar(name, "PBR / Modern", open, 36.0f);

    ImGui::SetCursorPos(ImVec2(0.0f, 36.0f));
    ImGui::BeginChild("##glass_content", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    return true;
}

void EndGlassWindow() {
    ImGui::EndChild();
    ImGui::End();
}

float Smooth(ImGuiID id, float target, float speed, float dt) {
    float& v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    dt = ImClamp(dt, 1.0f / 240.0f, 1.0f / 30.0f);
    float k = 1.0f - expf(-speed * dt);
    v += (target - v) * k;
    return v;
}

} // namespace DebugMenuTheme
```

Integration points:

```cpp
// debugmenu_ui.cpp: EnsureImGuiInit()
ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();
io.IniFilename = nullptr;                 // don't write imgui.ini into the game dir
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
io.MouseDrawCursor = true;
DebugMenuTheme::ApplyTheme(/*dpiScale=*/1.0f);
// load Selawik + JetBrains Mono here
ImGui_ImplDX9_Init(device);
ImGui_ImplWin32_Init(hwnd);
```

```cpp
// debugmenu_ui.cpp: DrawUnifiedDebugMenu()
if (Theme::BeginGlassWindow("SkyGFX", &open, ImVec2(1280, 760))) {
    // existing content, but inside the glass child
    Theme::EndGlassWindow();
}
```

Do not call `ApplyTheme()` every frame; it resets `StyleColorsDark()` and all overrides.

---

## 6. What will break / limits

### 6.1 Device reset — the critical one

This repo hooks `IDirect3DDevice9::Reset` in `src/render/depthhook.cpp:329` and calls `OnDeviceReset()` before `orig_Reset()`. `OnDeviceReset()` (`src/render/postfx.cpp:2264`) releases many default-pool resources, but it does **not** touch ImGui.

That is a real bug, not a theoretical one:

- `ImGui_ImplDX9_RenderDrawData()` creates its dynamic vertex/index buffers in `D3DPOOL_DEFAULT` and only releases them in `ImGui_ImplDX9_InvalidateDeviceObjects()` (`imgui_impl_dx9.cpp:189-201`, `:421-436`).
- The 1.92 backend also creates the font atlas texture in `D3DPOOL_DEFAULT` (`imgui_impl_dx9.cpp:347-352`).
- D3D9 requires all `D3DPOOL_DEFAULT` resources to be released before `Reset`. If they are alive, `Reset` can fail with `D3DERR_INVALIDCALL`; the repo's own comment at `depthhook.cpp:338-350` describes the crash path that follows a failed reset.

Fix:

```cpp
// src/core/debugmenu_ui.cpp
void DebugMenu_OnDeviceReset() {
    if (ImGui::GetCurrentContext())
        ImGui_ImplDX9_InvalidateDeviceObjects();
}
```

```cpp
// src/render/postfx.cpp — OnDeviceReset(), before orig_Reset()
extern void DebugMenu_OnDeviceReset();
DebugMenu_OnDeviceReset();
```

```cpp
// src/render/depthhook.cpp — after SUCCEEDED(orig_Reset)
ImGui_ImplDX9_CreateDeviceObjects(); // no-op in 1.92 but keeps the contract
```

Guard with `unifiedImGuiInited` if the menu was never opened. After recreation, the backend will lazily re-upload the font atlas on the next frame. If you add a custom `D3DPOOL_DEFAULT` noise texture, release/recreate it here too; use `D3DPOOL_MANAGED` to avoid the problem.

### 6.2 `D3DSBT_ALL` interactions

`ImGui_ImplDX9_RenderDrawData()` creates a `D3DSBT_ALL` state block, captures all device state, sets up its own fixed-function state, renders, then applies the state block and releases it (`imgui_impl_dx9.cpp:204-212`, `:311-312`). Consequences:

- **Procedural `ImDrawList` work is safe.** It only contributes vertices; no device state changes.
- **`AddCallback` is where things break.** A callback runs inside the ImGui render loop. If it changes blend/shader/texture/render-target state and does not restore it, subsequent ImGui commands render incorrectly. Use `ImDrawCallback_ResetRenderState` after the custom block, or avoid callbacks entirely. The ImGui issue tracker explains the callback contract ([#876](https://github.com/ocornut/imgui/issues/876)).
- **Do not change render targets inside a callback.** The state block will restore them at the end, but every draw between the callback and the restore uses the wrong target.
- **Do not call `BeginStateBlock`/`EndStateBlock` while the backend is rendering.** State block recording is global device state; nesting it inside the backend's capture/render window is asking for `D3DERR_INVALIDCALL`.
- The state block captures textures too; a custom user texture is fine, but if you release it mid-frame you can leave a dangling reference in the block.

For this theme, there is no reason to use `AddCallback`. The only tempting case is a true additive sheen; approximate it with alpha layers instead.

### 6.3 Alpha, premultiplied alpha, and glass over the game

The DX9 backend uses **straight alpha**:

```
SRCBLEND = D3DBLEND_SRCALPHA
DESTBLEND = D3DBLEND_INVSRCALPHA
SEPARATEALPHABLENDENABLE = TRUE
SRCBLENDALPHA = D3DBLEND_ONE
DESTBLENDALPHA = D3DBLEND_INVSRCALPHA
```

Source: `imgui_impl_dx9.cpp:117-123`.

That means:

- `IM_COL32(r,g,b,a)` is straight alpha. Keep it that way.
- Textures used with `AddImage` must also be straight alpha. If you import a premultiplied PNG, edges will look dark/fringed; either unpremultiply at load time or switch the blend mode for that texture (which would require a backend change and a callback). Do not premultiply ImGui colors.
- The alpha channel accumulation (`ONE` / `INVSRCALPHA`) is not the same as the color accumulation; it is designed to preserve the destination alpha. The game usually ignores backbuffer alpha after the UI pass, but if a later pass reads it, the menu can affect it. Draw the menu late.
- A true additive glow needs `D3DBLEND_ONE` / `D3DBLEND_ONE`. You cannot express that through `ImGuiStyle`; you would need a callback. Approximate a glow with 2-3 overlapping alpha rects at 10-25% instead. It reads as a glow on a dark surface and avoids state changes.
- Glass over a bright sky: the composite is `src*a + dst*(1-a)`. At a=0.94, only 6% of the game shows through — good for text, still enough to feel translucent. At a=0.72 (child panels), 28% shows through; use child panels only for large controls, never small text.
- The title bar can be more transparent than the body because its text is large and has an accent mark; still keep it >= 0.82.

### 6.4 Performance

- A themed menu adds **vertices, not draw calls**: a rounded rect is ~40-80 vertices; 100 custom elements is a few thousand vertices. That is small compared with the existing text/table vertex load.
- One extra noise texture adds **one draw call** (texture change). Keep all custom effects on the same texture or no texture to preserve batching.
- The expensive part is already there: `CreateStateBlock(D3DSBT_ALL)` + `Capture()` + `Apply()` every frame. The theme does not add state blocks. Do not add per-widget state blocks or per-widget `AddCallback`.
- Never create a D3D texture, vertex buffer, or state block inside the menu draw. Cache everything at init.
- The font atlas can grow and re-upload; pre-size it (`TexMinWidth=1024`, `TexMinHeight=512`) and avoid loading unnecessary glyph ranges.
- Large tables should use `ImGuiListClipper`; the existing settings inventory is large. The nav restructure should not make every tab build all rows every frame.
- If you implement manual backbuffer blur, budget a resolve + two passes + state save/restore; that is the only part of this design that can cost milliseconds. Don't.

### 6.5 Font atlas memory

| Atlas | RGBA32 memory | Notes |
|---|---:|---|
| 512x128 | 256 KB | 1.92 initial size. |
| 1024x512 | 2 MB | Good target for UI + mono, Latin-1. |
| 1024x1024 | 4 MB | Safe if you add icons/Cyrillic. |
| 2048x2048 | 16 MB | Overkill for this menu; avoid. |

The TTF data is separate (~300-500 KB per weight). Two weights + one mono font is fine. The 1.92 dynamic atlas grows and copies; a growth hitch is possible on first open, so pre-size and optionally pre-warm by drawing the menu once off-screen.

### 6.6 ImGui 1.92 API drift and the build path

The build uses `E:/SDKs/imgui-master` (`premake5.lua:21-23`), not `external/imgui`. They are both 1.92.9 WIP but not byte-identical. Before writing code:

- Compile against the same headers the build uses. If you edit `external/imgui` only, the build will not see it.
- `AddRect` argument order changed in 1.92.8: `(p_min, p_max, col, rounding, thickness, flags)`. The old overload is deleted.
- `ImTextureID` is `ImU64`; `AddImage`/`Image` take `ImTextureRef`. Use `(ImTextureID)(intptr_t)tex`.
- Tab colors were renamed in 1.90.9 (`TabActive` -> `TabSelected`, etc.); the old names are aliases only if obsolete functions are enabled.
- `ImGuiCol_CheckboxSelectedBg`, `TabSelectedOverline`, `NavCursor`, `TreeLines`, `UnsavedMarker`, `InputTextCursor` are newer; they exist in this 1.92.9 header.
- `FontSizeBase`, `FontScaleMain`, `FontScaleDpi`, `WindowBorderHoverPadding`, `ScrollbarPadding` are 1.92 fields. Older tutorials will not compile.

Pin the ImGui version in the plan. If the SDK copy is updated, re-run the style compile check and the visual regression.

### 6.7 Layout, clipping, and ID pitfalls

- `NoTitleBar` removes drag; reimplement it or the window becomes immovable.
- `NoBackground` still draws a border if `WindowBorderSize > 0`; push 0 around the custom window.
- The window draw list is clipped to the window rect; an outer shadow drawn there is invisible. Use `GetBackgroundDrawList()` or inset padding.
- `AddRectFilledMultiColor` has no rounding; a square gradient over a rounded window shows at the corners. Start the gradient below the radius or draw strips.
- Custom nav items and title buttons need stable IDs. `PushID(label)` around each nav item; `##title_close` for the close button.
- `ImGui::Selectable` is discouraged for contiguous rounded selections (`SelectableRounding` warning in `imgui.h`); use `InvisibleButton` + custom draw for the nav rail.
- Nested child windows each get their own clip rect and scrollbar; too many nested children cause scrollbar-inside-scrollbar. Limit to window -> content child -> one panel child.
- `BeginChild` with `ImGuiWindowFlags_NoBackground` still uses `ChildBg` for hit-testing? It draws no background, but the parent glass shows through; that is intended.
- If you resize the window, `s_lastSize` in the current code feeds the next frame. Keep that logic; do not fight it with a fixed `SetNextWindowSize`.

### 6.8 Legibility over the game

This is the most likely visual failure:

- A bright sky through 20% glass destroys 13 px text. Keep body opacity >= 0.92 and add a 20-25% black scrim behind dense text.
- Do not use pure white `#FFFFFF` on pure black; use `#E9EEEE` on `#121A1D` to reduce halation.
- Do not use gray-on-gray "subtle" text; the secondary `#A9B7BB` is 8.5:1, disabled `#718287` is 4.4:1.
- Do not rely on color alone for status LEDs; pair with `READY`, `WARN`, `ERROR`.
- Test against: bright midday sky, night, rain, fire/explosion, headlights, interior. If it fails on one, raise opacity or add a scrim; do not add more blur.

### 6.9 Input, cursor, and focus

- `io.MouseDrawCursor = true` is already set because GTA hides the OS cursor. Keep it; the ImGui cursor is part of the overlay.
- `ImGuiConfigFlags_NavEnableKeyboard` is already set. Add a visible focus ring (`ImGuiCol_NavCursor`) and make sure the custom nav items participate in nav (use `InvisibleButton` + `SetItemDefaultFocus` when the page changes).
- The WndProc subclass consumes input only when `config->debugMenuOpen`; keep that behavior. A custom title bar must not swallow game hotkeys when the menu is closed.
- If you add gamepad navigation, test that the game's controller input is not doubled. This is out of scope for v1.

---

## 7. Implementation plan

Four shippable steps. Each step is independently revertible; the menu should remain usable after every step.

### Step 1 — Theme constants + style pass

**Goal:** replace the default ImGui look with the palette and typography, without changing layout.

**Files:**
- **New:** `src/core/debugmenu_theme.h`
- **New:** `src/core/debugmenu_theme.cpp`
- `src/core/debugmenu_ui.cpp` — include the header; in `EnsureImGuiInit()` call `DebugMenuTheme::ApplyTheme()` and load fonts; set `io.IniFilename = nullptr`.
- `premake5.lua` — add `files { "src/core/*.*" }` (the current `src/Core/*.*` works on Windows but is case-sensitive on other tooling). Verify the new `.cpp` is compiled.
- Optional: `src/skygfx.h` / `src/core/main.cpp` — add `uiTheme` / `uiAnimations` INI keys, default on.

**Acceptance:**
- `python tools/fast_build.py` succeeds.
- Ctrl+4 opens the menu; no ImGui assert; no missing style field compile error.
- All 9 tabs render with the new palette; text is readable on the darkest and lightest panel.
- The menu is still 1280x760, draggable, resizable, and closable via the default title bar (layout unchanged in this step).
- No `imgui.ini` appears in the game directory.
- Screenshot at 1920x1080 for the palette review.

**Rollback:** set `uiTheme=0` and skip `ApplyTheme()`; the rest of the menu is unchanged.

### Step 2 — Custom window/title rendering

**Goal:** Aero/Bento chrome: glass body, gradient title bar, accent mark, caption close, drag, shadow.

**Files:**
- `src/core/debugmenu_theme.cpp` — `DrawGlassWindow`, `DrawWindowShadow`, `DrawTitleBar`, `BeginGlassWindow`, `EndGlassWindow`.
- `src/core/debugmenu_ui.cpp` — replace `ImGui::Begin("SkyGFX", &open)` / `ImGui::End()` with the theme pair; preserve `s_lastSize`, `SetNextWindowSize`, and the close behavior.

**Acceptance:**
- Drag by the title bar; resize from all edges/corners; close via the caption button and via `open=false`.
- The title bar does not cover the first content row; the content child starts at y=36.
- The outer shadow is visible and is not clipped by the window.
- Popups, combos, and tooltips still have a 1 px border and are readable.
- No double border, no square gradient corners, no title-bar click-through.
- Game rendering after closing the menu is unchanged (state restored).
- Screenshot with a bright sky behind the menu.

**Rollback:** restore the default `ImGui::Begin`/`End`; the theme style from Step 1 remains.

### Step 3 — Panel/nav rail restructure

**Goal:** replace the top tab strip with a Bento-style left nav rail, stacked collapsible panels, and a status strip.

**Files:**
- `src/core/debugmenu_ui.cpp` — replace `BeginTabBar` / `BeginTabItem` with `DrawNavRail()` + a page switch; keep the existing tab bodies as page draw functions. Move Save/Reload/Reset/Copy into a footer toolbar or the title-bar menu. Add the status strip and reserve its 24 px.
- `src/core/debugmenu_theme.cpp` — `NavItem`, `PanelHeader`, `StatusLed`, `Separator` helpers.
- No changes to the settings rows themselves; this is layout only.

**Acceptance:**
- All 9 pages reachable from the nav rail; the current Main two-column layout still works.
- The settings search filter still filters every page.
- Keyboard navigation: Tab/arrow/Enter/Esc; focus ring visible; no focus trap.
- 1280x760 and 1920x1080 both fit; no horizontal scrollbar; no clipped labels.
- The status strip shows READY/MODIFIED, profile, pipeline, FPS/ms.
- No duplicate ImGui IDs (check the debug log / `ImGui::ShowIDStackTool` if needed).

**Rollback:** keep the old tab bar behind `uiLayout=0`; switch in one line.

### Step 4 — Polish, animation, reset safety

**Goal:** make it feel finished and safe: hover/focus animation, LEDs, separators, noise, reduced motion, device-reset handling.

**Files:**
- `src/core/debugmenu_theme.cpp` — `Smooth`, hover/focus helpers, LED/separator drawing, noise texture init/release.
- `src/core/debugmenu_ui.cpp` — apply hover/focus to nav/buttons/rows; status readouts; reset storage on menu close.
- `src/render/postfx.cpp` / `src/render/depthhook.cpp` — ImGui invalidate/create on reset (Section 6.1).
- `src/skygfx.h` / `src/core/main.cpp` — `uiAnimations` INI key; optional `uiTheme`.
- Optional: `src/core/debugmenu_theme.cpp` — one 64x64 noise texture; release/recreate on reset.

**Acceptance:**
- 60 fps at 1080p with the menu open; frame-time delta vs. baseline < 0.2 ms (measure with the existing profiler).
- Alt+Tab, resolution change, windowed/fullscreen toggle, and `IDirect3DDevice9::Reset` stress test: no `D3DERR_INVALIDCALL`, no dead font texture, menu reopens correctly.
- Hover/focus animation is smooth; no layout jitter; no animation while the menu is closed.
- `uiAnimations=0` disables animation immediately.
- No `imgui.ini`; no new per-frame texture/state-block allocations.
- Final visual pass on bright sky/night/rain; all text passes 4.5:1 on its actual background.

**Rollback:** `uiAnimations=0`, remove the reset calls, keep Steps 1-3.

### Suggested order of work

1. Step 1 in one PR (style + fonts + palette). This is the highest-value, lowest-risk change.
2. Step 2 in a second PR (chrome). This is where most visual bugs appear; budget time for screenshot iteration.
3. Step 3 in a third PR (layout). This is the largest diff; keep the old tab bar behind a flag until the new rail is proven.
4. Step 4 in a fourth PR (polish + reset fix). The reset fix should ship even if the rest of Step 4 slips.

---

## 8. Reference gallery

Twelve links that best represent the target look, with what to steal.

| # | Link | What to steal |
|---:|---|---|
| 1 | [Microsoft — Window Frames (Aero UX guide)](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-window-frames) | Glass is for chrome, not content; use it sparingly and keep content opaque. |
| 2 | [MSDN Magazine — Aero Glass: Create Special Effects with the DWM](https://learn.microsoft.com/en-us/archive/msdn-magazine/2007/april/aero-glass-create-special-effects-with-the-desktop-window-manager) | Aero is a layer stack: blur, tint, reflection, highlight. Copy the stack, not one gradient. |
| 3 | [MakeAero — Window Glass generator](https://makeaero.com/window-glass) | Concrete reconstruction: 8 px radius, 9 px frame, 30 px title, 29x20 captions, 46 px close, top specular + diagonal sheen. |
| 4 | [NameThatUI — Windows Aero](https://namethatui.com/styles/windows-aero) | The state language: 1 px inner edge, glowing hot controls, close floods red, min/max glow cyan. |
| 5 | [Fluent 2 — Shapes](https://fluent2.microsoft.design/shapes) | 4/8/12 radius tokens; small shapes get smaller radii; don't round everything. |
| 6 | [Fluent 2 — Material](https://fluent2.microsoft.design/material) | Acrylic for transient chrome, Mica/solid for long-lived content; material is functional. |
| 7 | [Material 3 — Corner radius scale](https://m3.material.io/styles/shape/corner-radius-scale) | 8/12/16 for panels/cards; 28+ is for sheets/dialogs, not a dense overlay. |
| 8 | [Material 3 Expressive](https://m3.material.io/blog/building-with-m3-expressive) | Shape + motion can signal state; use a whisper of it (150 ms ease-out), not a bounce. |
| 9 | [Apple HIG — Materials](https://developer.apple.com/design/human-interface-guidelines/materials) | Liquid Glass is a distinct functional layer; don't glass the content. |
| 10 | [WinampHeritage — Winamp player overview (Bento)](https://winampheritage.com/overview) | Unified shell, left-side access to Media Library/Playlist/EQ/themes, 20+ color themes, dense lists. |
| 11 | [WACUP — Big Bento Modern v1.2](https://getwacup.com/community/index.php?topic=327.0) | The modernized Bento: high-res readability, larger fonts, smooth animations, light mode. |
| 12 | [WACUP — Screenshot gallery](https://getwacup.com/screenshots) | The actual Big Bento Modern layout; use it for panel proportions and density. |

**Also worth opening:**

- [Quinto Black CT](https://quinto-black-ct.info/) — dark skeuomorphic Winamp skin; restrained bevels and strong title bar. Good reference for "modern retro, not 2007 clone."
- [Winamp Skin Museum](https://skins.webamp.org) — Bento variants and color-theme ideas; useful for the 4 presets.
- [NN/g — Glassmorphism: Definition and Best Practices](https://www.nngroup.com/articles/glassmorphism) — contrast, blur, and border discipline; why glass fails over busy backgrounds.
- [Atlassian — Spacing](https://atlassian.design/foundations/spacing) — the 8 px base unit and token ramp.
- [LittleFox2024/Aero7-11](https://github.com/LittleFox2024/Aero7-11), [Aero11](https://winclassic.net/thread/3047/aero11-accurate-comprehensive-vista-theme), [OpenGlass](https://github.com/ALTaleX531/OpenGlass) — 2024-2026 Aero revival; what a modern Aero port keeps and drops.

---

## Appendix A — Current menu inventory (what the nav rail must cover)

Top tabs today (`debugmenu_ui.cpp:3469-3579`):

1. **Main** — left switchboard (364 px) + right effect tabs.
2. **All Settings** — full coverage inventory grouped by subsystem + live diagnostics.
3. **Vehicle** — chrome/env/veh* + car-pipe shading.
4. **Vegetation** — grass/tree features.
5. **PostFX** — every post-processing pass.
6. **Utility** — noise quality, hotkeys, startup-only keys.
7. **Profiler** — pass/frame timings, draw counters, shader binds.
8. **Presets** — five pipeline presets.
9. **INI Keys** — full key inventory, filterable.

Bottom actions today (`:3596-3665`): Save INI, Reload INI, Reset Values, Copy settings as INI, config name, Save as New Config, Saved configs combo, Load Config. The nav restructure should not lose any of these; move them to a footer toolbar and a title-bar "≡" menu.

Suggested nav rail grouping:

| Rail item | Contains |
|---|---|
| **Dashboard** | Main (pipeline + effects + rendering features). |
| **Vehicle** | Vehicle shading rows. |
| **World** | Vegetation + world/lighting rows. |
| **PostFX** | PostFX passes. |
| **Performance** | Profiler + Forward+ diagnostics. |
| **Presets** | Pipeline presets + named configs. |
| **Settings** | All Settings coverage view. |
| **INI Keys** | Reference inventory. |
| **Utility** | Noise, hotkeys, startup-only keys. |

This keeps the existing tab bodies intact and only changes the outer navigation.

---

## Appendix B — Acceptance checklist

**Build / correctness**

- [ ] `python tools/fast_build.py` passes with no new warnings.
- [ ] `Ctrl+4` opens/closes the menu; `Esc` does not close the game.
- [ ] No ImGui assert; no `AddRect` signature errors; no missing style fields.
- [ ] `ImGui_ImplDX9_InvalidateDeviceObjects()` runs before `Reset`; `CreateDeviceObjects()` after.
- [ ] Alt+Tab, resolution change, fullscreen toggle: menu survives, no `D3DERR_INVALIDCALL`.
- [ ] No `imgui.ini` in the game directory.
- [ ] No per-frame texture/state-block creation.

**Visual**

- [ ] Window radius 8, panel 6-8, control 6; 1 px borders.
- [ ] Glass body >= 0.92 opacity; title/nav >= 0.82.
- [ ] Top specular band + 1 px inner highlight visible but subtle.
- [ ] Noise visible at 3% (no banding) but not gritty.
- [ ] Accent only on active/focus/primary; no large taxi-yellow fills.
- [ ] Text primary/secondary/disabled tiers are distinguishable.
- [ ] Status LEDs always paired with a label.
- [ ] No heavy drop shadows, no bevels, no white text glow.

**Layout**

- [ ] All 9 pages reachable; search filter works on every page.
- [ ] 1280x760: no clipped labels, no horizontal scrollbar, footer visible.
- [ ] 1920x1080 / 2560x1440 / 3840x2160: font and spacing scale, 1 px borders remain crisp.
- [ ] Keyboard nav works; focus ring visible; no focus trap.
- [ ] Popups/combos/tooltips are styled and readable.

**Performance**

- [ ] Menu-open frame time delta < 0.2 ms vs. baseline at 1080p.
- [ ] No font atlas growth hitch after first open.
- [ ] No extra draw calls beyond the noise texture (if used).
- [ ] `uiAnimations=0` produces no animation and no visual jump.

---

## Sources

**Aero / DWM / Windows**

- Microsoft — Window Frames: https://learn.microsoft.com/en-us/windows/win32/uxguide/win-window-frames
- Microsoft — DWM Blur Behind overview: https://learn.microsoft.com/en-us/windows/win32/dwm/blur-ovw
- Microsoft — DwmEnableBlurBehindWindow: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow
- Microsoft — DwmGetColorizationColor: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmgetcolorizationcolor
- MSDN Magazine — Aero Glass: https://learn.microsoft.com/en-us/archive/msdn-magazine/2007/april/aero-glass-create-special-effects-with-the-desktop-window-manager
- Microsoft — NONCLIENTMETRICS: https://learn.microsoft.com/en-us/previous-versions/ms724506(v=vs.85)
- Microsoft — Fonts (Segoe UI 9 pt): https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-fonts
- Microsoft — Selawik: https://learn.microsoft.com/en-us/typography/font-list/selawik
- Wikipedia — Windows Aero: https://en.wikipedia.org/wiki/Windows_Aero
- MakeAero — Window Glass: https://makeaero.com/window-glass
- NameThatUI — Windows Aero: https://namethatui.com/styles/windows-aero
- OpenGlass: https://github.com/ALTaleX531/OpenGlass
- Aero7-11: https://github.com/LittleFox2024/Aero7-11
- Aero11: https://winclassic.net/thread/3047/aero11-accurate-comprehensive-vista-theme

**Modern UI / design systems**

- Fluent 2 — Shapes: https://fluent2.microsoft.design/shapes
- Fluent 2 — Material: https://fluent2.microsoft.design/material
- Fluent 2 — Color: https://fluent2.microsoft.design/color
- Fluent 2 — Layout: https://fluent2.microsoft.design/layout
- Fluent 2 — Design tokens: https://fluent2.microsoft.design/design-tokens
- Material 3 — Shape: https://m3.material.io/styles/shape
- Material 3 — Corner radius scale: https://m3.material.io/styles/shape/corner-radius-scale
- Material 3 Expressive: https://m3.material.io/blog/building-with-m3-expressive
- Material 3 — Motion: https://m3.material.io/styles/motion/overview/how-it-works
- Apple HIG — Materials: https://developer.apple.com/design/human-interface-guidelines/materials
- Apple — Adopting Liquid Glass: https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass
- Atlassian — Spacing: https://atlassian.design/foundations/spacing
- NN/g — Glassmorphism: https://www.nngroup.com/articles/glassmorphism
- Axess Lab — Glassmorphism accessibility: https://axesslab.com/glassmorphism-meets-accessibility-can-frosted-glass-be-inclusive
- Muzli — 2026 UI patterns: https://muz.li/blog/whats-changing-in-mobile-app-design-ui-patterns-that-matter-in-2026/
- Dark Mode UI guide 2026: https://uxmagic.ai/blog/dark-mode-ui-design-guide

**Winamp / Bento**

- WinampHeritage — overview: https://winampheritage.com/overview
- Wikipedia — Winamp (Bento unified window): https://en.wikipedia.org/wiki/Winamp
- WACUP — Big Bento Modern v1.2: https://getwacup.com/community/index.php?topic=327.0
- WACUP — Screenshot gallery: https://getwacup.com/screenshots
- WACUP — ONYX v2: https://getwacup.com/community/index.php?topic=2680.0
- Quinto Black CT: https://quinto-black-ct.info/
- Winamp Skin Museum: https://skins.webamp.org

**GTA carcols**

- Local: `E:/games/gtasa_skygfx_plus/data/carcols.dat` (taxi index 6 at line 29; `taxi, 6,1` at line 331).
- GTAMods Wiki — carcols.dat: https://gtamods.com/wiki/Carcols.dat
- Grand Theft Wiki — Carcols.dat: https://www.grandtheftwiki.com/Carcols.dat
- gtasrv.ru — SA-MP vehicle colors (`6 = d78e10`): https://www.gtasrv.ru/readme/servers_vehicle_colors.html

**ImGui / D3D9**

- Dear ImGui — repository: https://github.com/ocornut/imgui
- Dear ImGui — FONTS.md (1.92 dynamic fonts): https://github.com/ocornut/imgui/blob/master/docs/FONTS.md
- Dear ImGui — Draw List wiki: https://github.com/ocornut/imgui/wiki/Draw-List
- Dear ImGui — DX9 example (device reset): https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx9/main.cpp
- Dear ImGui — DX9 backend: https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx9.cpp
- Dear ImGui — animation issue #1857: https://github.com/ocornut/imgui/issues/1857
- Dear ImGui — premultiplied alpha issue #1035: https://github.com/ocornut/imgui/issues/1035
- Dear ImGui — AddCallback issue #876: https://github.com/ocornut/imgui/issues/876
- Microsoft — Lost Devices (D3D9): https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices
- Microsoft — State Blocks Save and Restore State: https://learn.microsoft.com/en-us/windows/win32/direct3d9/state-blocks-save-and-restore-state
- Microsoft — Alpha Blending (D3D9): https://learn.microsoft.com/en-us/windows/win32/direct3d9/alpha-blending
