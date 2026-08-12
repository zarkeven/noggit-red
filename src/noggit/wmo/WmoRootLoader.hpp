// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

class WMO;

namespace BlizzardArchive
{
  class ClientFile;
}

namespace Noggit::Wmo
{
  void load_root(WMO& wmo, BlizzardArchive::ClientFile& file);
}
