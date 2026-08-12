// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

class MapChunk;

namespace Noggit::Ui::Tools::ChunkManipulator
{
  /// Rotate chunk-local height grid, MCCV, and shadow map 90° clockwise (XZ in world space).
  void rotateChunkTerrain90CW(MapChunk* chunk);

  /// Mirror chunk terrain across the chunk X axis (flip Z within the chunk).
  void flipChunkTerrainAlongZ(MapChunk* chunk);
}
