// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/DBC.h>
#include <noggit/Log.h>
#include <noggit/MapHeaders.h>
#include <noggit/ModernLightTables.hpp>
#include <noggit/Model.h> // Model
#include <noggit/ModelManager.h> // ModelManager
#include <noggit/Sky.h>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <opengl/shader.hpp>
#include <glm/glm.hpp>
#include <noggit/project/CurrentProject.hpp>
#include <math/trig.hpp>
#include <math/bounding_box.hpp>

#include <algorithm>
#include <string>
#include <array>
#include <unordered_map>
#include <QFile>
#include <QTextStream>
#include <QStringList>

const float skymul = 36.0f;

namespace skyparams
{
  // Map to store SkyParams globally
  std::unordered_map<unsigned int, SkyParam> skyParamMap;
  
  // get or create SkyParam from the global map
  SkyParam* getOrCreateParam(unsigned int id, Noggit::NoggitRenderContext context) {
  
    if (!id)
      return nullptr;
  
    auto it = skyParamMap.find(id);
    if (it != skyParamMap.end()) {
      return &(it->second);  // Return existing SkyParam
    }
  
    SkyParam newParam = SkyParam(id, context);

    assert(newParam.Id != 0);
    if (newParam.Id == 0)
      return nullptr;
  
    auto [newIt, inserted] = skyParamMap.emplace(id, std::move(newParam));
    return &(newIt->second);  // Return newly created SkyParam
  }
}


SkyColor::SkyColor(int t, int col)
{
  time = t;
  color.z = ((col & 0x0000ff)) / 255.0f;
  color.y = ((col & 0x00ff00) >> 8) / 255.0f;
  color.x = ((col & 0xff0000) >> 16) / 255.0f;
}

SkyFloatParam::SkyFloatParam(int t, float val)
: time(t)
, value(val)
{
}

SkyParam::SkyParam(int paramId, Noggit::NoggitRenderContext context)
: _context(context)
{
  Id = paramId;

  if (Id == 0)
  {
    assert(false);
    return;
  }

  if (noggit_use_modern_sky_lights())
  {
    ModernLightTables::instance().ensure_loaded();
    if (ModernLightTables::instance().has_usable_data())
    {
      ModernLightTables::instance().init_sky_param(paramId, *this, context);
      if (has_modern_light_data())
      {
        return;
      }
    }
  }

  try
  {
    DBCFile::Record light_param = gLightParamsDB.getByID(paramId);
    int skybox_id = light_param.getInt(LightParamsDB::skybox);

    _highlight_sky = light_param.getInt(LightParamsDB::highlightSky);
    _river_shallow_alpha = light_param.getFloat(LightParamsDB::water_shallow_alpha);
    _river_deep_alpha = light_param.getFloat(LightParamsDB::water_deep_alpha);
    _ocean_shallow_alpha = light_param.getFloat(LightParamsDB::ocean_shallow_alpha);
    _ocean_deep_alpha = light_param.getFloat(LightParamsDB::ocean_deep_alpha);
    _glow = light_param.getFloat(LightParamsDB::glow);

    if (skybox_id && !skybox.has_value())
    {
      try
      {
        auto skyboxRec = gLightSkyboxDB.getByID(skybox_id);
        skybox.emplace(skyboxRec.getString(LightSkyboxDB::filename), _context);
        skyboxFlags = skyboxRec.getInt(LightSkyboxDB::flags);
      }
      catch (...)
      {
        LogError << "When trying to get the skybox id " << skybox_id << "for the param " << paramId << " in LightSkybox.dbc." << std::endl;
      }

    }
  }
  catch (...)
  {
    LogError << "When trying to initialize Params the entry " << paramId << " in LightParams.dbc." << std::endl;
    Id = 0;
  }

  // initialize colors (lightIntBand.dbc)
  int light_int_start = (paramId * NUM_SkyColorNames) - (NUM_SkyColorNames - 1);
  for (int i = 0; i < NUM_SkyColorNames; ++i)
  {
    try
    {
      DBCFile::Record rec = gLightIntBandDB.getByID(light_int_start + i);
      int entries = rec.getInt(LightIntBandDB::Entries);

      if (entries == 0)
      {
        // mmin[i] = -1;
      }
      else
      {
        // smallest/first time value
        // mmin[i] = rec.getInt(LightIntBandDB::Times);
        for (int l = 0; l < entries; l++)
        {
            SkyColor sc(rec.getInt(LightIntBandDB::Times + l), rec.getInt(LightIntBandDB::Values + l));
            colorRows[i].push_back(sc);
        }
      }
    }
    catch (...)
    {
      assert(false);
      LogError << "When trying to intialize sky, there was an error with getting an entry in LightIntBand.dbc id (" << i << "). Lightparam id : " << paramId << std::endl;
      /*
      DBCFile::Record rec = gLightIntBandDB.getByID(i);
      int entries = rec.getInt(LightIntBandDB::Entries);

      if (entries == 0)
      {
          mmin[i] = -1;
      }
      else
      {
          mmin[i] = rec.getInt(LightIntBandDB::Times);
          for (int l = 0; l < entries; l++)
          {
              SkyColor sc(rec.getInt(LightIntBandDB::Times + l), rec.getInt(LightIntBandDB::Values + l));
              colorRows[i].push_back(sc);
          }
      }*/
    }
  }

  // initialize float params
  int light_float_start = (paramId * NUM_SkyFloatParamsNames) - (NUM_SkyFloatParamsNames - 1);
  for (int i = 0; i < NUM_SkyFloatParamsNames; ++i)
    {
        try
        {
            DBCFile::Record rec = gLightFloatBandDB.getByID(light_float_start + i);
            int entries = rec.getInt(LightFloatBandDB::Entries);

            if (entries == 0)
            {
                // mmin_float[i] = -1;
            }
            else
            {
                // mmin_float[i] = rec.getInt(LightFloatBandDB::Times);
                for (int l = 0; l < entries; l++)
                {
                    SkyFloatParam sc(rec.getInt(LightFloatBandDB::Times + l), rec.getFloat(LightFloatBandDB::Values + l));
                    floatParams[i].push_back(sc);
                }
            }
        }
        catch (...)
        {
            assert(false);
            LogError << "When trying to intialize sky, there was an error with getting an entry in LightFloatBand.dbc id (" << i << "). Lightparam id : " << paramId << std::endl;
            /*
            DBCFile::Record rec = gLightFloatBandDB.getByID(i + 1);
            int entries = rec.getInt(LightFloatBandDB::Entries);

            if (entries == 0)
            {
                mmin_float[i] = -1;
            }
            else
            {
                mmin_float[i] = rec.getInt(LightFloatBandDB::Times);
                for (int l = 0; l < entries; l++)
                {
                    SkyFloatParam sc(rec.getInt(LightFloatBandDB::Times + l), rec.getFloat(LightFloatBandDB::Values + l));
                    floatParams[i].push_back(sc);
                }
            }*/
        }
    }

}

bool SkyParam::highlight_sky() const
{
  return _highlight_sky;
}

float SkyParam::river_shallow_alpha() const
{
  return _river_shallow_alpha;
}

float SkyParam::river_deep_alpha() const
{
  return _river_deep_alpha;
}

float SkyParam::ocean_shallow_alpha() const
{
  return _ocean_shallow_alpha;
}

float SkyParam::ocean_deep_alpha() const
{
  return _ocean_deep_alpha;
}

float SkyParam::glow() const
{
  return _glow;
}

void SkyParam::set_glow(float glow)
{
  _glow = glow;
}

void SkyParam::set_highlight_sky(bool state)
{
  _highlight_sky = state;
}

void SkyParam::set_river_shallow_alpha(float alpha)
{
  _river_shallow_alpha = alpha;
}

void SkyParam::set_river_deep_alpha(float alpha)
{
  _river_deep_alpha = alpha;
}

 void SkyParam::set_ocean_shallow_alpha(float alpha)
{
  _ocean_shallow_alpha = alpha;
}

void SkyParam::set_ocean_deep_alpha(float alpha)
{
  _ocean_deep_alpha = alpha;
}


Sky::Sky(DBCFile::Iterator data, Noggit::NoggitRenderContext context)
: _context(context)
, _selected(false)
{
  Id = data->getInt(LightDB::ID);
  mapId = data->getInt(LightDB::Map);
  pos = glm::vec3(data->getFloat(LightDB::PositionX) / skymul
                , data->getFloat(LightDB::PositionY) / skymul
                , data->getFloat(LightDB::PositionZ) / skymul);
  r1 = data->getFloat(LightDB::RadiusInner) / skymul;
  r2 = data->getFloat(LightDB::RadiusOuter) / skymul;

  global = (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f);

  for (int i = 0; i < NUM_SkyParamsNames; ++i)
  {
      int sky_param_id = data->getInt(LightDB::DataIDs + i);

      skyParams[i] = sky_param_id;
  }
}

