// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

class MapTile;
class World;

namespace Noggit::Adt
{
  void save_split_tile(MapTile* tile, World* world, bool save_using_mclq_liquids);
}
