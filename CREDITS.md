# Credits & Attribution

> **Reconciled 2026-11-07.** Credit truth consolidated from `readme.md` (Zeneric fork lineage — commit `5cc5d25`), `docs/Credits.md` (contributors + reference-source table), `docs/Project Lineage.md` (aap → junior_dr → skygfx_plus) and `docs/BRDF Reference.md` (academic citations). Every project/doc path cited below was existence-checked on disk during this reconciliation; stale entries are marked.
>
> **Corrected 2026-11-09.** The aap reference checkout is `E:\SDKs\skygfx_original` (github.com/aap/skygfx @ `926616a`, v4.2b); `E:\SDKs\skygfx` is the **Dryxio** fork, not aap. Fork chain: **aap → JuniorDjjr (SkyGfx Extended) → Zeneric09 → skygfx_plus** — Zeneric's fork is *of junior's code*, and this repository was started by **rewriting Zeneric's implementation as the starter base**. `backup_original/` is the **Zeneric09** snapshot (junior-derived, a strict superset of JuniorDjjr's SkyGfx Extended). Reference checkouts re-verified below.

---

## SkyGFX Plus (this repository) — core credits

### Fork lineage (oldest → current)

- **aap** — original **SkyGFX** author; MIT-licensed foundation of every fork below. Cited in `readme.md`, `premake5.lua` (resource metadata still points at github.com/aap/skygfx) and `docs/Project Lineage.md`. Reference checkout: `E:\SDKs\skygfx_original` (github.com/aap/skygfx @ `926616a` = v4.2b). **`E:\SDKs\skygfx` is not aap** — it is the **Dryxio** fork (MTA device-reset support), outside this lineage.
- **Junior (JuniorDjjr)** — the **junior fork** lineage, published as **SkyGfx Extended** (github.com/JuniorDjjr/skygfx @ `fb69df1`, checked out at `E:\SDKs\skygfx_junior`): noise based variation (originally shipped as "stochastic texturing", renamed project-wide in the W0.3 wave), wind animation, EDED plugin, texdb, extended colour filters. Traced in `docs/Project Lineage.md`.
- **Zeneric09** — author of the **Zeneric skygfx** fork, a fork **of JuniorDjjr's SkyGfx Extended code** and the direct base of this repository. Attributed in `readme.md` (first paragraph + Credits list) per commit `5cc5d25` ("credit Zeneric fork lineage"). No public Zeneric skygfx repo exists; the inherited snapshot survives in-repo as `backup_original/` (read-only evidence, never a build input) — Zeneric's layer is junior-derived and a strict superset of it (10 Zeneric-only files: `SMAA.cpp/.h`, `AreaTex.h`, `SearchTex.h`, `PC_GrassRenderer.cpp/.h`, `config.cpp`, `debugmenu_ui.cpp`, `normmap_stubs.cpp`, `postfx.h`).
- **skygfx_plus project team** — this repository is the **rewrite of Zeneric's implementation used as the starter base** (fork chain aap → junior → Zeneric → us), grown into its own systems (PBR building/vehicle pipes, water & sky overhaul, native softbody ragdoll, ImGui menu, diagnostics). Project identity: `.serena/memories/project.md`; evolution record: `docs/Project Lineage.md`; contributor notes: `docs/Credits.md`.

### Reference checkouts (verified 2026-11-09)

| Lineage | Repository / revision | Local checkout |
|---|---|---|
| aap (original SkyGFX) | github.com/aap/skygfx @ `926616a` (v4.2b) | `E:\SDKs\skygfx_original` |
| JuniorDjjr (SkyGfx Extended) | github.com/JuniorDjjr/skygfx @ `fb69df1` | `E:\SDKs\skygfx_junior` |
| Zeneric09 (our base) | no public repo — snapshot preserved in-repo | `backup_original/` (read-only evidence) |
| Dryxio (MTA fork, not in lineage) | github.com/Dryxio/skygfx @ `5daf7f6` | `E:\SDKs\skygfx` |
| aap SA v4.2b release tree | release archive (not a git checkout) | `E:\SDKs\skygfx_aap_v4.2b` |

### Companion mod — SilentPatchSA (coexists; do NOT patch its addresses)

- **DK22Pac (Silent)** — **SilentPatch / SilentPatchSA** and the **Normal Map Plugin**. SilentPatchSA is used alongside this mod and is a recommended companion (`readme.md`, Installation); **its owned addresses must not be patched** — the unpatch lineage (0x6FB17C, 0x748AA8, 0x53E0C8) is tracked in `.opencode/memory-bank/activeContext.md`, and corona z-test ownership is recorded in `docs/plans/2026-10-31-md-reconciliation.md`. No SilentPatch code is copied (`docs/Credits.md`). Reference checkout: `E:\SDKs\SilentPatch-dev`.
- **DK22Pac Normal Map Plugin** — rpNORMMAP pipeline reference; `rpnormmap.lib` is linked (see `DEPENDENCIES.md`); sources at `E:\SDKs\normalmap_byDK_1.01`.
- **_AG** — RenderWare documentation and reverse engineering (`docs/Credits.md`).