Sky::Sky(ModernLightRecord const& data, Noggit::NoggitRenderContext context)
: _context(context)
, _selected(false)
{
  Id = data.id;
  mapId = data.map_id;
  pos = data.pos;
  r1 = data.r1;
  r2 = data.r2;
  global = (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f);

  for (int i = 0; i < NUM_SkyParamsNames; ++i)
    skyParams[i] = data.sky_params[i];
}

int Sky::getId() const
{
  return Id;
}

std::optional<SkyParam*> Sky::getParam(int param_index) const
{
  unsigned int param_id = skyParams[param_index];
  if (param_id == 0)
    return std::nullopt;
  
  if (cachedCurrentParam && cachedCurrentParam->Id == param_id) {
    return cachedCurrentParam;
  }
  
  SkyParam* param_ptr = skyparams::getOrCreateParam(param_id, _context);

  if (param_ptr)
  {
    cachedCurrentParam = param_ptr;
    return cachedCurrentParam;
  }
  else
  {
    cachedCurrentParam = nullptr;
    return std::nullopt;
  }
}

std::optional<SkyParam*> Sky::getCurrentParam() const
{
  return getParam(curr_sky_param);
}

std::vector<SkyColor> SkyParam::color_row_preview(int color_index) const
{
  std::vector<SkyColor> out;
  if (!has_modern_light_data() || color_index < 0 || color_index >= kModernLightColorCount)
    return out;

  out.reserve(light_data_keyframes.size());
  for (auto const& kf : light_data_keyframes)
  {
    glm::vec3 const& c = kf.colors[color_index];
    int const packed = (static_cast<int>(c.x * 255.f) << 16)
                     | (static_cast<int>(c.y * 255.f) << 8)
                     | static_cast<int>(c.z * 255.f);
    out.emplace_back(kf.time, packed);
  }
  return out;
}

bool SkyParam::interpolate_light_data(int time, LightDataKeyframe& out) const
{
  if (!has_modern_light_data())
    return false;

  auto const& kfs = light_data_keyframes;
  if (kfs.empty())
    return false;

  if (kfs.size() == 1)
  {
    out = kfs.front();
    return true;
  }

  LightDataKeyframe const* a = &kfs.back();
  LightDataKeyframe const* b = &kfs.front();
  int t_wrap = time;

  if (time < kfs.front().time)
  {
    a = &kfs.back();
    b = &kfs.front();
    t_wrap = time + DAY_DURATION;
  }
  else
  {
    for (std::size_t i = 0; i < kfs.size(); ++i)
    {
      if (kfs[i].time <= time)
      {
        a = &kfs[i];
        b = (i + 1 < kfs.size()) ? &kfs[i + 1] : &kfs.front();
        if (i + 1 >= kfs.size())
          t_wrap = (time < kfs.front().time) ? time + DAY_DURATION : time;
        break;
      }
    }
  }

  int t1 = a->time;
  int t2 = b->time;
  if (t2 < t1)
    t2 += DAY_DURATION;
  float const tt = (t2 == t1) ? 0.f : static_cast<float>(t_wrap - t1) / static_cast<float>(t2 - t1);

  out = *a;
  for (int i = 0; i < kModernLightColorCount; ++i)
    out.colors[i] = glm::mix(a->colors[i], b->colors[i], tt);

  out.fog_end = a->fog_end + (b->fog_end - a->fog_end) * tt;
  out.fog_scaler = a->fog_scaler + (b->fog_scaler - a->fog_scaler) * tt;
  out.fog_density = a->fog_density + (b->fog_density - a->fog_density) * tt;
  out.fog_height = a->fog_height + (b->fog_height - a->fog_height) * tt;
  out.fog_height_scaler = a->fog_height_scaler + (b->fog_height_scaler - a->fog_height_scaler) * tt;
  out.fog_height_density = a->fog_height_density + (b->fog_height_density - a->fog_height_density) * tt;
  out.end_fog_color = glm::mix(a->end_fog_color, b->end_fog_color, tt);
  out.end_fog_color_distance = a->end_fog_color_distance + (b->end_fog_color_distance - a->end_fog_color_distance) * tt;
  out.sun_fog_color = glm::mix(a->sun_fog_color, b->sun_fog_color, tt);
  out.sun_fog_strength = a->sun_fog_strength + (b->sun_fog_strength - a->sun_fog_strength) * tt;
  out.fog_height_color = glm::mix(a->fog_height_color, b->fog_height_color, tt);
  out.cloud_density = a->cloud_density + (b->cloud_density - a->cloud_density) * tt;
  for (int i = 0; i < 4; ++i)
  {
    out.fog_height_coeff[i] = a->fog_height_coeff[i] + (b->fog_height_coeff[i] - a->fog_height_coeff[i]) * tt;
    out.main_fog_coeff[i] = a->main_fog_coeff[i] + (b->main_fog_coeff[i] - a->main_fog_coeff[i]) * tt;
  }
  return true;
}

bool Sky::lightDataFor(int time, LightDataKeyframe& out) const
{
  auto param_opt = getCurrentParam();
  if (!param_opt.has_value())
    return false;
  return param_opt.value()->interpolate_light_data(time, out);
}

float Sky::floatParamFor(int r, int t) const
{
  auto param_opt = getCurrentParam();
  if (!param_opt.has_value())
    return 0.0f;

  SkyParam* const sky_param = param_opt.value();

  if (r < 0 || r >= NUM_SkyFloatParamsNames)
    return 0.0f;

  auto const& params = sky_param->floatParams[r];
  if (params.empty() && sky_param->has_modern_light_data())
  {
    LightDataKeyframe kf;
    if (!sky_param->interpolate_light_data(t, kf))
      return 0.f;
    switch (r)
    {
      case SKY_FOG_DISTANCE: return kf.fog_end;
      case SKY_FOG_MULTIPLIER: return kf.fog_scaler;
      case SKY_CLOUD_DENSITY: return kf.cloud_density;
      default: break;
    }
  }

  if (params.empty())
  {
    return 0.0f;
  }
  float c1, c2;
  int t1, t2;
  int const last = static_cast<int>(params.size()) - 1;

  if (t < params.front().time)
  {
    // reverse interpolate
    c1 = params[static_cast<size_t>(last)].value;
    c2 = params[0].value;
    t1 = params[static_cast<size_t>(last)].time;
    t2 = params[0].time + DAY_DURATION;
    t += DAY_DURATION;
  }
  else
  {
    bool found = false;
    for (int i = last; i >= 0; --i)
    {
      if (params[static_cast<size_t>(i)].time <= t)
      {
        c1 = params[static_cast<size_t>(i)].value;
        t1 = params[static_cast<size_t>(i)].time;

        if (i == last)
        {
          c2 = params[0].value;
          t2 = params[0].time + DAY_DURATION;
        }
        else
        {
          c2 = params[static_cast<size_t>(i + 1)].value;
          t2 = params[static_cast<size_t>(i + 1)].time;
        }
        found = true;
        break;
      }
    }
    if (!found)
    {
      c1 = params[0].value;
      c2 = params[0].value;
      t1 = params[0].time;
      t2 = params[0].time + DAY_DURATION;
    }
  }

  float tt = (t2 == t1) ? 0.f : static_cast<float>(t - t1) / static_cast<float>(t2 - t1);
  return c1 + ((c2 - c1) * tt);
}

