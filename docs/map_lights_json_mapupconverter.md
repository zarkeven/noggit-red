# Map lights JSON — MapUpconverter upstream follow-up

Noggit Yellow writes `{map}_lights.json` beside the WotLK map folder:

```
{project}/World/Maps/{map}/{map}_lights.json
```

MapUpconverter's `inputDir` should include this path so a future upstream change can prefer JSON over `noggit_light*` MDDF scanning.

## Suggested MapUpconverter changes

### `LightWDT.GenerateForSL` ([LightWDT.cs](https://github.com/ModernWoWTools/MapUpconverter/blob/main/MapUpconverter/WDT/LightWDT.cs))

1. Before collecting lights from ADT `Obj0` placeholders, check for  
   `{inputDir}/world/maps/{map}/{map}_lights.json` (case-insensitive).
2. If present and `version == 1`:
   - Build MPL3 / MSLT / MTEX / MLTA from JSON fields (`position_disk`, colors, attenuation, MLTA flicker, cookie FileDataIDs, spot cone data).
   - Write `{map}_lgt.wdt` from that data (same chunk order as Noggit: MVER, MPL3, MSLT, MTEX, MLTA).
   - Skip MDDF `noggit_light` model collection for light generation.

### `Obj0.Convert` ([Obj0.cs](https://github.com/ModernWoWTools/MapUpconverter/blob/main/MapUpconverter/ADT/Obj0.cs))

When JSON manifest exists for the map:

- Do not scan MDDF for `noggit_light` models to feed `LightWDT`.
- Still hide any legacy proxy models in `obj0` if they remain in source ADTs.

### NGPL on converted ADTs

Noggit's `MapLightsJsonInjector` (or Epsilon export hook) upserts the `NGPL` chunk on **converted** root ADTs in the patch using `adt_light_caps[]` from JSON. MapUpconverter could alternatively apply the same caps during ADT conversion if it already emits Noggit extension chunks.

## JSON schema v1 (reference)

See `src/noggit/map_lights/MapLightsManifest.hpp` and a saved `{map}_lights.json` from Noggit.

Key fields:

- `position_disk` — absolute server/GPS coords (same as retail `_lgt` MPL3/MSLT `.gps`)
- `adt_light_caps[].ngpl_cap_encoded` — `0` = WoW default 104; `1..255` = explicit per-ADT cap written to `NGPL`

## Noggit injection (today)

Without upstream MapUpconverter support:

1. Save map in Noggit → `{map}_lights.json` + `_lgt.wdt`.
2. Run MapUpconverter manually on the project (ADTs/WDT/WDL).
3. **World → Inject lights to patch** (or Epsilon export on save) reads JSON and writes patch `_lgt.wdt`, patches WDT `MPHD.lgtFileDataID`, and upserts `NGPL` on converted ADTs in the Epsilon patch folder.
