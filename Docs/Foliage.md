# Foliage authoring

The first foliage tool paints static model instances on a terrain. Foliage records belong to a `TerrainFoliage` component on the selected scene entity and save with the scene. Terrain height/surface assets remain independent. Instances use internal render parts, shared model resources, the normal material instancing path, imported mesh LODs, shadows and entity picking. They do not create an entity per tree.

Open `Assets/Scenes/Tests/FoliagePlayground.lvl` in the game project for a small starter scene. Select **Terrain**, click **Edit Terrain** in its inspector, then choose **Foliage → Paint**. The palette already contains Tree, Bush and Rock. To use your own terrain:

1. Open its `.terrain` source in the Terrain panel and select its scene instance.
2. Choose **Foliage**, then **Starter meshes** or **Add model...**. You can drag a model from the asset browser onto Add model.
3. Select a species and click **Paint**. Drag in World View. **Shift** reverses paint/erase; **Escape** cancels the current stroke. Right mouse navigation releases the brush gesture.
4. Adjust radius, density, minimum spacing, scale range, maximum slope, height offset and normal alignment. Yaw varies deterministically across a stroke. Minimum spacing applies across species.
5. **Erase** removes the selected species; **Erase all species** broadens the brush. Clear selected/all and Reapply are undoable. Use **Reapply** after sculpting to fit positions/normals to the current source.
6. Use **Ctrl+S / Save Scene** for foliage and the terrain asset save controls for height/surface edits. Undo/Redo in Foliage uses scene history, including exact stable instance IDs and undoable brush settings (which do not dirty the scene). Play/Stop preserves the edit-time scene.

Tree, Bush and Rock under `Engine/Assets/Models/Foliage/` are simple starter OBJ meshes authored for this feature; imported static models can replace them by adding species. Imported materials control shading. Skinned, animated and vertex-cache models are rejected with a visible diagnostic.

The first pass supports 16 species and 5,000 records per component, with an additional 20,000 mesh-part draw limit. A model can have up to 64 parts. Draw distance is measured against all rendering cameras and the editor view; inactive/hidden/distant instances release or suppress their commands. This is resident foliage rendering; terrain page streaming does not yet stream foliage records or evict their GPU resources by tile.

Foliage is visual: collision, navigation obstacles, dense grass/wind, masks/biomes and species editing/reseeding are later work. Palette and placement are per scene instance; source Make Unique and terrain cooks do not transfer them. Scene duplication/prefab serialization copies the component records normally. Scene recovery handles them through scene snapshots; terrain asset recovery handles terrain heights/surfaces separately.

`Source/Foliage/FoliageData.*` owns deterministic grid candidates, bounded strokes, per-tile identities, spacing/slope checks and checked JSON serialization. `TerrainFoliage` owns validated scene data, `RenderFoliage.cpp` owns resource/command lifetime, and `TerrainFoliageWidget.cpp` owns scene history and viewport interaction. `FoliageTests.cpp` covers placement, event-rate independence, erase, malformed data and limits; `FoliageFlows.edscript` exercises the editor, renderer cleanup and Play/Stop.

Linux validation (2026-10-20): editor/game Debug builds, 344 editor tests and 341 game tests pass. The five foliage cases pass with 6,877 assertions in each build, including checked integer ranges and transactional malformed-data rejection. Native foliage flows cover brush-setting and stroke history, cancel, clear, distance/visibility cleanup, sculpt/Reapply undo, save/reopen in a fresh process, and Play/Stop. The painted terrain streaming regression and a 120-frame standalone playground capture pass. Win64/macOS builds were not run locally.
