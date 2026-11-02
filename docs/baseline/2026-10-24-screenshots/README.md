# Regression baseline — 2026-10-24 21:04–21:07 (24 screenshots)

**Baseline ID:** `2026-10-24-screenshots`
**Run date:** 2026-10-24
**Capture window:** 21:04:18–21:07:14 (local file mtimes)
**Images:** 24 (22 PNG, 2 JPEG), 1920×1080
**Companion catalogue:** [`docs/plans/2026-10-24-screenshot-catalogue-2104.md`](../../plans/2026-10-24-screenshot-catalogue-2104.md)

## What this baseline is

This folder is the permanent regression baseline for the 24-shot test run of 2026-10-24. It records what the tested binary actually rendered, image by image, and turns the visible failures into a stable issue registry (`BASE-01` … `BASE-16`) that can be checked off as fixes land. It is evidence, not a diagnosis: where a root cause is known it points at the diagnosis; where it is not, the issue stays `OPEN` unless a working-tree fix is already pending a build.

The images themselves stay in ShareX:

`C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10\`

This folder does not copy them. Filenames are the key.

## Build under test

Exact binary identity (from `skygfx_dbg.log:20` and the water-veil diagnosis):

```text
[BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi
        size=2317824 mtime=2026-10-24 21:03:40 built=Oct 24 2026 21:02:54
```

| Field | Value |
|---|---|
| Deployed ASI | `E:\games\gtasa_skygfx_plus\skygfx.asi` |
| Size | **2,317,824 bytes** |
| Compile stamp | **2026-10-24 21:02:54** |
| Deploy mtime | **2026-10-24 21:03:40** |
| Run/log tag | **`corona-flush-shield`** |
| Shaders | `compiled OK (ps=57 vs=17), FAILED (ps=0 vs=0)` |
| Log | `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, 21:03:59.965–21:07:13 |

Do not silently substitute a later binary. If a run is not this size/stamp, it is a different baseline.

### Confirmed live settings (from the run log / water-veil diagnosis)

| Key | Value |
|---|---|
| `waterStyle` | `Xbox` (`waterStyle=0`) |
| `waterQuality` | `0` (tier 0) |
| `waterParallaxEnable` | `1` |
| `waterBodyClassify` | `1` |
| `waterFresnelSplit` | `1` |
| `stateBaselineEnable` | `1` |
| `ssaoEnable` | `0` (SSAO bailed every frame) |

Other settings were not verified from this run; do not assume them.

## What is in this folder

| File | Purpose |
|---|---|
| [images.md](images.md) | Chronological table of all 24 shots (index, filename, timestamp, clock, scene, CJ, vehicle, water, issue IDs) plus a short paragraph per image. |
| [issues.md](issues.md) | Stable issue registry `BASE-01` … `BASE-16` with severity, frequency, evidence, suspected subsystem, status, and a one-line verify instruction. |
| [next-run-checklist.md](next-run-checklist.md) | What to do first on the next build: INI bisect levers, which issues to look for, which log tags to capture. |

**Index convention.** The `#` index is the catalogue index from `docs/plans/2026-10-24-screenshot-catalogue-2104.md`: **#1 is the last capture (13:06), #24 is the first (10:10)**. Rows in [images.md](images.md) are sorted chronologically (oldest first), so the index column runs 24 → 01. The issue registry uses the same catalogue indices, which is why e.g. the debug banner is `#24`.

## How to use this baseline

1. **Open [images.md](images.md)** and find the shot by index, filename, or in-game clock. The paragraph under the table describes what is wrong without opening the PNG.
2. **Pick an issue in [issues.md](issues.md)**. Start with the highest severity / highest frequency that is still `OPEN` or `FIX-PENDING-BUILD`.
3. **Run the issue's "Verify by" instruction on the current build.** Capture the same kind of shot (same location and time of day if possible) and the named log tag.
4. **Record the result** in the verification log at the bottom of [issues.md](issues.md): date, build size/stamp, issue ID, `PASS`/`FAIL`, evidence filename, tester. Then update the issue status:
   - `OPEN` — no fix in the tested build.
   - `FIX-PENDING-BUILD` — fix is in the working tree but not in a tested binary.
   - `FIXED-UNVERIFIED` — a build contains the fix but the visual check has not passed.
   - `VERIFIED-FIXED` — the verify instruction passes on a named build.

Never change an issue to `VERIFIED-FIXED` from a code read alone; the point of the baseline is the image.

## How to refresh the baseline

One folder per run date, same three core files:

1. Create `docs/baseline/YYYY-MM-DD-screenshots/` (use the capture date, not the build date).
2. Copy the structure: `README.md`, `images.md`, `issues.md`. Refresh `next-run-checklist.md` separately if the test plan changed.
3. Run the 24-shot route again, in the same order and at the same in-game clocks if possible, so rows are comparable. Capture the `[BUILD]` log line first.
4. **Open every image before writing the table.** Do not copy the previous run's descriptions; re-verify each shot. Note any new artifact class as a new `BASE-xx` ID, and keep old IDs stable for issues that persist.
5. Record the new build identity exactly (size, compile stamp, deploy mtime, run/log tag) and the capture window.
6. Leave the old folder immutable. If a later run contradicts an old row, add a note in the new folder rather than editing history.

## Sources

- Screenshot catalogue: [2026-10-24-screenshot-catalogue-2104.md](../../plans/2026-10-24-screenshot-catalogue-2104.md)
- Water veil diagnosis: [2026-10-24-water-veil-diagnosis.md](../../plans/2026-10-24-water-veil-diagnosis.md)
- Corona black-quad diagnosis: [2026-10-24-corona-blackquad-diagnosis.md](../../plans/2026-10-24-corona-blackquad-diagnosis.md)
- Baseline images: [images.md](images.md) · issue registry: [issues.md](issues.md) · next-run plan: [next-run-checklist.md](next-run-checklist.md)

## Caveats

- These are 24 hand-taken shots, not a systematic sweep. Absence of an artifact in this set is not proof it is fixed; presence is evidence it was live in this build.
- Source line numbers cited in [issues.md](issues.md) drift. They were valid for the working tree at the time of the cited diagnosis (2026-10-24); re-check before patching.
- The two JPEGs (`#14`, `#16`) have compression artifacts; do not treat JPEG ringing as a renderer bug.
- `#24` is an interior shot; its dark minimap and the yellow triangle marker are normal for that context and are not counted as issues.
