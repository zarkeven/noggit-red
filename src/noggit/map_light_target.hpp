// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <cstdint>

namespace Noggit::MapLightTarget
{
  //! Retail client series Noggit Yellow targets for `_lgt.wdt` and related map light I/O.
  //! Matches wowlib `versions::shadowlands` / WoWDBDefs pin 9.2.7.45745.
  inline constexpr int client_version_major = 9;
  inline constexpr int client_version_minor = 2;
  inline constexpr int client_version_patch = 7;
  inline constexpr int client_version_build = 45745;

  //! Full build string for BlizzardDatabase / DBD selection.
  inline constexpr char const* client_build_string = "9.2.7.45745";

  //! Version field in the `_lgt.wdt` **MVER** chunk on save (Shadowlands-era sniffs).
  inline constexpr std::int32_t _lgt_wdt_mver = 18;

  /**
   * Point lights are written as **MPL3** (retail >= 9.0.1). Older maps may only have **MPL2**;
   * loading still accepts **MPL2** when **MPL3** is absent.
   */
  inline constexpr bool save_point_lights_as_mpl3 = true;
}
