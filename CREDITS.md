# Credits & Attribution

## GTA San Andreas Stories 1987 — Total Conversion Mod

This installer exists ONLY to make it easier to install the **GTA San Andreas Stories 1987** total-conversion mod. All credit for the mod itself belongs to its development team. The installer is a fan-made tool, unaffiliated with Rockstar Games or Take-Two Interactive.

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

---

## Supporting Mods (downloaded/linked by this installer)

The GTA SAS 1987 mod already bundles all required dependencies (per the official site's note: *"GTA San Andreas Stories already includes all required dependencies. The storyline is built on DYOM 8.1, which is already included and fully configured inside the archive."*). The installer offers these as **optional** extras for users who want to update them independently.

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
- **Credits:** GameCopyWorld team for hosting community game patches for over two decades.
- **Note:** The NO-CD patch is for use with your own legally-purchased game only. Please respect Rockstar's EULA and your local laws.

### LibertyCity.net — Mod mirror
- **Source:** https://libertycity.net/
- **Used for:** Fallback search when the main mod isn't found on the official gtasas.netlify.app site.
- **Credits:** LibertyCity.net team for hosting the largest GTA mod archive.

### MixMods.com.br — Brazilian GTA modding community
- **Source:** https://www.mixmods.com.br/
- **Used for:** Original article pages for CLEO+ and NewOpcodes (the installer prefers the GitHub mirror for CLEO+ because MixMods is behind Cloudflare bot protection).
- **Credits:** JuniorDjjr and the MixMods team for the largest Portuguese-language GTA modding resource.

### GitHub — Mod source hosting
- **Used for:** CLEO 5 releases (cleolibrary/CLEO5), CLEO+ releases (JuniorDjjr/CLEOPlus), and the official GTA SAS 1987 website source (babamohammed2022/gta-1987-remastered-mod — where the splash images come from).
- **Credits:** GitHub Inc. for hosting the world's open-source code.

### MediaFire — Main mod file hosting
- **Used for:** The official GTA SAS 1987 main mod archive (`GTA_SAS_june_2026.rar`).
- **Credits:** MediaFire LLC for free file hosting.

### Pricedown Bl font
- **Source:** https://www.dafont.com/pricedown.font
- **Author:** Typodermic Fonts Inc. (Ray Larabie)
- **License:** Free for commercial use (see `fonts/FONT_LICENSE.html` and `fonts/Typodermic_EULA.pdf` for full terms)
- **Used for:** The actual GTA logo typeface, used here for installer display text.

### GTA Vice City Stories carcols.dat palette
- **Reference:** https://gta.fandom.com/wiki/Carcols.dat/GTAVCS
- **Used for:** Color palette inspiration (dark greens preferred for legibility).
- **Credits:** Rockstar Games for the original VCS color table; GTA Fandom wiki for documenting it.

---

## Installer Development Credits

- **Installer design & code:** Built with PyQt5, Python 3.10+, and a lot of caffeine.
- **PyQt5:** Riverbank Computing (https://www.riverbankcomputing.com/software/pyqt/) — GPL/commercial license.
- **Requests:** Kenneth Reitz (https://requests.readthedocs.io/) — Apache 2.0.
- **BeautifulSoup4:** Leonard Richardson (https://www.crummy.com/software/BeautifulSoup/) — MIT.
- **py7zr:** Hiroshi Miura (https://github.com/miurahr/py7zr) — LGPL-3.0.
- **rarfile:** Marko Kreen (https://github.com/markokr/rarfile) — ISC.
- **psutil:** Giampaolo Rodola (https://github.com/giampaolo/psutil) — BSD 3-Clause.
- **PyInstaller:** PyInstaller team (https://pyinstaller.org/) — GPL.

---

## Legal Disclaimer

This installer is a **fan-made tool** and is **not affiliated with, endorsed by, or sponsored by** Rockstar Games, Take-Two Interactive, or any of the mod authors listed above.

- **GTA San Andreas** is a trademark of Rockstar Games / Take-Two Interactive. You must own a legal copy of the game to use this installer.
- **GTA San Andreas Stories 1987** is the property of its respective authors (see mod credits above).
- **Pricedown Bl** font is free for commercial use under the Typodermic Fonts license (bundled).
- All other trademarks belong to their respective owners.

The installer does NOT distribute the game itself, the mod, or any copyrighted assets — it only downloads them from the official sources listed above at install time. The shared cache (`cache/` folder next to the installer) stores downloaded files locally for re-use; delete this folder to fully uninstall.

If you are a copyright holder and believe any part of this installer infringes your rights, please open an issue and we will address it promptly.
