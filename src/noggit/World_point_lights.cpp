// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/World.h>
#include <noggit/MapTile.h>
#include <noggit/map_index.hpp>
#include <noggit/MapHeaders.h>
#include <noggit/ModernLightTables.hpp>

#include <math/coordinates.hpp>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <utility>

void World::worldPosToAdtMcnk(glm::vec3 const& pos, std::uint16_t& out_adt_x, std::uint16_t& out_adt_z, int& out_mcnk_x, int& out_mcnk_z)
{
  out_adt_x = static_cast<std::uint16_t>(std::clamp(static_cast<int>(std::floor(pos.x / TILESIZE)), 0, 63));
  out_adt_z = static_cast<std::uint16_t>(std::clamp(static_cast<int>(std::floor(pos.z / TILESIZE)), 0, 63));
  float const ox = pos.x - static_cast<float>(out_adt_x) * TILESIZE;
  float const oz = pos.z - static_cast<float>(out_adt_z) * TILESIZE;
  out_mcnk_x = std::clamp(static_cast<int>(ox / CHUNKSIZE), 0, 15);
  out_mcnk_z = std::clamp(static_cast<int>(oz / CHUNKSIZE), 0, 15);
}

glm::vec3 World::pointLightDiskToWorld(glm::vec3 const& disk, std::uint16_t tile_x, std::uint16_t tile_y)
{
  // Retail (SL+) MPL2/MPL3/MSLT store absolute server/GPS (Z-up), same as MCSE / VFOG.
  // Verified on map 2222 _lgt: 12761/12761 MPL3 and 195/195 MSLT match to_client ↔ tile_x/tile_y.
  // Always use that path when modern features are on — the ADT-local fallback below will
  // mis-place lights if tile fields are garbage or from an older Noggit save.
  if (noggit_modern_features_enabled())
    return math::to_client(disk.x, disk.y, disk.z);

  // Legacy / mixed project files may use absolute client or ADT-local client coords.
  auto tile_of = [](glm::vec3 const& p) -> std::pair<int, int>
  {
    return { static_cast<int>(std::floor(p.x / TILESIZE))
           , static_cast<int>(std::floor(p.z / TILESIZE)) };
  };

  glm::vec3 const as_server = math::to_client(disk.x, disk.y, disk.z);
  auto const [sx, sz] = tile_of(as_server);
  if (sx == static_cast<int>(tile_x) && sz == static_cast<int>(tile_y))
    return as_server;

  auto const [cx, cz] = tile_of(disk);
  if (cx == static_cast<int>(tile_x) && cz == static_cast<int>(tile_y))
    return disk; // already absolute client/Y-up

  // ADT-local client (tile is ownership origin).
  return disk + glm::vec3(static_cast<float>(tile_x), 0.f, static_cast<float>(tile_y)) * TILESIZE;
}

glm::vec3 World::pointLightWorldToDisk(glm::vec3 const& world, std::uint16_t /*tile_x*/, std::uint16_t /*tile_y*/)
{
  // Always write absolute server/GPS so retail clients and MapUpconverter agree.
  return math::to_server(world.x, world.y, world.z);
}

void World::syncPointLightTileFromPosition(PointLight& light)
{
  light.tile_x = static_cast<std::uint16_t>(std::clamp(static_cast<int>(std::floor(light.position.x / TILESIZE)), 0, 63));
  light.tile_y = static_cast<std::uint16_t>(std::clamp(static_cast<int>(std::floor(light.position.z / TILESIZE)), 0, 63));
}

void World::ensureSpotLightDefaults(PointLight& light)
{
  if (light.light_type != MapLightType::Spot)
  {
    return;
  }

  float constexpr k_default_inner = 0.5235987755982989f;  // ~30 deg
  float constexpr k_default_outer = 0.7853981633974483f;  // ~45 deg
  float constexpr k_min_cone_delta = 0.05f;

  if (light.inner_angle <= 0.f || !std::isfinite(light.inner_angle))
  {
    light.inner_angle = k_default_inner;
  }
  if (light.outer_angle <= 0.f || !std::isfinite(light.outer_angle))
  {
    light.outer_angle = k_default_outer;
  }
  if (light.inner_angle > light.outer_angle)
  {
    std::swap(light.inner_angle, light.outer_angle);
  }
  if (light.outer_angle - light.inner_angle < k_min_cone_delta)
  {
    light.outer_angle = light.inner_angle + k_min_cone_delta;
  }

  if (light.spotlight_radius <= 0.f || !std::isfinite(light.spotlight_radius))
  {
    light.spotlight_radius = std::max(15.f, light.attenuation_end);
  }

  // Point lights ignore rotation; a freshly converted spot with zero euler aims along -Z and misses the scene.
  if (glm::length(light.rotation_radians) < 1e-4f)
  {
    light.rotation_radians = glm::vec3(-glm::half_pi<float>(), 0.f, 0.f);
  }
}

std::size_t World::pointLightsInAdtCount(std::uint16_t adt_x, std::uint16_t adt_z, std::optional<std::size_t> exclude_index) const
{
  std::size_t n = 0;
  for (std::size_t i = 0; i < _point_lights.size(); ++i)
  {
    if (exclude_index && i == *exclude_index)
      continue;

    auto const& L = _point_lights[i];
    if (L.tile_x == adt_x && L.tile_y == adt_z)
      ++n;
  }
  return n;
}

std::uint32_t World::effectivePointLightCapForAdt(std::uint16_t adt_x, std::uint16_t adt_z) const
{
  MapTile* const tile = mapIndex.getTile(TileIndex(adt_x, adt_z));
  if (!tile)
    return 104u;

  return tile->effectiveAdtPointLightCap();
}

std::string World::pointLightMpl2SaveOverflowReport() const
{
  std::set<std::pair<std::uint16_t, std::uint16_t>> tiles;
  for (auto const& L : _point_lights)
    tiles.emplace(L.tile_x, L.tile_y);

  std::ostringstream lines;
  for (auto const& t : tiles)
  {
    std::size_t const n = pointLightsInAdtCount(t.first, t.second);
    std::uint32_t const cap = effectivePointLightCapForAdt(t.first, t.second);
    if (n > cap)
      lines << "\n  ADT " << static_cast<int>(t.first) << "_" << static_cast<int>(t.second)
            << ": " << n << " lights, NGPL cap " << cap;
  }

  std::string const detail = lines.str();
  if (detail.empty())
    return {};

  return std::string("Some ADTs have more point lights than their per-ADT NGPL cap; a client may not use them all."
                     " Noggit still wrote every light to _lgt.wdt (MPL2).")
         + detail;
}
