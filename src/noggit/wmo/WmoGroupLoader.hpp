// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

class WMO;
class WMOGroup;

namespace Noggit::Wmo
{
  [[nodiscard]] bool load_group(WMOGroup& group);
  void ensure_split_child_groups_loaded(WMO& wmo);
}