glm::vec3 Sky::colorFor(int r, int t) const
{
  auto param_opt = getCurrentParam();
  if (!param_opt.has_value())
    return glm::vec3(0.0f, 0.0f, 0.0f);

  SkyParam* const sky_param = param_opt.value();

  if (r < 0 || r >= NUM_SkyColorNames)
    return glm::vec3(0.0f);

  auto const& rows = sky_param->colorRows[r];
  if (rows.empty())
  {
    if (sky_param->has_modern_light_data())
    {
      LightDataKeyframe kf;
      if (sky_param->interpolate_light_data(t, kf))
        return kf.colors[r];
    }
    return glm::vec3(0.0f, 0.0f, 0.0f);
  }
  glm::vec3 c1, c2;
  int t1, t2;
  int const last = static_cast<int>(rows.size()) - 1;

  if (last == 0)
  {
      c1 = rows[0].color;
      c2 = rows[0].color;
      t1 = rows[0].time;
      t2 = rows[0].time + DAY_DURATION;
      t += DAY_DURATION;
  }
  else if (t < rows.front().time)
  {
      // reverse interpolate
      c1 = rows[static_cast<size_t>(last)].color;
      c2 = rows[0].color;
      t1 = rows[static_cast<size_t>(last)].time;
      t2 = rows[0].time + DAY_DURATION;
      t += DAY_DURATION;
  }
  else
  {
      bool found = false;
      for (int i = last; i >= 0; --i)
      {
          if (rows[static_cast<size_t>(i)].time <= t)
          {
              c1 = rows[static_cast<size_t>(i)].color;
              t1 = rows[static_cast<size_t>(i)].time;

              if (i == last)
              {
                  c2 = rows[0].color;
                  t2 = rows[0].time + DAY_DURATION;
              }
              else
              {
                  c2 = rows[static_cast<size_t>(i + 1)].color;
                  t2 = rows[static_cast<size_t>(i + 1)].time;
              }
              found = true;
              break;
          }
      }
      if (!found)
      {
          c1 = rows[0].color;
          c2 = rows[0].color;
          t1 = rows[0].time;
          t2 = rows[0].time + DAY_DURATION;
      }
  }

  float tt = (t2 == t1) ? 0.f : static_cast<float>(t - t1) / static_cast<float>(t2 - t1);
  return c1*(1.0f - tt) + c2*tt;
}

const float rad = 400.0f;

//...............................top....med....medh........horiz..........bottom
const math::degrees angles[] = { math::degrees (90.0f)
                               , math::degrees (18.0f)
                               , math::degrees (10.0f)
                               , math::degrees (3.0f)
                               , math::degrees (0.0f)
                               , math::degrees (-30.0f)
                               , math::degrees (-90.0f)
                               };
const int cnum = 7;
const int skycolors[cnum] = { SKY_COLOR_TOP, SKY_COLOR_MIDDLE, SKY_COLOR_BAND1, SKY_COLOR_BAND2, SKY_COLOR_SMOG, SKY_FOG_COLOR, SKY_FOG_COLOR };
const int hseg = 32;

namespace
{
  constexpr int kDefaultSkyLightId = 1;

  bool add_default_light_from_modern(std::vector<Sky>& skies, int& numSkies, Noggit::NoggitRenderContext context)
  {
    ModernLightRecord const* const fallback =
      ModernLightTables::instance().find_light(kDefaultSkyLightId);
    if (!fallback)
      return false;

    Sky s(*fallback, context);
    s.global = true;
    skies.push_back(s);
    ++numSkies;
    return true;
  }

  bool add_default_light_from_dbc(std::vector<Sky>& skies, int& numSkies, Noggit::NoggitRenderContext context)
  {
    for (DBCFile::Iterator i = gLightDB.begin(); i != gLightDB.end(); ++i)
    {
      if (kDefaultSkyLightId == static_cast<int>(i->getUInt(LightDB::ID)))
      {
        Sky s(i, context);
        s.global = true;
        skies.push_back(s);
        ++numSkies;
        return true;
      }
    }
    return false;
  }

  bool add_default_light(std::vector<Sky>& skies
                       , int& numSkies
                       , Noggit::NoggitRenderContext context
                       , bool prefer_modern)
  {
    if (prefer_modern && add_default_light_from_modern(skies, numSkies, context))
      return true;
    return add_default_light_from_dbc(skies, numSkies, context);
  }
}


void Skies::loadZoneLights(int map_id)
{
  if (noggit_use_modern_sky_lights())
  {
    ModernLightTables::instance().fill_zone_lights(map_id, zoneLightsWotlk);
    for (auto& zl : zoneLightsWotlk)
    {
      Sky* light_ptr = findSkyById(static_cast<int>(zl.lightId));
      if (light_ptr)
        light_ptr->zone_light = true;
    }
    return;
  }

  if (Noggit::Project::CurrentProject::get()->projectVersion != Noggit::Project::ProjectVersion::WOTLK)
    return;

    // read zone lights from csv file
  {
    std::string zonelight_db_path = Noggit::Application::NoggitApplication::instance()->getConfiguration()->ApplicationNoggitDefinitionsPath
      + "\\ZoneLight.3.4.3.56262.csv";
    QString qPath = QString::fromStdString(zonelight_db_path);
    QFile file(qPath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      QTextStream in(&file);

      // Skip the header line
      std::string headerLine = in.readLine().toStdString();
      assert(headerLine == "ID,Name,MapID,LightID");

      while (!in.atEnd())
      {
        QString line = in.readLine();

        // Split the line by comma
        QStringList fields = line.split(',');

        assert(fields.size() == 4);
        if (fields.size() != 4)
        {
          continue;
        }

        bool ok;
        int light_map_id = fields[2].toInt(&ok);
        assert(ok);
        // only load this map
        if (map_id != light_map_id)
          continue;

        ZoneLight zone_light_entry;
        zone_light_entry.id = fields[0].toInt(&ok);
        zone_light_entry.name = fields[1].toStdString();
        // zone_light_entry.mapId = light_map_id;
        zone_light_entry.lightId = fields[3].toInt(&ok);

        // zoneLightsWotlk[zone_light_entry.id] = zone_light_entry;
        zoneLightsWotlk.push_back(zone_light_entry);

        // get the light reference
        Sky* light_ptr = findSkyById(zone_light_entry.lightId);
        // if light was not loaded, most likely missing or not in map.
        assert(light_ptr != nullptr);
        if (!light_ptr)
          continue;
        // zoneLightsWotlk[zone_light_entry.id].light = findSkyById(zone_light_entry.lightId);
        light_ptr->zone_light = true;
      }
      file.close();
    }
    else
    {
      LogError << "Failed loading Zone Lights. Can't open " << zonelight_db_path << std::endl;
      return;
    }
  }

  // load zone light points to temporary object
  std::unordered_map<int, std::vector<ZoneLightPoint>> zoneLightPoints;
  {
    std::string zonelightpoints_db_path = Noggit::Application::NoggitApplication::instance()->getConfiguration()->ApplicationNoggitDefinitionsPath
      + "\\ZoneLightPoint.3.4.3.56262.csv";
    QString qPath = QString::fromStdString(zonelightpoints_db_path);
    QFile file(qPath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      QTextStream in(&file);

      // Skip the header line
      std::string headerLine = in.readLine().toStdString();
      assert(headerLine == "ID,Pos_0,Pos_1,PointOrder,ZoneLightID");

      while (!in.atEnd())
      {
        QString line = in.readLine();

        // Split the line by comma
        QStringList fields = line.split(',');

        assert(fields.size() == 5);
        if (fields.size() != 5)
        {
          continue;
        }

        bool ok;
        int zone_light_id = fields[4].toUInt(&ok);
        assert(ok);

        // Check if the vector contains the zone_light_id
        // if (!zoneLightsWotlk.contains(zone_light_id))
        auto it = std::find_if(zoneLightsWotlk.begin(), zoneLightsWotlk.end(),
          [zone_light_id](const ZoneLight& zoneLight) {
            return zoneLight.id == zone_light_id;
          });
        if (it == zoneLightsWotlk.end())
          continue;

        ZoneLightPoint zone_light_point_entry;
        zone_light_point_entry.id = fields[0].toUInt(&ok);
        zone_light_point_entry.zoneLightId = zone_light_id;

        // convert server to client coords
        // need to swap X and Y
        zone_light_point_entry.posX = -fields[2].toFloat(&ok) + ZEROPOINT;
        zone_light_point_entry.posY = -fields[1].toFloat(&ok) + ZEROPOINT;
        zone_light_point_entry.pointOrder = fields[3].toUInt(&ok);

        // Automatically create a vector if the key doesn't exist, and add the entry
        zoneLightPoints[zone_light_id].push_back(zone_light_point_entry);

        // bad idea, this blindly trusts the ordering
        // zoneLightsWotlk[zone_light_id].points.push_back(glm::vec2(zone_light_point_entry.posX, zone_light_point_entry.posY));

      }
      file.close();
    }
    else
    {
      LogError << "Failed loading Zone Light Points. Can't open " << zonelightpoints_db_path << std::endl;
      return;
    }
  }

  // re order points, because we don't trust the storage order
  for (auto& points_list : zoneLightPoints)
  {
    // if (!zoneLightsWotlk.contains(points_list.first))
    //   continue;

    // polygon must have at least 3 points
    assert(points_list.second.size() > 2);

    // Check for duplicate pointOrder values
    for (int i = 1; i < points_list.second.size(); ++i) {
      if (points_list.second[i].pointOrder == points_list.second[i - 1].pointOrder)
      {
        assert(false);
        continue;
      }
    }

    // Sort the vector based on pointOrder (ascending)
    std::sort(points_list.second.begin(), points_list.second.end(), [](const ZoneLightPoint& a, const ZoneLightPoint& b) {
      return a.pointOrder < b.pointOrder;
      });

  }


  for (auto& zone_light : zoneLightsWotlk)
  {
    auto& list = zoneLightPoints[zone_light.id];
    // insert reordered points
    for (auto& point : list)
    {
      zone_light.points.push_back(glm::vec2(point.posX, point.posY));
    }

    // calculate 2d extents
    math::aabb_2d const bounds (zone_light.points);

    zone_light._extents[0] = bounds.min;
    zone_light._extents[1] = bounds.max;
  }

}

