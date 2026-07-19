# Noggit — TO DO

Track fork work (Midnight / modern editor) and legacy items below.

---

## Restore fork features (after upstream reset)

**Context:** `git reset --hard upstream/main` removed local commits; ~95 untracked source files were deleted during cleanup. `origin/main` (`931b95a5`) has most integration wired in tracked files, but the repo does not build until missing sources exist again. Many files were recovered from Cursor agent transcripts; the rest must be recreated or copied from backup.

### Missing — recreate or recover from backup

These were never committed to git and were not fully captured in agent transcript `Write` logs:

- [ ] `src/noggit/tools/PointLightTool.hpp` / `PointLightTool.cpp`
- [ ] `src/noggit/ui/tools/PointLightEditor/PointLightEditor.hpp` / `PointLightEditor.cpp`
- [ ] `src/noggit/tools/TerrainUnifiedTool.hpp` / `TerrainUnifiedTool.cpp`
- [ ] `src/noggit/ui/TerrainUnifiedToolWidget.hpp` (widget `.cpp` recovered from transcripts — verify header + tool glue)
- [ ] `src/noggit/BrushFalloffCurve.hpp` / `BrushFalloffCurve.cpp`
- [ ] `src/noggit/integrations/DiscordRichPresence.hpp` / `DiscordRichPresence.cpp`

### Recovered from transcripts — verify compile + behavior

- [ ] `src/noggit/wmo/WmoGroupLoader.*` / `WmoRootLoader.*`
- [ ] `src/noggit/adt/AdtCommon.*`, `src/noggit/format/ChunkReader.*`, `src/noggit/m2/M2Loader.*`, `src/noggit/wdt_common.hpp`
- [ ] `src/noggit/ModernLight*`, `map_light_target.hpp`, `SunOccluderInstance.hpp`, `VolumetricFog.*`, `World_point_lights.cpp`
- [ ] `src/noggit/rendering/PointLightFlicker.*`, `RealtimeGpuShadowMap.*`, `RealtimeTerrainShadowMask.*`, `RealtimeSunDirection.hpp`
- [ ] `src/noggit/tools/SoundEmitterTool.*`, `src/noggit/ui/tools/SoundEmitterEditor/*`
- [ ] `src/noggit/audio/SoundEmitterAudioManager.*`, `SoundFileLoader.*`
- [ ] `src/noggit/integrations/EpsilonPatchExporter.*`
- [ ] `src/noggit/project/VertexColorPalettePersistence.*`, `WowExportListfileDownload.*`
- [ ] `src/noggit/ui/BrushCursorTool.*`, `RampCreationTool.*`
- [ ] `src/noggit/ui/tools/ChunkManipulator/ChunkCopyOptionsWidget.*`, `ChunkGroupsWidget.*`, `ChunkTerrainTransform.hpp`
- [ ] Extra glsl under `src/noggit/rendering/glsl/` (sun shadow, sea level clip, mccv viz, texture layer billboard, sound emitter billboard, etc.) — must match `resources/resources.qrc`

### Point / spot lights — re-apply fixes (lost with file deletion)

- [ ] Load `_lgt.wdt` via `MPHD.lgtFileDataID` when path lookup fails (retail Midnight maps)
- [ ] MPL/MSLT positions: ADT-local → world on load; world → ADT-local on save
- [ ] Auto-enable `_draw_point_lights` + `_draw_point_light_spheres` when entering point light tool (mirror sound emitters)
- [ ] Confirm View toolbar toggles and editor list populate after map reload

### Build / hygiene

- [ ] Full Release build on `origin/main` + restored sources
- [ ] **Commit all feature sources to git** (stop relying on untracked files ignored by `/**` in `.gitignore`)
- [ ] Smoke test: map 3014 — WMOs, terrain, point lights, sound emitters, realtime shadows

---

## Legacy (SDL 1.4 changelog)

### Done

- Add app Icon
- You can now load BGs
- Rework texture pallet
- Add load all tileset function to texture pallet
- Change rows and cols direction and count
- Add big preview of selected texture
- Discard the old current texture window
- Fix texture pallet size to fit names
- Do Rel with debug now - test do this realy work. !!!!!!
- New keys and mouse functions
- ALT + 1 till 5 set now the texture paint opercety to 100,75,50,25 and 0 %
- Space + Mouse = Speed and Pressure
- During load noggit test the config file and report common problems into the logfile. So perhaps users can fix this problems alone in the future.
- Link to Manual in modcraft wiki
- Load MPQs now in read only. This prevent noggit fron not running if folder rights or read only dont fit on wow folder.
- Better wmo culling that the moldes dont hide all the time.
- Add ground flatten/blur speed.
- UID Fix. On save of an adt all UIDs get recalced now and saged in this and all surrounding adts.
- Clean out test and deprecated menu functions
- Auto size,tile and rotation works now also if you have copy model size and rotation activated.
- In Holes mode you can now alos edit the full chunk if you hold down the ALT key during edit.
- Water save
- Water fix for custom added water
- better Vertics Rendering ( Hanfer )
- After you have saved the selection works not. Fixed.

### ToDo

- Maptile display on minimap << Works not in the manu now because it was jsut cleard out and not fixed.
- Water Functions Assist menu actions and basic edit UI ( Steff )
- Add the ALT Key to texture delete from chunk function. You have to hold all 3 keys. This often cause problems of unwanted deleting in past

### Bugs

- Hole lines. Ground editing msut set the same values for vertics an ADT borders on both adts. ELse you get a smle line on the boarder where oyu cna look trough the ground.
- Resize bug. After the load of a map and the resice of the window the selection do not work anymore.
- Some WMOs crash noggit if renderering is turned on. Perhaps some null pointer. Like in Mulgot near start area.
- If you make a model copy to clipboard, deletethe source model and then paste it, noggit crash. On delete check clipboard and free it before.
- Some models render not or wrong. Perhaps 2 side force or normals. Some mushrooms for example in front of the cave in whispering woods.
- Fish model render wrong. Spikes all over the screen.
- Bigger models (Like stormwind) should mark and load all adts they are located on after insert, move or delete.
- There is no unloading of ADTs. We should think abut and implement
- WMO culling dont work 100%. On big models you have often parts that disapeare.
- Test again that the test that an model is on an ADT works. Some people said that some models dont work. Boundingbox test.
- During the edit (move , insert, delete) of big models (wmos) test if they mark all adts as TO SAVE. Example delete stormwind It will be there after next reload.
- Alpha layer destroy during ground editing. If you edit ground the alpha layer sames to get corrupted. You get artifacts and lines on chunk borders.

### Discuss

- U mode usage or rework to fit more. Perhaps add here also basic alpha map editing and view.

### Future

- Auto terrain painter/generator
- DBC Save function
- If DBC save Light/skybox editor. Icon is already in. Shold show then light centers with a model and perhaps sice as circle on ground if possible.
- Add texture set function. You must select 4 textures and the full adt get cleard with this. Should replace clear texture assist
- Only one selection for all. In the moment holes and all other use different selection types. THe old selection also produce holse somewhere on the map
