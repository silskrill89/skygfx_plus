# Compress Recovery — DCP (opencode-dynamic-context-pruning)

Crash/recovery procedure when the `compress` tool fails or OpenCode terminates during
compression. Root cause (m0052/m0054): DCP fetched the **entire session** on every
compress/context hook. The session store (`%USERPROFILE%\.local\share\opencode\opencode.db`)
grew past 16 GB; single-session payloads reached 283 MB and killed the process ("terminated").

The installed plugin is patched (`DCP-PATCH m0052/m0054 paginate+cap v1`, 4 sites) to
paginate and hard-cap (3000 messages / 40 MB per fetch). `autoUpdate:false` in
`~/.config/opencode/dcp.jsonc` keeps the patch from being overwritten.

## Quick health check

```bash
python tools/dcp_compress_probe.py                # all checks incl. live server probe
python tools/dcp_compress_probe.py --skip-server  # offline checks only
```

All checks must print `PASS` before compress is safe to use.

## What to KEEP vs DELETE

| Keep (never delete) | Purpose |
|---|---|
| `~/.config/opencode/` (`opencode.jsonc`, `dcp.jsonc`) | config, plugin list, `autoUpdate:false` |
| `~/.local/share/opencode/opencode.db` | all sessions/messages (16 GB — this is your data) |
| `.serena/`, `.opencode/memory-bank/`, `.sadie/` | project memories/knowledge |
| `.serena/agent-reports/sessions/` | session text dumps (backups) |

| Delete only when... | How |
|---|---|
| db won't open / probe reports corruption | close OpenCode, delete `opencode.db-wal` + `opencode.db-shm`, retest |
| patch missing after reinstall | re-apply patch (below), or restore `dist/index.js.orig` |
| plugin dir corrupt | `rm -rf` plugin dir + `npm install -g @tarquinen/opencode-dcp` + re-patch |

## Restart-from-scratch procedure

1. **Close OpenCode completely** (all windows/processes). Do not press compress.
2. Dump sessions first if possible: `python tools/session_dump.py --latest 50`
3. Patched plugin lost? Reinstall + re-patch:
   ```bash
   npm install -g @tarquinen/opencode-dcp
   # then restore the patched file from backup if you have it:
   cp ".../opencode-dcp/dist/index.js.orig" ".../opencode-dcp/dist/index.js"  # only if .orig is the PATCHED copy
   ```
   The pristine original is saved as `dist/index.js.orig` **only on the machine where this
   fix was applied**. After a fresh npm install you must re-apply the patch (ask the agent
   with directive m0052/m0054, marker `DCP-PATCH m0052/m0054 paginate+cap v1`).
4. Confirm `dcp.jsonc` contains `"autoUpdate": false`.
5. Start OpenCode. Wait for it to fully load.
6. `python tools/dcp_compress_probe.py` — every check must PASS.
7. Compress is safe to use again only after a full PASS verdict.

## When compress is safe

- Probe verdict = `PASS` on all four checks, **and**
- Session store under ~2 GB, or you accept that compress will truncate (keep newest 3000
  messages; older ranges become unavailable — the patch logs
  `[dcp-patch] ... truncated ...` to the OpenCode log when this happens).

## Notes

- DCP 3.2.0 contains **no server-port discovery code**; it runs in-process inside OpenCode.
  The probe's server check targets the OpenCode server itself (env/port-file → netstat →
  bounded scan 4096-4200 with health paths `/config`, `/session`, `/`).
- Never run `npm update -g` / `npm install -g` for this package without re-checking the
  patch afterwards: `python tools/dcp_compress_probe.py`.