Sky* Skies::findSkyById(int sky_id)
{
  for (auto& sky : skies)
  {
    if (sky.getId() == sky_id)
    {
      return &sky;
    }
  }
  return nullptr;
}

Skies::Skies(unsigned int mapid, Noggit::NoggitRenderContext context)
  : stars (ModelInstance("Environments\\Stars\\Stars.mdx", context))
  , _context(context)
  , _indices_count(0)
  , _last_pos(glm::vec3(0.0f, 0.0f, 0.0f))
{
  bool has_global = false;
  bool const use_modern_lights = noggit_use_modern_sky_lights();

  if (use_modern_lights)
  {
    for (auto const& rec : ModernLightTables::instance().lights_for_map(mapid))
    {
      Sky s(rec, _context);
      skies.push_back(s);
      ++numSkies;
      if (s.pos == glm::vec3(0, 0, 0))
        has_global = true;
    }
  }
  else
  {
    for (DBCFile::Iterator i = gLightDB.begin(); i != gLightDB.end(); ++i)
    {
      if (mapid == i->getUInt(LightDB::Map))
      {
        Sky s(i, _context);
        skies.push_back(s);
        numSkies++;

        if (s.pos == glm::vec3(0, 0, 0))
          has_global = true;
      }
    }
  }

  if (skies.empty() || !has_global)
  {
    if (skies.empty())
    {
      LogDebug << "No light data found for map " << mapid << "; using default light id "
               << kDefaultSkyLightId << std::endl;
    }
    else
    {
      LogDebug << "No global light for map " << mapid << "; using default light id "
               << kDefaultSkyLightId << std::endl;
    }

    if (add_default_light(skies, numSkies, _context, use_modern_lights))
    {
      using_fallback_global = true;
    }
    else
    {
      LogError << "Failed to load default light id " << kDefaultSkyLightId
               << " for map " << mapid << std::endl;
    }
  }

  std::sort(skies.begin(), skies.end());
  loadZoneLights(mapid);
}

Sky* Skies::createNewSky(Sky*  old_sky, unsigned int new_id, glm::vec3& pos)
{
  // TODO

  Sky new_sky_copy = *old_sky;
  new_sky_copy.Id = new_id;
  new_sky_copy.pos = pos;

  new_sky_copy.weight = 0.f;
  new_sky_copy.is_new_record = true;

  new_sky_copy.save_to_dbc();

  skies.push_back(new_sky_copy);

  numSkies++;

  // refresh rendering & weights
  std::sort(skies.begin(), skies.end());
  force_update();

  for (Sky& sky : skies)
  {
    if (sky.Id == new_id)
    {
        return &sky;
    }
  }
  return nullptr;
}

// returns the global light, not the highest weight
Sky* Skies::findSkyWeights(glm::vec3 pos)
{
  Sky* default_sky = nullptr;

  for (auto& sky : skies)
  {
    if (sky.global || sky.pos == glm::vec3(0, 0, 0))
    {
      default_sky = &sky;
      break;
    }
  }

  std::sort(skies.begin(), skies.end(), [=](Sky& a, Sky& b)
  {
    return glm::distance(pos, a.pos) > glm::distance(pos, b.pos);
  });

  for (auto& sky : skies)
  {
    float distance_to_light = glm::distance(pos, sky.pos);

    if (default_sky == &sky || distance_to_light > sky.r2)
    {
      sky.weight = 0.f;
      continue;
    }

    float const length_of_falloff = sky.r2 - sky.r1;
    if (length_of_falloff > 1e-6f)
    {
      sky.weight = (sky.r2 - distance_to_light) / length_of_falloff;
      if (distance_to_light <= sky.r1)
        sky.weight = 1.0f;
    }
    else
    {
      sky.weight = (distance_to_light <= sky.r2) ? 1.0f : 0.0f;
    }
  }

  // Light zones
  glm::vec2 const pos_2d = glm::vec2(pos.x, pos.z);
  for (auto& lightzone : zoneLightsWotlk)
  {
    if (math::is_inside_of_aabb_2d(pos_2d, lightzone._extents[0], lightzone._extents[1]))
    {
      bool inside = math::is_inside_of_polygon(pos_2d, lightzone.points);

      if (inside)
      {
        Sky* sky = findSkyById(lightzone.lightId);
        if (sky)
          sky->weight = 1.0f;
      }
    }
  }

  return default_sky;
}

Sky* Skies::findClosestSkyByWeight()
{
    // gets the highest weight sky
    if (skies.size() == 0)
        return nullptr;

    Sky* closest_sky = &skies[0];
    for (auto& sky : skies)
    {
        // use >= to make sure when we have multiple with the same weight, 
        // last one has priority, because it is the closest
        // skies is sorted by distance to center
        if (sky.weight > 0.0f && sky.weight >= closest_sky->weight)
            closest_sky = &sky;
    }
    return closest_sky;
}

Sky* Skies::findClosestSkyByDistance(glm::vec3 pos)
{
    if (skies.size() == 0)
        return nullptr;

    Sky* closest = &skies[0];
    float distance = 1000000.f;
    for (auto& sky : skies)
    {
        float distanceToCenter = glm::distance(pos, sky.pos);

        if (distanceToCenter <= sky.r2 && distanceToCenter < distance)
        {
            distance = distanceToCenter;
            closest = &sky;
        }
    }

    return closest;
}

void Skies::setCurrentParam(int param_id)
{
  assert(param_id < NUM_SkyParamsNames);

  for (auto& sky : skies)
  {
      Sky* skyptr = &sky;
      skyptr->curr_sky_param = param_id;
  }
}

void Skies::apply_light_data_fog(LightDataKeyframe const& kf)
{
  // Modern LightData often leaves FogEnd at 0 and drives atmosphere via height/density.
  // Inventing FogEnd=6500 and then applying a tiny FogScaler (e.g. 0.005 on Exile's Reach)
  // made FogStart ~32 yd and the world vanish into distance fog inside local light zones.
  if (kf.fog_end == 0.f)
  {
    _fog_distance = 6500.f;
    _fog_multiplier = 0.92f;
  }
  else
  {
    _fog_distance = kf.fog_end;
    _fog_multiplier = kf.fog_scaler == 0.f ? 0.1f : kf.fog_scaler;
  }
  _fog_density = kf.fog_density;
  _cloud_density = kf.cloud_density;
  _end_fog_color = kf.end_fog_color;
  _end_fog_color_distance = kf.end_fog_color_distance;
  _fog_height_color = kf.fog_height_color;
  _fog_height = kf.fog_height;
  _fog_height_scaler = kf.fog_height_scaler;
  _fog_height_density = kf.fog_height_density;
  for (int i = 0; i < 4; ++i)
  {
    _fog_height_coeff[i] = kf.fog_height_coeff[i];
    _main_fog_coeff[i] = kf.main_fog_coeff[i];
  }
}

