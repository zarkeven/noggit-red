// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/adt/SplitAdtLoader.hpp>
#include <noggit/Log.h>

class MapTile;

namespace Noggit::Adt
{
  void load_split_tile(MapTile* tile)
  {
    (void)tile;
    LogError << "Split ADT loading is not available in this build." << std::endl;
  }
}
