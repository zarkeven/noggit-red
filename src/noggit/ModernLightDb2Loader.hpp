// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#pragma once

#include <noggit/ModernLightData.hpp>

struct ZoneLight;
struct ZoneLightPoint;

class ModernLightTables;

namespace Noggit::ModernLightDb2
{
  //! Try loading modern light tables from client DB2 using WoWDBDefs layouts for
  //! MapLightTarget::client_build_string (SL 9.2.7 / WDC3). Returns true when at
  //! least Light + LightParams + LightData were parsed.
  bool try_load_into(ModernLightTables& tables);
}