void Skies::blend_light_data_fog(LightDataKeyframe const& kf, float weight, float weight_remain)
{
  // Only blend classic distance fog when this keyframe actually defines FogEnd.
  // FogScaler alone (with FogEnd==0) must not pull FogStart down to a few dozen yards.
  if (kf.fog_end != 0.f)
  {
    _fog_distance = _fog_distance * weight_remain + kf.fog_end * weight;
    float const scaler = kf.fog_scaler == 0.f ? 0.1f : kf.fog_scaler;
    _fog_multiplier = _fog_multiplier * weight_remain + scaler * weight;
  }
  if (kf.fog_density != 0.f)
    _fog_density = _fog_density * weight_remain + kf.fog_density * weight;
  _cloud_density = _cloud_density * weight_remain + kf.cloud_density * weight;
  _end_fog_color = glm::mix(_end_fog_color, kf.end_fog_color, weight);
  if (kf.end_fog_color_distance != 0.f)
    _end_fog_color_distance = _end_fog_color_distance * weight_remain + kf.end_fog_color_distance * weight;
  _fog_height_color = glm::mix(_fog_height_color, kf.fog_height_color, weight);
  _fog_height = _fog_height * weight_remain + kf.fog_height * weight;
  _fog_height_scaler = _fog_height_scaler * weight_remain + kf.fog_height_scaler * weight;
  _fog_height_density = _fog_height_density * weight_remain + kf.fog_height_density * weight;
  for (int i = 0; i < 4; ++i)
  {
    _fog_height_coeff[i] = _fog_height_coeff[i] * weight_remain + kf.fog_height_coeff[i] * weight;
    _main_fog_coeff[i] = _main_fog_coeff[i] * weight_remain + kf.main_fog_coeff[i] * weight;
  }
}

bool Skies::update_sky_colors(glm::vec3 pos, int time, bool global_only)
{
  if (numSkies == 0)
  {
    return false;
  }

  // Zone weights barely change for small camera moves; avoid sorting all skies every frame while flying.
  constexpr float k_min_sky_recalc_dist_sq = 80.f * 80.f;
  glm::vec3 const delta = pos - _last_pos;
  float const dist_sq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

  if (_last_time == time && !_force_update && dist_sq < k_min_sky_recalc_dist_sq)
  {
    return false;
  }
  _force_update = false;

  bool const modern = noggit_use_modern_sky_lights();

  Sky* default_sky = findSkyWeights(pos);

  // initialize lightning with default(global) light
  if (default_sky && default_sky->getCurrentParam().has_value())
  {
    for (int i = 0; i < NUM_SkyColorNames; ++i)
    {
      color_set[i] = default_sky->colorFor(i, time);
    }

    if (modern)
    {
      LightDataKeyframe kf;
      if (default_sky->lightDataFor(time, kf))
        apply_light_data_fog(kf);
      else
      {
        _fog_distance = 6500.0f;
        _fog_multiplier = 0.92f;
      }
    }
    else
    {
      float fog_distance = default_sky->floatParamFor(SKY_FOG_DISTANCE, time);
      _fog_distance = fog_distance == 0.0f ? 6500.0f : fog_distance;

      float fog_multiplier = default_sky->floatParamFor(SKY_FOG_MULTIPLIER, time);
      _fog_multiplier = fog_multiplier == 0.0f ? 0.1f : fog_multiplier;
    }

    _celestial_glow = default_sky->floatParamFor(SKY_CELESTIAL_GLOW, time);
    _cloud_density = default_sky->floatParamFor(SKY_CLOUD_DENSITY, time);
    _unknown_float_param4 = default_sky->floatParamFor(SKY_UNK_FLOAT_PARAM_4, time);
    _unknown_float_param5 = default_sky->floatParamFor(SKY_UNK_FLOAT_PARAM_5, time);

    // param values
    auto param_opt = default_sky->getCurrentParam();
    if (param_opt.has_value())
    {
      SkyParam* const default_sky_param = param_opt.value();

      _river_shallow_alpha = default_sky_param->river_shallow_alpha();
      _river_deep_alpha = default_sky_param->river_deep_alpha();
      _ocean_shallow_alpha = default_sky_param->ocean_shallow_alpha();
      _ocean_deep_alpha = default_sky_param->ocean_deep_alpha();
      _glow = default_sky_param->glow();
    }
    else
    {
      // if no param data, use some default. TODO : check how client does it.
      _river_shallow_alpha = 0.5f;
      _river_deep_alpha = 1.0f;
      _ocean_shallow_alpha = 0.75f;
      _ocean_deep_alpha = 1.0f;
      _glow = 0.5f;
    }
  }
  else
  {
    LogError << "Failed to load default light. Something went seriously wrong. Potentially corrupt Light.dbc" << std::endl;

    for (int i = 0; i < NUM_SkyColorNames; ++i)
    {
      color_set[i] = glm::vec3(1.0f, 1.0f, 1.0f);
    }

    _fog_multiplier = 0.1f;
    _fog_distance = 6500.0f;
    _fog_density = 0.f;
    _end_fog_color = glm::vec3(0.f);
    _end_fog_color_distance = 0.f;
    _celestial_glow = 1.0f;
    _cloud_density = 1.0f;
    _unknown_float_param4 = 1.0f;
    _unknown_float_param5 = 1.0f;

    _river_shallow_alpha = 0.5f;
    _river_deep_alpha = 1.0f;
    _ocean_shallow_alpha = 0.75f;
    _ocean_deep_alpha = 1.0f;
    _glow = 0.5f;

  }

  if (!global_only)
  {
    // Blending interpolation with local lights
    for (size_t j = 0; j<skies.size(); j++) 
    {
      Sky const& sky = skies[j];

      if (sky.weight > 0.f)
      {
        // now calculate the color rows
        for (int i = 0; i < NUM_SkyColorNames; ++i) 
        {
          if ((sky.colorFor(i, time).x>1.0f) || (sky.colorFor(i, time).y>1.0f) || (sky.colorFor(i, time).z>1.0f))
          {
            LogDebug << "Sky " << j << " " << i << " is out of bounds!" << std::endl;
            continue;
          }
          auto timed_color = sky.colorFor(i, time);
          color_set[i] = glm::mix(color_set[i], timed_color, sky.weight);
        }

        auto param_opt = sky.getCurrentParam();
        if (param_opt.has_value())
        {
          SkyParam* default_sky_param = param_opt.value();

          float sky_weight_remain = (1.0f - sky.weight);

          if (modern)
          {
            LightDataKeyframe kf;
            if (sky.lightDataFor(time, kf))
              blend_light_data_fog(kf, sky.weight, sky_weight_remain);
          }
          else
          {
            float fog_distance = sky.floatParamFor(SKY_FOG_DISTANCE, time);
            if (fog_distance != 0.0f)
              _fog_distance = (_fog_distance * sky_weight_remain) + (fog_distance * sky.weight);

            float fog_multiplier = sky.floatParamFor(SKY_FOG_MULTIPLIER, time);
            if (fog_multiplier != 0.0f)
              _fog_multiplier = (_fog_multiplier * sky_weight_remain) + (fog_multiplier * sky.weight);
          }

          _celestial_glow = (_celestial_glow * sky_weight_remain) + (sky.floatParamFor(SKY_CELESTIAL_GLOW, time) * sky.weight);
          _cloud_density = (_cloud_density * sky_weight_remain) + (sky.floatParamFor(SKY_CLOUD_DENSITY, time) * sky.weight);
          _unknown_float_param4 = (_unknown_float_param4 * sky_weight_remain) + (sky.floatParamFor(SKY_UNK_FLOAT_PARAM_4, time) * sky.weight);
          _unknown_float_param5 = (_unknown_float_param5 * sky_weight_remain) + (sky.floatParamFor(SKY_UNK_FLOAT_PARAM_5, time) * sky.weight);

          _river_shallow_alpha = (_river_shallow_alpha * sky_weight_remain) + (default_sky_param->river_shallow_alpha() * sky.weight);
          _river_deep_alpha = (_river_deep_alpha * sky_weight_remain) + (default_sky_param->river_deep_alpha() * sky.weight);
          _ocean_shallow_alpha = (_ocean_shallow_alpha * sky_weight_remain) + (default_sky_param->ocean_shallow_alpha() * sky.weight);
          _ocean_deep_alpha = (_ocean_deep_alpha * sky_weight_remain) + (default_sky_param->ocean_deep_alpha() * sky.weight);

          _glow = (_glow * sky_weight_remain) + (default_sky_param->glow() * sky.weight);
        }
        else
        {
          // if no data for param index, it just uses default values from global light, no blending
        }

      }

    }
  }

  // fog_distance_end()/start() already convert classic ×36 storage; reuse for fogRate.
  const float fogEnd = fog_distance_end();
  const float fogStart = fog_distance_start();
  const float fogRange = fogEnd - fogStart;

  constexpr float fogFarClip = 1583.333374f;

  if (fogRange <= fogFarClip)
  {
    _fog_rate = ((1.0f - (fogRange / fogFarClip)) * 5.5f) + 1.5f;
  }
  else
  {
    _fog_rate = 1.5f;
  }

  _last_pos = pos;
  _last_time = time;

  _need_color_buffer_update = true;
  return true;
}