### Reference-only sources (studied — NO code from them ships in this project)

| Source | On disk | Used for |
|--------|---------|----------|
| Rockstar **GTASource** collection | `E:\dev(dave)\releases\GTASource` (incl. `GTA SA RE\source`) | GTA SA pipes/render-core study — `docs/plans/2026-10-24-gtasource-render-core.md` |
| **gta-reversed** | `E:\SDKs\gta-reversed` (also `E:\dev(dave)\releases\GTASource\gta-reversed-master`) | struct/offset validation — cited in `src/core/main.cpp` comments |
| **librw** | `E:\SDKs\librw` | RenderWare re-implementation study |
| **plugin-sdk** (DK22Pac) | `E:\SDKs\plugin-sdk-master` | struct/offset cross-checks (`docs/SDK Dependencies.md`) |
| **SMAA** (iryoku) | `E:\SDKs\smaa` (BSD `LICENSE.txt`); in-repo copy `SMAA_reference/` | subpixel morphological AA — ported into `src/render/postfx_smaa.cpp`; license: `SMAA_reference/LICENSE.txt` |
| **SADestruction** | `E:\SDKs\SADestruction` — **NO LICENSE FILE** | geometry-carving study ONLY (`docs/plans/2026-10-29-destruction-mod-inventory.md`) — **their code must never ship in this project** |
| CryEngine shaders / LibertyRecomp | per `docs/Credits.md` | PBR/BRDF conventions, filmic tonemap, postFX ideas — reimplemented for SM3.0 |

### Shipped third-party code (full detail in `DEPENDENCIES.md`)

