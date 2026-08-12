# SL 9.2.7 client-data validation (wowlib oracle)

Noggit targets **Shadowlands 9.2.7.45745** for modern (non-WotLK) projects.
Layouts are aligned with [skarndev/wowlib](https://github.com/skarndev/wowlib)
`versions::shadowlands` and DBDs from [wowdev/WoWDBDefs](https://github.com/wowdev/WoWDBDefs).

wowlib is **not** linked into Noggit (C++26). Use it as an out-of-tree round-trip oracle.

## Sync DBDs

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\sync_wowdbdefs.ps1
```

## wowlib setup

```powershell
# Clone beside the noggit repo (or anywhere)
git clone https://github.com/skarndev/wowlib.git C:\Users\donal\Desktop\wowlib

# Point integration tests at a 9.2.x client folder that contains .build.info / Data
# Example archive path used during development:
#   D:\WoWBuildArchive\9.x\9.2.7.45338
$env:WOWLIB_TEST_CLIENTS_DIR = "D:\WoWBuildArchive\9.x"
$env:WOWLIB_TEST_LISTFILE = "C:\Users\donal\Desktop\noggit-red-upstream\dist\listfile\listfile.csv"

# Build/test per wowlib README (requires gcc ≥ 16):
#   cmake --preset gcc16-debug
#   cmake --build --preset gcc16-debug
#   ctest --preset gcc16-debug
```

Layouts verified byte-perfect in wowlib for SL include: **WDT**, **`_lgt.wdt`**, **WDL**, **BLP**, **WDC3 DB2**.
ADT/M2 are semantic round-trips. **MH2O liquids** follow wowlib (`liquid.hpp` / `chunks/liquid.hpp`): UVMapEntry is `u16 / 8` in shaders; LiquidObject LVF resolve uses exact bytes-per-vertex `1/5/8/9` only.

Agents and contributors: always consult the local wowlib clone before changing format readers/writers (see `.cursor/rules/wowlib-format-oracle.mdc`).

## Priority corpus checklist

Against the same 9.2.7 client, confirm in Noggit (SL project) and optionally via wowlib:

1. `Map.db2` / map list loads (DBD pin `9.2.7.45745`)
2. `Light.db2`, `LightParams.db2`, `LightData.db2` (WDC3) → modern lights
3. One map with `_lgt.wdt` containing **MPL3** (+ optional **MSLT** / **MLTA**)
4. One split ADT tile (`_tex0` / `_obj0`)
5. One WMO (MVER 17) and one MD21 M2 (version 274)

## Noggit intentional deviations

- **NGPL** on ADT roots: Noggit-only point-light cap chunk; not a client/wowlib format.
- **`{map}_lights.json`**: project-side manifest used when writing `_lgt.wdt`.

## Reader note

`blizzard-database-library` WDC3 record reader now populates `Columns[name].Values` for
int/float array fields from DBDs (`GameCoords[3]`, `LightParamsID[8]`, fog coefficient arrays).
Without that, DBD-driven Light/LightData loading cannot read SL 9.2.7 layouts.