bool Skies::draw(glm::mat4x4 const& model_view
                , glm::mat4x4 const& projection
                , glm::vec3 const& camera_pos
                , OpenGL::Scoped::use_program& m2_shader
                , math::frustum const& frustum
                , const float& cull_distance
                , int animtime
                , int time
                /*, bool draw_particles*/
                , bool draw_skybox
                , OutdoorLightStats const& light_stats
                )
{
  if (numSkies == 0)
  {
    return false;
  }

  if (!_uploaded)
  {
    upload();
  }

  if (_need_color_buffer_update)
  {
    update_color_buffer();
  }

  {
    OpenGL::Scoped::use_program shader {*_program.get()};

    if(_need_vao_update)
    {
      update_vao(shader);
    }

    {
      OpenGL::Scoped::vao_binder const _ (_vao);
       
      shader.uniform("model_view_projection", projection * model_view);
      shader.uniform("camera_pos", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));

      gl.drawElements(GL_TRIANGLES, _indices_count, GL_UNSIGNED_SHORT, nullptr);
    }
  }

  if (draw_skybox)
  {
    bool combine_flag = false;
    bool has_skybox = false;

    // Prefer the highest-weight sky that actually has a skybox model.
    Sky* best_sky = nullptr;
    float best_weight = 0.f;
    for (Sky& sky : skies)
    {
      auto param_opt = sky.getCurrentParam();
      if (sky.weight > best_weight && param_opt.has_value() && param_opt.value()->skybox)
      {
        best_weight = sky.weight;
        best_sky = &sky;
      }
    }

    auto const draw_sky_model = [&](ModelInstance& model, SkyParam* curr_param, float weight, bool full_day)
    {
      model.model->trans = weight;
      model.pos = camera_pos;
      model.scale = 0.1f;
      model.recalcExtents();

      OpenGL::M2RenderState model_render_state;
      model_render_state.tex_arrays = {0, 0};
      model_render_state.tex_indices = {0, 0};
      model_render_state.tex_unit_lookups = {-1, -1};
      gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      gl.disable(GL_BLEND);
      gl.depthMask(GL_TRUE);
      m2_shader.uniform("blend_mode", 0);
      m2_shader.uniform("unfogged", static_cast<int>(model_render_state.unfogged));
      m2_shader.uniform("unlit",  static_cast<int>(model_render_state.unlit));
      m2_shader.uniform("tex_unit_lookup_1", 0);
      m2_shader.uniform("tex_unit_lookup_2", 0);
      m2_shader.uniform("terrain_uv_mask", 0);
      m2_shader.uniform("pixel_shader", 0);

      int skyboxtime = animtime;
      if (full_day && curr_param && (curr_param->skyboxFlags & LIGHT_SKYBOX_FULL_DAY))
      {
        unsigned int anim_lenght = model.model->get_anim_lenght(0);
        float day_fraction = static_cast<float>(time % DAY_DURATION) / DAY_DURATION;
        skyboxtime = static_cast<int>(day_fraction * anim_lenght);
      }

      model.model->renderer()->draw(model_view
                                   , model
                                   , m2_shader
                                   , model_render_state
                                   , frustum
                                   , 1000000
                                   , camera_pos
                                   , skyboxtime
                                   , display_mode::in_3D
                                   , true
                                   , true);
    };

    if (best_sky)
    {
      has_skybox = true;
      SkyParam* curr_param = best_sky->getCurrentParam().value();
      if ((curr_param->skyboxFlags & LIGHT_SKYBOX_COMBINE))
        combine_flag = true;

      draw_sky_model(curr_param->skybox.value(), curr_param, best_sky->weight, true);

      if (curr_param->celestial_skybox.has_value())
        draw_sky_model(curr_param->celestial_skybox.value(), curr_param, best_sky->weight, true);
    }
    // if it's night, draw the stars
    if (light_stats.nightIntensity > 0 && (combine_flag || !has_skybox))
    {
      stars.model->trans = light_stats.nightIntensity;
      stars.pos = camera_pos;
      stars.scale = 0.1f;
      stars.recalcExtents();
    
      OpenGL::M2RenderState model_render_state;
      model_render_state.tex_arrays = {0, 0};
      model_render_state.tex_indices = {0, 0};
      model_render_state.tex_unit_lookups = {-1, -1};
      gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      gl.disable(GL_BLEND);
      gl.depthMask(GL_TRUE);
      m2_shader.uniform("blend_mode", 0);
      m2_shader.uniform("unfogged", static_cast<int>(model_render_state.unfogged));
      m2_shader.uniform("unlit",  static_cast<int>(model_render_state.unlit));
      m2_shader.uniform("tex_unit_lookup_1", 0);
      m2_shader.uniform("tex_unit_lookup_2", 0);
      m2_shader.uniform("terrain_uv_mask", 0);
      m2_shader.uniform("pixel_shader", 0);
    
      stars.model->renderer()->draw(model_view
                                   , stars
                                   , m2_shader
                                   , model_render_state
                                   , frustum
                                   , 1000000
                                   , camera_pos
                                   , animtime
                                   , display_mode::in_3D
                                   , true
                                   , true);
    }
  }


  return true;
}

void Skies::drawLightingSpheres (glm::mat4x4 const& model_view
  , glm::mat4x4 const& projection
  , glm::vec3 const& camera_pos
  , math::frustum const& frustum
  , const float& cull_distance
)
{
  for (Sky& sky : skies)
  {
    if (glm::distance(sky.pos, camera_pos) <= cull_distance) // TODO: frustum cull here
    {
        glm::vec4 diffuse = { color_set[LIGHT_GLOBAL_DIFFUSE], 1.f };
        glm::vec4 ambient = { color_set[LIGHT_GLOBAL_AMBIENT], 1.f };

        Log << sky.getId() << " <=> (x,y,z) : " << sky.pos.x << "," << sky.pos.y << "," << sky.pos.z << " -- r1 : " << sky.r1 << " -- r2 : " << sky.r2 << std::endl;

        _sphere_render.draw(model_view * projection, sky.pos, ambient, sky.r1, 32, 18, 1.f);
        _sphere_render.draw(model_view * projection, sky.pos, diffuse, sky.r2, 32, 18, 1.f);
    }
  }
}

void Skies::drawLightingSphereHandles (glm::mat4x4 const& model_view
  , glm::mat4x4 const& projection
  , glm::vec3 const& camera_pos
  , math::frustum const& frustum
  , const float& cull_distance
  , bool draw_spheres)
{
  for (Sky& sky : skies)
  {
    if (glm::distance(sky.pos, camera_pos) - sky.r2 <= cull_distance) // TODO: frustum cull here
    {

      _sphere_render.draw(model_view * projection, sky.pos, {1.f, 0.f, 0.f, 1.f}, 5.f);

      if (sky.selected())
      {
        glm::vec3 diffuse = color_set[LIGHT_GLOBAL_DIFFUSE];
        glm::vec3 ambient = color_set[LIGHT_GLOBAL_AMBIENT];
        _sphere_render.draw(model_view * projection, sky.pos, {ambient.x, ambient.y, ambient.z, 0.3}, sky.r1);
        _sphere_render.draw(model_view * projection, sky.pos, {diffuse.x, diffuse.y, diffuse.z, 0.3}, sky.r2);
      }
    }
  }
}

bool Skies::hasSkies() const
{
  return numSkies > 0;
}

float Skies::river_shallow_alpha() const
{
  return _river_shallow_alpha;
}

float Skies::river_deep_alpha() const
{
  return _river_deep_alpha;
}

float Skies::ocean_shallow_alpha() const
{
  return _ocean_shallow_alpha;
}

float Skies::ocean_deep_alpha() const
{
  return _ocean_deep_alpha;
}

float Skies::fog_distance_end() const
{
  // Classic Light.dbc FogEnd is stored ×36; modern LightData FogEnd is already yards.
  if (noggit_use_modern_sky_lights())
    return _fog_distance;
  return _fog_distance / 36.f;
}

float Skies::fog_distance_start() const
{
  return fog_distance_end() * _fog_multiplier;
}

float Skies::fog_distance_multiplier() const
{
  return _fog_multiplier;
}

float Skies::celestial_glow() const
{
  return _celestial_glow;
}

float Skies::cloud_density() const
{
  return _cloud_density;
}

