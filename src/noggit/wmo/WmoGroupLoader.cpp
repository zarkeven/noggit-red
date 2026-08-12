// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/wmo/WmoGroupLoader.hpp>
#include <noggit/WMO.h>

namespace Noggit::Wmo
{
  bool load_group(WMOGroup& group)
  {
    (void)group;
    return false;
  }

  void ensure_split_child_groups_loaded(WMO& wmo)
  {
    (void)wmo;
  }
}