- **Bullet Physics 2.87** — vendored at `third_party/bullet/` (ragdoll backend). zlib license vendored at `third_party/bullet/LICENSE.txt` (verbatim upstream, https://github.com/bulletphysics/bullet3); carved out of the `third_party/bullet/` gitignore so it tracks. Upstream caveat: files under `Extras/` and `examples/ThirdPartyLibs/` are NOT zlib — neither tree is vendored here.
- **Dear ImGui** 1.92.9 — in-game menu; the build compiles `E:\SDKs\imgui-master` (6 TUs), with a vendored mirror + `LICENSE.txt` at `external/imgui/`.
- **injector** (LINK/2012 / thelink2012) — hook helpers; git submodule `external/injector/` (`external/injector/LICENSE`, `.gitmodules`).
- **RenderWare 3.7 D3D9 headers** — copied into `external/d3d9/`.
- **linb ini_parser** — `external/ini_parser.hpp`.
- **SMAA** — BSD, license text at `SMAA_reference/LICENSE.txt` (see reference table above).

### Academic citations (full notes in `docs/BRDF Reference.md` and `docs/Credits.md`)

- Jensen et al. (2001) — subsurface light transport
- Walter et al. (2007) — GGX microfacet models
- Hable (Uncharted 2) — filmic tonemapping
- Burley (Disney, 2012) — Disney BRDF

### Broader inspiration

- **Crytek** — CryEngine rendering techniques (SSS, GGX BRDF); **Rockstar Games** — GTA IV/V/RDR2 rendering; **Criterion Games** — RenderWare Graphics engine; **O3DE Foundation** — Atom renderer PBR pipeline. No proprietary code from any of them is compiled into this project (`docs/Credits.md`).

---

## GTA San Andreas Stories 1987 — Total Conversion Mod

This installer exists ONLY to make it easier to install the **GTA San Andreas Stories 1987** total-conversion mod. All credit for the mod itself belongs to its development team. The installer is a fan-made tool, unaffiliated with Rockstar Games or Take-Two Interactive. (Installer implementation lives in `installer/`, PyQt5 UI in `installer/ui/`.)

### Official mod website
- **https://gtasas.netlify.app/**
- **Discord:** https://discord.gg/t5SjMrAJNh

### Mod credits (extracted from the official site's credits page)

**Lead Team:**
- **babamohammed2022** — Scripting, In-game Photography, Supervision, Optimization
- **Abdullah (AdevAbdullah)** — Website Design, Models, Suggestions, Programming
- **FrankoU** — Mapping Specialist, Artist, Server Administration, Retexturing, Screenshot Production, Artworks
- **avixreal** — Supervision, Website Assistance, Website Design
- **The Small Chese** — Mission Design, Creative Direction
- **Armando** — Sounds, Retexture, Supervisor, Tester, Screenshots, Missions Designer, Administration, Moderation, Suggestions, Videos and Clips, Bug Reports

**Contributors:**
- **Mike** — Missions, In-game Photography
- **JoGamer** — Animations, Modelling, Suggestions, Screenshots, Artistic collaboration; also fixed radar icons
- **GTAMissionsCreator** — Video Production
- **Noobshakespeare** — Retexturing, Mission Testing, Bug Report
- **karammii** — Billboards Design
- **Jmanuc** — Screenshots, Suggestions, Supervision, Optimization, Artworks, Atmosphere Enhancements, Research
- **Rayane** — Suggestions, Supervision
- **Seve267** — Scripting Help, DYOM Modder, Bug Fixer, Miscellaneous
- **Forge** — Scripting Help
- **Elrico** — Mission Testing, Promotional Screenshots, Server Contribution
- **Reaper** — HUD improvements, Weapon Icons, Supervision
- **Sadie** — Supervision
- **Rule Breakers** — Environmental Mapping
- **Bolszewik** — In-game Photography
- **14todoeltiempodc** — Weapon Retextures
- **NorthStationX** — Billboards Design, Logo Creation
- **FrankoU** — Special Mods (Vehicles Parked in Airport LS, Beta Sweet House, and others)
- **Lost MC Claude** — Bug Testing
- **MAHMOGAMER** — Project Supervision
- **MaxMakoShark** — Server Supervision
- **Tix** — Gameplay Testing
- **Raul** — Visual Enhancements, Mission Testing and Enhancing, Storyline Improvements, Video Recording, Supervision, Bug Reports
- **Urafael Games** — Coding Expertise and ASI Development
- **Fikan** — Remapping Artist

**Special Thanks:**
- Endochronic
- SASquad
- Yucark
- TheMadeMan

*(Provenance: official site credits page, consumed by the installer package `installer/` — see `installer/scraper.py` for source fetching.)*

---

## Supporting Mods (downloaded/linked by this installer)

The GTA SAS 1987 mod already bundles all required dependencies (per the official site's note: *"GTA San Andreas Stories already includes all required dependencies. The storyline is built on DYOM 8.1, which is already included and fully configured inside the archive."*). The installer offers these as **optional** extras for users who want to update them independently. (Selection/fetch logic: `installer/config.py`, `installer/downloader.py`, `installer/scraper.py`.)

### CLEO 5
- **Source:** https://github.com/cleolibrary/CLEO5/releases
- **Author:** CLEO Library team (cleolibrary)
- **License:** BSD 3-Clause (see https://github.com/cleolibrary/CLEO5/blob/master/LICENSE)
- **Purpose:** Required for running CLEO scripts in GTA San Andreas.

### CLEO+ (GitHub mirror)
- **Source:** https://github.com/JuniorDjjr/CLEOPlus/releases
- **Original page:** https://www.mixmods.com.br/2023/10/cleoplus/
- **Author:** JuniorDjjr
- **License:** See repo README — free for personal use
- **Purpose:** Enhances CLEO with more opcodes and features.

### NewOpcodes
- **Source:** https://www.mixmods.com.br/2020/10/newopcodes-cleo-v2-1/
- **GitHub repo:** https://github.com/cleolibrary/NewOpcodes
- **Author:** DK22Pac (plugin), cleolibrary (GitHub mirror)
- **License:** See MixMods page
- **Purpose:** Adds additional opcodes for CLEO compatibility and gameplay enhancements.

### DYOM 8.1 (Design Your Own Mission)
- **Source:** Bundled inside the main GTA SAS 1987 archive
- **Author:** Dutchy3010 & PatrickW (original DYOM team)
- **Website:** https://dyom.gtagames.nl/
- **License:** Free for personal use
- **Purpose:** The storyline of GTA SAS 1987 is built on DYOM 8.1.

---

## Tools & Services Used by the Installer

### GameCopyWorld — NO-CD patches
- **Source:** https://gamecopyworld.com/games/pc_gta_san_andreas.shtml
- **Used for:** Auto-downloading the HOODLUM v1.0 [ALL] No-CD/Fixed EXE patch when the user has a Steam v3.0 or SecuROM exe that needs downgrading.
- **Implementation:** `installer/gamecopyworld.py`
- **Credits:** GameCopyWorld team for hosting community game patches for over two decades.
- **Note:** The NO-CD patch is for use with your own legally-purchased game only. Please respect Rockstar's EULA and your local laws.

### LibertyCity.net — Mod mirror
- **Source:** https://libertycity.net/
- **Used for:** Fallback search when the main mod isn't found on the official gtasas.netlify.app site.
- **Implementation:** `installer/scraper.py`, `installer/downloader.py`
- **Credits:** LibertyCity.net team for hosting the largest GTA mod archive.

### MixMods.com.br — Brazilian GTA modding community
- **Source:** https://www.mixmods.com.br/
- **Used for:** Original article pages for CLEO+ and NewOpcodes (the installer prefers the GitHub mirror for CLEO+ because MixMods is behind Cloudflare bot protection).
- **Implementation:** `installer/scraper.py`
- **Credits:** JuniorDjjr and the MixMods team for the largest Portuguese-language GTA modding resource.

### GitHub — Mod source hosting
- **Used for:** CLEO 5 releases (cleolibrary/CLEO5), CLEO+ releases (JuniorDjjr/CLEOPlus), and the official GTA SAS 1987 website source (babamohammed2022/gta-1987-remastered-mod — where the splash images come from).
- **Implementation:** `installer/downloader.py`, `installer/scraper.py`
- **Credits:** GitHub Inc. for hosting the world's open-source code.

### MediaFire — Main mod file hosting
- **Used for:** The official GTA SAS 1987 main mod archive (`GTA_SAS_june_2026.rar`).
- **Implementation:** `installer/downloader.py`
- **Credits:** MediaFire LLC for free file hosting.

### Pricedown Bl font
- **Source:** https://www.dafont.com/pricedown.font
- **Author:** Typodermic Fonts Inc. (Ray Larabie)
- **License:** Free for commercial use (see `fonts/FONT_LICENSE.html` and `fonts/Typodermic_EULA.pdf` for full terms)
- **Used for:** The actual GTA logo typeface, used here for installer display text (`installer/ui/theme.py` loads `fonts/PricedownBl.otf`).

### GTA Vice City Stories carcols.dat palette
- **Reference:** https://gta.fandom.com/wiki/Carcols.dat/GTAVCS
- **Used for:** Color palette inspiration (dark greens preferred for legibility).
- **Credits:** Rockstar Games for the original VCS color table; GTA Fandom wiki for documenting it.

---

## Installer Development Credits

- **Installer design & code:** Built with PyQt5, Python 3.10+, and a lot of caffeine. Code: `installer/` (UI pages in `installer/ui/`).
- **PyQt5:** Riverbank Computing (https://www.riverbankcomputing.com/software/pyqt/) — GPL/commercial license. (Verified: imported by 11 files under `installer/ui/`.)
- **Requests:** Kenneth Reitz (https://requests.readthedocs.io/) — Apache 2.0. (Verified: `installer/downloader.py`.)
- **BeautifulSoup4:** Leonard Richardson (https://www.crummy.com/software/BeautifulSoup/) — MIT. (Verified: `installer/scraper.py`.)
- **py7zr:** Hiroshi Miura (https://github.com/miurahr/py7zr) — LGPL-3.0. (Verified: `installer/extractor.py`.)
- **rarfile:** Marko Kreen (https://github.com/markokr/rarfile) — ISC. (Verified: `installer/extractor.py`.)
- **psutil:** Giampaolo Rodola (https://github.com/giampaolo/psutil) — BSD 3-Clause. **STALE (2026-11-07):** no longer imported by `installer/` or `tools/` — kept for historical attribution only.
- **PyInstaller:** PyInstaller team (https://pyinstaller.org/) — GPL. **STALE (2026-11-07):** no active spec at repo root; the old spec is archived at `_memes/installer.spec`.
- **Build/tooling Python deps** (watchdog, numpy, Pillow, yt-dlp, PyPDF2) are pinned separately in `requirements.txt` — see `DEPENDENCIES.md`.

---

## Legal Disclaimer

This installer is a **fan-made tool** and is **not affiliated with, endorsed by, or sponsored by** Rockstar Games, Take-Two Interactive, or any of the mod authors listed above.

- **GTA San Andreas** is a trademark of Rockstar Games / Take-Two Interactive. You must own a legal copy of the game to use this installer.
- **GTA San Andreas Stories 1987** is the property of its respective authors (see mod credits above).
- **Pricedown Bl** font is free for commercial use under the Typodermic Fonts license (bundled in `fonts/`).
- All other trademarks belong to their respective owners.

The installer does NOT distribute the game itself, the mod, or any copyrighted assets — it only downloads them from the official sources listed above at install time. The shared cache (`cache/` folder next to the installer) stores downloaded files locally for re-use; delete this folder to fully uninstall.

If you are a copyright holder and believe any part of this installer infringes your rights, please open an issue and we will address it promptly.
