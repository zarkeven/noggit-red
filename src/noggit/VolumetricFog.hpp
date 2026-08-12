// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct VolumetricFogEntry
{
  std::uint32_t id = 0;
  glm::vec3 color{1.f};
  //! On-disk intensity[3]: [0] ≈ 0–1 opacity, [1] mid factor ≈ 0.35–6, [2] large scale ≈ 50–3300 (not a mix multiplier).
  float intensity[3]{1.f, 1.f, 1.f};
  glm::vec3 position{0.f};
  //! Client Y-up ellipsoid radii (from on-disk radius[3] after axis remap).
  float radius[3]{100.f, 100.f, 100.f};
  //! Server-space rotation quaternion (xyzw); applied when non-identity.
  float rotation[4]{0.f, 0.f, 0.f, 1.f};
  std::uint32_t flags = 0;
  std::uint32_t model_file_data_id = 0;
  std::uint32_t fog_level = 0;
  //! VFEX Unk1[0..2] when present (MVER ≥ 2); otherwise zeros.
  float vfex_unk[3]{0.f, 0.f, 0.f};
  bool has_vfex = false;
};

//! Shader-safe opacity from VFOG intensity triplet (uses intensity[0], soft-boosted by [1]).
float volumetric_fog_shader_intensity(float intensity0, float intensity1, float intensity2);

//! Load `mapname_fogs.wdt` (VFOG + optional VFEX). Prefers MPHD fogsFileDataID when non-zero.
std::vector<VolumetricFogEntry> load_volumetric_fogs_from_client(
  std::string const& map_basename
, std::uint32_t fogs_file_data_id = 0);

//! Parse a raw `_fogs.wdt` from disk (for tests / project overrides).
std::vector<VolumetricFogEntry> load_volumetric_fogs_from_path(std::string const& absolute_path);