float Skies::unknown_float_param4() const
{
  return _unknown_float_param4;
}

float Skies::unknown_float_param5() const
{
  return _unknown_float_param5;
}

float Skies::glow() const
{
  return _glow;
}

float Skies::fogRate() const
{
  return _fog_rate;
}

float Skies::fog_density() const
{
  return _fog_density;
}

glm::vec3 Skies::end_fog_color() const
{
  return _end_fog_color;
}

float Skies::end_fog_color_distance() const
{
  return _end_fog_color_distance;
}

glm::vec3 Skies::fog_height_color() const
{
  return _fog_height_color;
}

float Skies::fog_height() const
{
  return _fog_height;
}

float Skies::fog_height_scaler() const
{
  return _fog_height_scaler;
}

float Skies::fog_height_density() const
{
  return _fog_height_density;
}

std::array<float, 4> Skies::fog_height_coeff() const
{
  return {_fog_height_coeff[0], _fog_height_coeff[1], _fog_height_coeff[2], _fog_height_coeff[3]};
}

std::array<float, 4> Skies::main_fog_coeff() const
{
  return {_main_fog_coeff[0], _main_fog_coeff[1], _main_fog_coeff[2], _main_fog_coeff[3]};
}

void Skies::unload()
{
  _program.reset();
  _vertex_array.unload();
  _buffers.unload();
  _sphere_render.unload();

  _uploaded = false;
  _need_vao_update = true;

}

void Skies::force_update()
{
  _force_update = true;
}

void Skies::upload()
{
  _program.reset(new OpenGL::program(
    {
        {GL_VERTEX_SHADER, R"code(
#version 330 core

uniform mat4 model_view_projection;
uniform vec3 camera_pos;

in vec3 position;
in vec3 color;

out vec3 f_color;

void main()
{
  vec4 pos = vec4(position + camera_pos, 1.f);
  gl_Position = model_view_projection * pos;
  f_color = color;
}
)code" }
        , {GL_FRAGMENT_SHADER, R"code(
#version 330 core

in vec3 f_color;

out vec4 out_color;

void main()
{
  out_color = vec4(f_color, 1.);
}
)code" }
    }
  ));

  _vertex_array.upload();
  _buffers.upload();

  std::vector<glm::vec3> vertices;
  std::vector<std::uint16_t> indices;

  glm::vec3 basepos1[cnum], basepos2[cnum];

  for (int h = 0; h < hseg; h++)
  {
    for (int i = 0; i < cnum; ++i)
    {
      basepos1[i] = basepos2[i] = glm::vec3(glm::cos(math::radians(angles[i])._) * rad, glm::sin(math::radians(angles[i])._)*rad, 0);

      math::rotate(0, 0, &basepos1[i].x, &basepos1[i].z, math::radians(glm::pi<float>() *2.0f / hseg * h));
      math::rotate(0, 0, &basepos2[i].x, &basepos2[i].z, math::radians(glm::pi<float>() *2.0f / hseg * (h + 1)));
    }

    for (int v = 0; v < cnum - 1; v++)
    {
      int start = static_cast<int>(vertices.size());

      vertices.push_back(basepos2[v]);
      vertices.push_back(basepos1[v]);
      vertices.push_back(basepos1[v + 1]);
      vertices.push_back(basepos2[v + 1]);

      indices.push_back(start+0);
      indices.push_back(start+1);
      indices.push_back(start+2);

      indices.push_back(start+2);
      indices.push_back(start+3);
      indices.push_back(start+0);
    }
  }

  gl.bufferData<GL_ARRAY_BUFFER, glm::vec3>(_vertices_vbo, vertices, GL_STATIC_DRAW);
  gl.bufferData<GL_ELEMENT_ARRAY_BUFFER, std::uint16_t>(_indices_vbo, indices, GL_STATIC_DRAW);

  _indices_count = static_cast<int>(indices.size());

  _uploaded = true;
  _need_vao_update = true;
}

void Skies::update_vao(OpenGL::Scoped::use_program& shader)
{
  OpenGL::Scoped::index_buffer_manual_binder indices_binder (_indices_vbo);

  {
    OpenGL::Scoped::vao_binder const _ (_vao);

    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> vertices_buffer (_vertices_vbo);
    shader.attrib("position", 3, GL_FLOAT, GL_FALSE, 0, 0);

    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> colors_buffer (_colors_vbo);
    shader.attrib("color", 3, GL_FLOAT, GL_FALSE, 0, 0);

    indices_binder.bind();
  }

  _need_vao_update = false;
}

void Skies::update_color_buffer()
{
  std::vector<glm::vec3> colors;

  for (int h = 0; h < hseg; h++)
  {
    for (int v = 0; v < cnum - 1; v++)
    {
      colors.push_back(color_set[skycolors[v]]);
      colors.push_back(color_set[skycolors[v]]);
      colors.push_back(color_set[skycolors[v + 1]]);
      colors.push_back(color_set[skycolors[v + 1]]);
    }
  }

  gl.bufferData<GL_ARRAY_BUFFER, glm::vec3>(_colors_vbo, colors, GL_STATIC_DRAW);

  _need_vao_update = true;
}


void OutdoorLightStats::interpolate(OutdoorLightStats *a, OutdoorLightStats *b, float r)
{
  static constexpr unsigned DayNight_SecondsPerDay = 86400;

  float progressDayAndNight = r / DayNight_SecondsPerDay;

  float phiValue = 0;
  const float thetaValue = 3.926991f;
  const float phiTable[4] =
    {
      2.2165682f,
      1.9198623f,
      2.2165682f,
      1.9198623f
    };

  unsigned currentPhiIndex = static_cast<unsigned>(progressDayAndNight / 0.25f);
  if (currentPhiIndex > 3)
    currentPhiIndex = 3;
  unsigned nextPhiIndex = (currentPhiIndex < 3) ? currentPhiIndex + 1 : 0;

  // Lerp between the current value of phi and the next value of phi
  {
    float transitionProgress = (progressDayAndNight / 0.25f) - currentPhiIndex;

    float currentPhiValue = phiTable[currentPhiIndex];
    float nextPhiValue = phiTable[nextPhiIndex];

    phiValue = glm::mix(currentPhiValue, nextPhiValue, transitionProgress);
  }

  // Convert from Spherical Position to Cartesian coordinates
  float sinPhi = glm::sin(phiValue);
  float cosPhi = glm::cos(phiValue);

  float sinTheta = glm::sin(thetaValue);
  float cosTheta = glm::cos(thetaValue);

  dayDir.x = sinPhi * cosTheta;
  dayDir.y = sinPhi * sinTheta;
  dayDir.z = cosPhi;

  float ir = 1.0f - progressDayAndNight;
  nightIntensity = a->nightIntensity * ir + b->nightIntensity * progressDayAndNight;
}

OutdoorLighting::OutdoorLighting()
{

  static constexpr std::array<int, 24> night_hours =
    {1, 1, 1, 1, 1, 1,
     0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 1, 1};

  for (int i = 0; i < 24; ++i)
  {
    OutdoorLightStats ols;
    ols.nightIntensity = night_hours[i];
    lightStats.push_back(ols);
  }
}

OutdoorLightStats OutdoorLighting::getLightStats(int time)
{
  // ASSUME: only 24 light info records, one for each whole hour
  //! \todo  generalize this if the data file changes in the future

  int normalized_time ((static_cast<int>(time) % DAY_DURATION) / 2);

  static constexpr unsigned DayNight_SecondsPerDay = 86400;

  // time is in half-minutes (0..DAY_DURATION); each step is 30 seconds of in-game day.
  long progressDayAndNight = (static_cast<long>(time) % DAY_DURATION) * 30L;

  while (progressDayAndNight < 0 || progressDayAndNight > DayNight_SecondsPerDay)
  {
    if (progressDayAndNight > DayNight_SecondsPerDay)
      progressDayAndNight -= DayNight_SecondsPerDay;

    if (progressDayAndNight < 0)
      progressDayAndNight += DayNight_SecondsPerDay;
  }

  OutdoorLightStats out;

  OutdoorLightStats *a, *b;
  int ta = normalized_time / 60;
  int tb = (ta + 1) % 24;

  a = &lightStats[ta];
  b = &lightStats[tb];

  out.interpolate(a, b, progressDayAndNight);

  return out;
}

bool Sky::operator<(const Sky& s) const
{
  if (global) return false;
  else if (s.global) return true;
  else return r2 < s.r2;
}

bool Sky::selected() const
{
  return _selected;
}

