// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/map_lights/LightInfoCatalog.hpp>
#include <noggit/Log.h>

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QString>

#include <algorithm>
#include <cmath>
#include <random>
#include <system_error>

namespace
{
  [[nodiscard]] std::filesystem::path resolve_light_info_json (std::filesystem::path const& user_path)
  {
    if (user_path.empty())
      return {};

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base;

    if (fs::is_regular_file (user_path, ec) && !ec)
      base = user_path.parent_path();
    else if (fs::is_directory (user_path, ec) && !ec)
      base = user_path;
    else
      base = user_path.parent_path();

    if (base.empty())
      return {};

    fs::path const direct = base / "meta" / "LightInfo.json";
    if (fs::is_regular_file (direct, ec) && !ec)
      return direct;

    fs::path const parent_try = base.parent_path() / "meta" / "LightInfo.json";
    if (fs::is_regular_file (parent_try, ec) && !ec)
      return parent_try;

    return {};
  }
}

namespace Noggit::MapLights
{
  LightInfoCatalog& LightInfoCatalog::instance()
  {
    static LightInfoCatalog s;
    return s;
  }

  void LightInfoCatalog::load_fallback_presets()
  {
    _presets.clear();
    _presets.push_back ({ "flicker_normal", 2, 25.f, 15.f });
    _presets.push_back ({ "flicker_campfire", 2, 25.f, 150.f });
    _presets.push_back ({ "sine", 1, 25.f, 15.f });
    _presets.push_back ({ "harsh", 3, 25.f, 15.f });
    _using_fallback = true;
    _resolved_json_path = std::nullopt;
  }

  bool LightInfoCatalog::try_load_json (std::filesystem::path const& json_path)
  {
    QFile file (QString::fromStdString (json_path.string()));
    if (!file.open (QIODevice::ReadOnly))
    {
      LogError << "LightInfoCatalog: cannot read " << json_path.string() << std::endl;
      return false;
    }

    QJsonParseError err {};
    QJsonDocument const doc = QJsonDocument::fromJson (file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
      LogError << "LightInfoCatalog: invalid JSON in " << json_path.string() << ": "
               << err.errorString().toStdString() << std::endl;
      return false;
    }

    QJsonObject const root = doc.object();
    QJsonObject const anims = root.value ("LightAnims").toObject();
    if (anims.isEmpty())
    {
      LogError << "LightInfoCatalog: no LightAnims in " << json_path.string() << std::endl;
      return false;
    }

    std::vector<LightAnimPreset> loaded;
    loaded.reserve (static_cast<std::size_t>(anims.size()));

    for (auto it = anims.begin(); it != anims.end(); ++it)
    {
      if (!it.value().isObject())
        continue;

      QJsonObject const o = it.value().toObject();
      LightAnimPreset preset {};
      preset.id = it.key().toStdString();
      preset.flicker_mode = static_cast<std::uint8_t>(
        std::clamp (o.value ("FlickerMode").toInt (0), 0, 3));
      preset.flicker_intensity = static_cast<float>(o.value ("FlickerIntensity").toDouble (25.0));
      preset.flicker_speed = static_cast<float>(o.value ("FlickerSpeed").toDouble (15.0));
      loaded.push_back (preset);
    }

    if (loaded.empty())
      return false;

    _presets = std::move (loaded);
    _using_fallback = false;
    _resolved_json_path = json_path;
    Log << "LightInfoCatalog: loaded " << _presets.size() << " animation preset(s) from "
        << json_path.string() << std::endl;
    return true;
  }

  void LightInfoCatalog::reload_from_settings()
  {
    QSettings settings;
    QString const path_q = settings.value ("integrations/mapupconverter_path").toString().trimmed();
    std::filesystem::path const user_path = path_q.isEmpty()
                                              ? std::filesystem::path {}
                                              : std::filesystem::path (path_q.toStdWString());

    if (user_path == _cached_settings_path && _resolved_json_path)
    {
      std::error_code ec;
      auto const mtime = std::filesystem::last_write_time (*_resolved_json_path, ec);
      if (!ec && mtime == _cached_mtime)
        return;
    }

    _cached_settings_path = user_path;

    if (user_path.empty())
    {
      load_fallback_presets();
      return;
    }

    std::filesystem::path const json_path = resolve_light_info_json (user_path);
    if (json_path.empty())
    {
      Log << "LightInfoCatalog: meta/LightInfo.json not found near " << user_path.string()
          << "; using built-in presets." << std::endl;
      load_fallback_presets();
      return;
    }

    std::error_code ec;
    _cached_mtime = std::filesystem::last_write_time (json_path, ec);

    if (!try_load_json (json_path))
      load_fallback_presets();
  }

  std::vector<LightAnimPreset> const& LightInfoCatalog::presets() const
  {
    return _presets;
  }

  bool LightInfoCatalog::using_fallback_presets() const
  {
    return _using_fallback;
  }

  std::optional<std::filesystem::path> LightInfoCatalog::resolved_json_path() const
  {
    return _resolved_json_path;
  }

  std::optional<std::size_t> LightInfoCatalog::match_preset (World::PointLight const& light) const
  {
    if (light.flicker_mode == 0)
      return std::nullopt;

    auto near_eq = [] (float a, float b) { return std::abs (a - b) < 0.05f; };

    for (std::size_t i = 0; i < _presets.size(); ++i)
    {
      LightAnimPreset const& p = _presets[i];
      if (light.flicker_mode == p.flicker_mode
          && near_eq (light.flicker_intensity, p.flicker_intensity)
          && near_eq (light.flicker_speed, p.flicker_speed))
        return i;
    }

    return std::nullopt;
  }

  void LightInfoCatalog::apply_preset (World::PointLight& light, std::size_t preset_index) const
  {
    if (preset_index >= _presets.size())
      return;

    LightAnimPreset const& p = _presets[preset_index];
    static thread_local std::mt19937 rng { std::random_device{}() };
    light.flicker_seed = rng();

    light.flicker_mode = p.flicker_mode;
    light.flicker_intensity = p.flicker_intensity;
    light.flicker_speed = p.flicker_speed;
  }

  QString LightInfoCatalog::preset_display_label (QString const& preset_id)
  {
    QString label = preset_id;
    label.replace (QLatin1Char ('_'), QLatin1Char (' '));
    return label;
  }
}