void Sky::save_to_dbc()
{
  // Save Light.dbc record
  // find new empty ID : gLightDB.getEmptyRecordID(); .prob do it when creating new light instead.
  try 
  {
    // assuming a new unused id is already set with is_new_record
    DBCFile::Record lightDbRecord = is_new_record ? gLightDB.addRecord(Id) : gLightDB.getByID(Id);

    if (is_new_record)
        lightDbRecord.write(LightDB::Map, mapId);

    lightDbRecord.write(LightDB::PositionX, pos.x * skymul);
    lightDbRecord.write(LightDB::PositionY, pos.y * skymul);
    lightDbRecord.write(LightDB::PositionZ, pos.z * skymul);
    lightDbRecord.write(LightDB::RadiusInner, r1 * skymul);
    lightDbRecord.write(LightDB::RadiusOuter,r2 * skymul);

    bool save_param_dbc = false;
    bool save_colors_dbc = false;
    bool save_floats_dbc = false;
    bool save_skybox_dbc = false;

    for (int param_id = 0; param_id < NUM_SkyFloatParamsNames; param_id++)
    {
      auto param_opt = getCurrentParam();
      if (!param_opt.has_value())
        continue;

      SkyParam* sky_param = param_opt.value();
      if (sky_param == nullptr)
        continue;

      assert(sky_param->Id > 0);

      // This is weird. Id assignation for new records need to be done either now or before.
      // int lightParam_dbc_id = sky_param->_is_new_param_record ? gLightParamsDB.getEmptyRecordID() 
      //                                                         : sky_param->Id;
      // assuming Id was properly set
      int lightParam_dbc_id = sky_param->Id;

      lightDbRecord.write(LightDB::DataIDs + param_id, lightParam_dbc_id);

      if (lightParam_dbc_id == 0)
      {
        continue;
      }

      // save lightparams.dbc
      if (sky_param->_need_save || sky_param->_is_new_param_record)
      {
          save_param_dbc = true;
          try
                {
                    DBCFile::Record light_param = sky_param->_is_new_param_record ? gLightParamsDB.addRecord(lightParam_dbc_id)
                        : gLightParamsDB.getByID(lightParam_dbc_id);

                    light_param.write(LightParamsDB::highlightSky, int(sky_param->highlight_sky()));
                    light_param.write(LightParamsDB::water_shallow_alpha, sky_param->river_shallow_alpha());
                    light_param.write(LightParamsDB::water_deep_alpha, sky_param->river_deep_alpha());
                    light_param.write(LightParamsDB::ocean_shallow_alpha, sky_param->ocean_shallow_alpha());
                    light_param.write(LightParamsDB::ocean_deep_alpha, sky_param->ocean_deep_alpha());
                    light_param.write(LightParamsDB::glow, sky_param->glow());

                    if (sky_param->skybox.has_value()) // TODO skybox dbc
                    {
                        // try to find an existing record with those params
                        bool exists = false;
                        for (DBCFile::Iterator i = gLightSkyboxDB.begin(); i != gLightSkyboxDB.end(); ++i)
                        {
                            if (i->getString(LightSkyboxDB::filename) == sky_param->skybox.value().model->file_key().filepath()
                                && i->getInt(LightSkyboxDB::flags) == sky_param->skyboxFlags)
                            {
                                int id = i->getInt(LightSkyboxDB::ID);
                                light_param.write(LightParamsDB::skybox, id);
                                exists = true;
                                break;
                            }
                        }

                        if (!exists) // doesn't exist, create a new record
                        {
                          int new_skybox_dbc_id = gLightSkyboxDB.getEmptyRecordID();
                          DBCFile::Record rec = gLightSkyboxDB.addRecord(new_skybox_dbc_id);
                          rec.writeString(LightSkyboxDB::filename, sky_param->skybox.value().model->file_key().filepath());
                          rec.write(LightSkyboxDB::flags, sky_param->skyboxFlags);

                          gLightSkyboxDB.save();
                          
                          light_param.write(LightParamsDB::skybox, new_skybox_dbc_id);
                        }
                    }
                    else
                        light_param.write(LightParamsDB::skybox, 0);
                }
          catch (DBCFile::NotFound)
          {
              assert(false);
              LogError << "When trying to get the lightparams for the entry " << lightParam_dbc_id << " in LightParams.dbc" << std::endl;

              // failsafe, don't point to new id that couldn't be created
              if (sky_param->_is_new_param_record)
                  lightDbRecord.write(LightDB::DataIDs + param_id, 0);
          }
      }

      // save LightIntBand.dbc
      if (sky_param->_colors_need_save || sky_param->_is_new_param_record)
      {
        save_colors_dbc = true;
        int light_int_start = (lightParam_dbc_id * NUM_SkyColorNames) - (NUM_SkyColorNames - 1);

        for (int i = 0; i < NUM_SkyColorNames; ++i)
        {
          try
          {
            DBCFile::Record rec = sky_param->_is_new_param_record ? gLightIntBandDB.addRecord(light_int_start + i) 
                                                                  : gLightIntBandDB.getByID(light_int_start + i);

            int entries = static_cast<int>(sky_param->colorRows[i].size());

            rec.write(LightIntBandDB::Entries, entries); // nb of entries

            // write all entries to cleanup data
            for (int l = 0; l < 16; l++)
            {
              if (l >= entries)
              {
                rec.write(LightIntBandDB::Times + l, 0);
                rec.write(LightIntBandDB::Values + l, 0);
              }
              else
              {
                rec.write(LightIntBandDB::Times + l, sky_param->colorRows[i][l].time);
                
                int rebuilt_color_int = static_cast<int>(sky_param->colorRows[i][l].color.z * 255.0f)
                    + (static_cast<int>(sky_param->colorRows[i][l].color.y * 255.0f) << 8)
                    + (static_cast<int>(sky_param->colorRows[i][l].color.x * 255.0f) << 16);
                rec.write(LightIntBandDB::Values + l, rebuilt_color_int);
              }
            }
            // sky_param->_colors_need_save = false;
          }
          catch (...)
          {
              assert(false);
              LogError << "When trying to save sky colors, sky id : " << lightDbRecord.getInt(LightDB::ID) << std::endl;
          }
        }
      }

      // save LightFloatBand.dbc
      if (sky_param->_floats_need_save || sky_param->_is_new_param_record)
      {
        save_floats_dbc = true;
        int light_float_start = (lightParam_dbc_id * NUM_SkyFloatParamsNames) - (NUM_SkyFloatParamsNames - 1);

        for (int i = 0; i < NUM_SkyFloatParamsNames; ++i)
        {
          try
          {
            DBCFile::Record rec = sky_param->_is_new_param_record ? gLightFloatBandDB.addRecord(light_float_start + i) 
                                                                  : gLightFloatBandDB.getByID(light_float_start + i);
            int entries = static_cast<int>(sky_param->floatParams[i].size());

            rec.write(LightFloatBandDB::Entries, entries); // nb of entries

            // write all entries to cleanup data
            for (int l = 0; l < 16; l++)
            {
              if (l >= entries)
              {
                rec.write(LightFloatBandDB::Times + l, 0);
                rec.write(LightFloatBandDB::Values + l, 0.0f);
              }
              else
              {
                rec.write(LightFloatBandDB::Times + l, sky_param->floatParams[i][l].time);
                rec.write(LightFloatBandDB::Values + l, sky_param->floatParams[i][l].value);
              }
            }
            // sky_param->_floats_need_save = false;
          }
          catch (...)
          {
            LogError << "Error when trying to save sky float params, sky id : " << lightDbRecord.getInt(LightDB::ID) << std::endl;
          }
        }
      }

      // sky_param->_need_save = false;
      sky_param->_is_new_param_record = false;
    }

    gLightDB.save();
    if (save_colors_dbc)
        gLightIntBandDB.save();
    if (save_colors_dbc)
        gLightFloatBandDB.save();
    if (save_param_dbc)
        gLightParamsDB.save();

    is_new_record = false;
  }
  catch (DBCFile::AlreadyExists)
  {
    LogError << "DBCFile::AlreadyExists When trying to add light.dbc record for the entry " << Id << std::endl;
    assert(false);
  }
  catch (DBCFile::NotFound)
  {
    LogError << "DBCFile::NotFound When trying to add light.dbc record for the entry " << Id << std::endl;
    assert(false);
  }
  catch (...)
  {
    LogError << "Unknown exception When trying to add light.dbc record for the entry " << Id << std::endl;
    assert(false);
  }

}
