// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <ClientFile.hpp>
#include <math/frustum.hpp>
#include <math/ray.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/Log.h> // LogDebug
#include <noggit/Model.h>
#include <noggit/ModelInstance.h>
#include <noggit/ModelManager.h> // ModelManager
#include <noggit/TextureManager.h> // TextureManager, Texture
#include <noggit/WMO.h>
#include <noggit/wmo_liquid.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "Misc.h"


WMO::WMO(BlizzardArchive::Listfile::FileKey const& file_key, Noggit::NoggitRenderContext context)
  : AsyncObject(file_key)
  , _context(context)
  , _renderer(this)
{
}

WMO::~WMO()
{
}

void WMO::finishLoading ()
{
  auto client_data = Noggit::Application::NoggitApplication::instance()->clientData();
  BlizzardArchive::ClientFile f(_file_key.filepath(), client_data);
  if (f.isEof()) {
    LogError << "Error loading WMO \"" << _file_key.stringRepr() << "\"." << std::endl;
    return;
  }

  uint32_t fourcc = 0;
  uint32_t size = 0;

  float ff[3];

  char const* ddnames = nullptr;
  char const* groupnames = nullptr;

  uint32_t version = 0;
  CArgb ambient_color;
  unsigned int nTextures = 0, nGroups = 0, nP = 0, nLights = 0, nModels = 0, nDoodads = 0, nDoodadSets = 0;

  std::vector<char> texbuf;
  std::map<std::uint32_t, std::uint32_t> texture_fdid_to_inmem_index;
  std::map<std::uint32_t, std::uint32_t> modi_to_fdid;

  auto load_texture
    ( [&] (std::uint32_t ofs)
      {
        std::string texture_path = client_data->listfile()->getPath(ofs);
        const char* texture = texture_path.c_str();
        auto const mapping
          (texture_fdid_to_inmem_index.emplace(ofs, static_cast<std::uint32_t>(textures.size())));

        if (mapping.second)
        {
          textures.emplace_back(texture, _context);
        }
        return mapping.first->second;
      }
    );

  // Read chunks in any order
  while (!f.isEof()) {
    if (!f.read(&fourcc, 4)) break;
    if (!f.read(&size, 4)) break;

    LogDebug << "Reading WMO chunk at " << f.getPos() << ": " << fourcc << " ('" << fourcc_to_str(fourcc) << "'), (" << size << " bytes)" << std::endl;
    switch (fourcc) {
      case 'MVER': {
        f.read(&version, 4);
        f.seekRelative(size - 4);
        assert(version == 17);
        break;
      }
      case 'MOHD': {
        f.read(&nTextures, 4);
        f.read(&nGroups, 4);
        f.read(&nP, 4);
        f.read(&nLights, 4);
        f.read(&nModels, 4);
        f.read(&nDoodads, 4);
        f.read(&nDoodadSets, 4);
        f.read(&ambient_color, 4);
        f.read(&WmoId, 4);
        f.read(ff, 12);
        extents[0] = ::glm::vec3(ff[0], ff[1], ff[2]);
        f.read(ff, 12);
        extents[1] = ::glm::vec3(ff[0], ff[1], ff[2]);
        f.read(&flags, 2);
        f.seekRelative(2);

        ambient_light_color.x = static_cast<float>(ambient_color.r) / 255.f;
        ambient_light_color.y = static_cast<float>(ambient_color.g) / 255.f;
        ambient_light_color.z = static_cast<float>(ambient_color.b) / 255.f;
        ambient_light_color.w = static_cast<float>(ambient_color.a) / 255.f;
        break;
      }
      case 'MOTX': {
        texbuf.resize(size);
        f.read(texbuf.data(), texbuf.size());
        break;
      }
      case 'MOMT': {
        std::size_t const num_materials (size / 0x40);
        materials.resize(num_materials);
        for (size_t i(0); i < num_materials; ++i) {
          f.read(&materials[i], sizeof(WMOMaterial));
          uint32_t shader = materials[i].shader;
          bool use_second_texture = (shader == 6 || shader == 5 || shader == 3 || shader == 21 || shader == 23);
          materials[i].texture1 = load_texture(materials[i].texture_offset_1);
          if (use_second_texture) {
            materials[i].texture2 = load_texture(materials[i].texture_offset_2);
          }
        }
        break;
      }
      case 'MOGN': {
        groupnames = reinterpret_cast<char const*>(f.getPointer());
        f.seekRelative(size);
        break;
      }
      case 'MOGI': {
        groups.reserve(nGroups);
        for (unsigned int i = 0; i < nGroups; ++i) {
          groups.emplace_back(this, &f, i, groupnames);
        }
        break;
      }
      case 'MOSB': {
        if (size > 4) {
          std::string path = BlizzardArchive::ClientData::normalizeFilenameInternal(std::string(reinterpret_cast<char const*>(f.getPointer())));
          auto from = std::string("mdx");
          auto to = std::string("m2");
          size_t start_pos = 0;
          while ((start_pos = path.find(from, start_pos)) != std::string::npos) {
            path.replace(start_pos, from.length(), to);
            start_pos += to.length();
          }
          if (path.length()) {
            if (Noggit::Application::NoggitApplication::instance()->clientData()->exists(path)) {
              skybox = scoped_model_reference(path, _context);
            }
          }
        }
        f.seekRelative(size);
        break;
      }
      case 'GFID': // TODO: Group FileDataIDs
      case 'MOPV':
      case 'MOPT':
      case 'MOPR':
      case 'MOVV':
      case 'MOVB': {
        f.seekRelative(size);
        break;
      }
      case 'MOLT': {
        lights.reserve(nLights);
        for (size_t i = 0; i < nLights; ++i) {
          WMOLight l;
          l.init(&f);
          lights.push_back(l);
        }
        break;
      }
      case 'MODS': {
        doodadsets.reserve(nDoodadSets);
        for (size_t i = 0; i < nDoodadSets; ++i) {
          WMODoodadSet dds;
          f.read(&dds, 32);
          doodadsets.push_back(dds);
        }
        break;
      }
      case 'MODI': {
        for (size_t i = 0; i < size / 0x4; ++i) {
			uint32_t fdid;
            f.read(&fdid, 4);
            if (fdid != 0) {
                modi_to_fdid.emplace(i, fdid);
			}
        }
        break;
      }
      case 'MODD': {
        modelis.reserve(size / 0x28);
        for (size_t i = 0; i < size / 0x28; ++i) {
          struct {
            uint32_t name_offset : 24;
            uint32_t flag_AcceptProjTex : 1;
            uint32_t flag_0x2 : 1;
            uint32_t flag_0x4 : 1;
            uint32_t flag_0x8 : 1;
            uint32_t flags_unused : 4;
          } x;
          size_t after_entry(f.getPos() + 0x28);
          f.read(&x, sizeof(x));
          model_nearest_light_vector.emplace_back();
          f.seek(after_entry);
        }
        break;
      }
      case 'MFOG': {
        int nfogs = size / 0x30;
        fogs.reserve(nfogs);
        for (size_t i = 0; i < nfogs; ++i) {
          WMOFog fog;
          fog.init(&f);
          fogs.push_back(std::move(fog));
        }
        break;
      }
      default: {
        // Unknown chunk, skip
        LogDebug << "Unknown WMO chunk: " << fourcc << " ('" << fourcc_to_str(fourcc) << "'), (" << size << " bytes)" << std::endl;
        f.seekRelative(size);
        break;
      }
    }
  }

  for (auto& doodadFDID : modi_to_fdid) {
    if(doodadFDID.second == 0) {
        LogError << "WMO " << _file_key.stringRepr() << " has a doodad with FDID 0, this is not allowed." << std::endl;
        continue;
    }
 
	LogDebug << "Loading model for FDID " << doodadFDID.second << std::endl;
    BlizzardArchive::Listfile::FileKey doodad_key;
	doodad_key.setFileDataID(doodadFDID.second);
    doodad_key.setFilepath(client_data->listfile()->getPath(doodadFDID.second));
    BlizzardArchive::ClientFile doodadFile(doodad_key, client_data);
    modelis.emplace_back(doodad_key, &doodadFile, _context);
  }

  // Load groups after all chunks are processed
  for (auto& group : groups)
    group.load();

  finished = true;
  _state_changed.notify_all();
}

void WMO::waitForChildrenLoaded()
{
  for (auto& tex : textures)
  {
    tex.get()->wait_until_loaded();
  }

  for (auto& doodad : modelis)
  {
    doodad.model->wait_until_loaded();
    doodad.model->waitForChildrenLoaded();
  }
}

std::vector<float> WMO::intersect (math::ray const& ray, bool do_exterior) const
{
  std::vector<float> results;

  if (!finishedLoading() || loading_failed())
  {
    return results;
  }

  for (auto& group : groups)
  {
    if (!do_exterior && !group.is_indoor())
          continue;

    group.intersect (ray, &results);
  }

  if (!do_exterior && results.size())
  {
      // dirty way to find the furthest face and ignore invisible faces, cleaner way would be to do a direction check on faces
      // float max = *std::max_element(std::begin(results), std::end(results));
      // results.clear();
      // results.push_back(max);

      // other way, ignore the closest intersect, works well
      if (results.size() > 1)
      {
        auto it = std::min_element(results.begin(), results.end());
        results.erase(it);
      }
  }

  return results;
}

std::map<uint32_t, std::vector<wmo_doodad_instance>> WMO::doodads_per_group(uint16_t doodadset) const
{
  std::map<uint32_t, std::vector<wmo_doodad_instance>> doodads;

  if (doodadset >= doodadsets.size())
  {
    LogError << "Invalid doodadset for instance of wmo " << _file_key.stringRepr() << std::endl;
    return doodads;
  }

  auto const& dset = doodadsets[doodadset];
  uint32_t start = dset.start, end = start + dset.size;

  for (int i = 0; i < groups.size(); ++i)
  {
    for (uint16_t ref : groups[i].doodad_ref())
    {
      if (ref >= start && ref < end)
      {
        doodads[i].push_back(modelis[ref]);
      }
    }
  }

  return doodads;
}

[[nodiscard]]
bool WMO::is_hidden() const
{
  return _hidden;
}

void WMO::toggle_visibility()
{
  _hidden = !_hidden;
}

void WMO::show()
{
  _hidden = false;
}

void WMO::hide()
{
  _hidden = true;
}

[[nodiscard]]
bool WMO::is_required_when_saving() const
{
  return true;
}

[[nodiscard]]
Noggit::Rendering::WMORender* WMO::renderer()
{
  return &_renderer;
}

void WMOLight::init(BlizzardArchive::ClientFile* f)
{
  char type[4];
  f->read(&type, 4);
  f->read(&color, 4);
  f->read(&pos, 12);
  f->read(&intensity, 4);
  f->read(unk, 4 * 5);
  f->read(&r, 4);

  pos = glm::vec3(pos.x, pos.z, -pos.y);

  // rgb? bgr? hm
  float fa = ((color & 0xff000000) >> 24) / 255.0f;
  float fr = ((color & 0x00ff0000) >> 16) / 255.0f;
  float fg = ((color & 0x0000ff00) >> 8) / 255.0f;
  float fb = ((color & 0x000000ff)) / 255.0f;

  fcolor = glm::vec4(fr, fg, fb, fa);
  fcolor *= intensity;
  fcolor.w = 1.0f;

  /*
  // light logging
  gLog("Light %08x @ (%4.2f,%4.2f,%4.2f)\t %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f, %4.2f\t(%d,%d,%d,%d)\n",
  color, pos.x, pos.y, pos.z, intensity,
  unk[0], unk[1], unk[2], unk[3], unk[4], r,
  type[0], type[1], type[2], type[3]);
  */
}

void WMOLight::setup(GLint)
{
  // not used right now -_-
}

void WMOLight::setupOnce(GLint, glm::vec3, glm::vec3)
{
  //glm::vec4position(dir, 0);
  //glm::vec4position(0,1,0,0);

  //glm::vec4ambient = glm::vec4(light_color * 0.3f, 1);
  //glm::vec4diffuse = glm::vec4(light_color, 1);


  //gl.enable(light);
}



WMOGroup::WMOGroup(WMO *_wmo, BlizzardArchive::ClientFile* f, int _num, char const* names)
  : wmo(_wmo)
  , num(_num)
  , _renderer(this)
{
  // extract group info from f
  std::uint32_t flags; // not used, the flags are in the group header
  f->read(&flags, 4);
  float ff[3];
  f->read(ff, 12);
  VertexBoxMax = glm::vec3(ff[0], ff[1], ff[2]);
  f->read(ff, 12);
  VertexBoxMin = glm::vec3(ff[0], ff[1], ff[2]);
  int nameOfs;
  f->read(&nameOfs, 4);

  //! \todo  get proper name from group header and/or dbc?
  if (nameOfs > 0) {
    name = std::string(names + nameOfs);
  }
  else name = "(no name)";
}

WMOGroup::WMOGroup(WMOGroup const& other)
  : BoundingBoxMin(other.BoundingBoxMin)
  , BoundingBoxMax(other.BoundingBoxMax)
  , VertexBoxMin(other.VertexBoxMin)
  , VertexBoxMax(other.VertexBoxMax)
  , use_outdoor_lights(other.use_outdoor_lights)
  , name(other.name)
  , wmo(other.wmo)
  , header(other.header)
  , center(other.center)
  , rad(other.rad)
  , num(other.num)
  , fog(other.fog)
  , _doodad_ref(other._doodad_ref)
  , _batches(other._batches)
  , _vertices(other._vertices)
  , _normals(other._normals)
  , _texcoords(other._texcoords)
  , _texcoords_2(other._texcoords_2)
  , _vertex_colors(other._vertex_colors)
  , _indices(other._indices)
  , _renderer(this)
{
  if (other.lq)
  {
    lq = std::make_unique<wmo_liquid>(*other.lq.get());
  }
}

namespace
{
  glm::vec4 colorFromInt(unsigned int col)
  {
    GLubyte r, g, b, a;
    a = (col & 0xFF000000) >> 24;
    r = (col & 0x00FF0000) >> 16;
    g = (col & 0x0000FF00) >> 8;
    b = (col & 0x000000FF);
    return glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
  }
}

void WMOGroup::load()
{
  // open group file
  std::stringstream curNum;
  curNum << "_" << std::setw (3) << std::setfill ('0') << num;

  std::string fname = wmo->file_key().filepath();
  fname.insert (fname.find (".wmo"), curNum.str ());

  BlizzardArchive::ClientFile f(fname, Noggit::Application::NoggitApplication::instance()->clientData());
  if (f.isEof()) {
    LogError << "Error loading WMO \"" << fname << "\"." << std::endl;
    return;
  }

  uint32_t fourcc = 0;
  uint32_t size = 0;
  uint32_t version = 0;
  bool have_mver = false, have_mogp = false, have_header = false;
  bool done = false;

  // Clear/prepare containers
  _indices.clear();
  _vertices.clear();
  _normals.clear();
  _texcoords.clear();
  _texcoords_2.clear();
  _vertex_colors.clear();
  _batches.clear();
  _doodad_ref.clear();
  lq.reset();

  // Read chunks in any order
  while (!f.isEof() && !done) {
    if (!f.read(&fourcc, 4)) break;
    if (!f.read(&size, 4)) break;

     LogDebug << "Group chunk: " << fourcc << " ('" << fourcc_to_str(fourcc) << "') size=" << size << std::endl;

    switch (fourcc) {
      case 'MVER': {
        f.read(&version, 4);
        f.seekRelative(size - 4);
        assert(version == 17);
        have_mver = true;
        break;
      }
      case 'MOGP': {
        f.read(&header, sizeof(wmo_group_header));
        have_mogp = true;
        unsigned fog_index = header.fogs[0];
        if (fog_index >= wmo->fogs.size()) {
          fog = -1;
        } else {
          WMOFog& wf = wmo->fogs[fog_index];
          if (wf.r2 <= 0) fog = -1;
          else fog = header.fogs[0];
        }
        BoundingBoxMin = ::glm::vec3(header.box1[0], header.box1[2], -header.box1[1]);
        BoundingBoxMax = ::glm::vec3(header.box2[0], header.box2[2], -header.box2[1]);
        break;
      }
      case 'MOPY': {
        f.seekRelative(size);
        break;
      }
      case 'MOVI': {
        _indices.resize(size / sizeof(uint16_t));
        f.read(_indices.data(), size);
        break;
      }
      case 'MOVT': {
        // let's hope it's padded to 12 bytes, not 16...
        ::glm::vec3 const* vertices = reinterpret_cast< ::glm::vec3 const*>(f.getPointer());
        _vertices.resize(size / sizeof(::glm::vec3));
        VertexBoxMin = ::glm::vec3(std::numeric_limits<float>::max());
        VertexBoxMax = ::glm::vec3(std::numeric_limits<float>::lowest());
        rad = 0;
        for (size_t i = 0; i < _vertices.size(); ++i) {
          _vertices[i] = glm::vec3(vertices[i].x, vertices[i].z, -vertices[i].y);
          ::glm::vec3& v = _vertices[i];
          if (v.x < VertexBoxMin.x) VertexBoxMin.x = v.x;
          if (v.y < VertexBoxMin.y) VertexBoxMin.y = v.y;
          if (v.z < VertexBoxMin.z) VertexBoxMin.z = v.z;
          if (v.x > VertexBoxMax.x) VertexBoxMax.x = v.x;
          if (v.y > VertexBoxMax.y) VertexBoxMax.y = v.y;
          if (v.z > VertexBoxMax.z) VertexBoxMax.z = v.z;
        }
        center = (VertexBoxMax + VertexBoxMin) * 0.5f;
        rad = glm::distance(center, VertexBoxMax);
        f.seekRelative(size);
        break;
      }
      case 'MONR': {
        _normals.resize(size / sizeof(::glm::vec3));
        f.read(_normals.data(), size);
        for (auto& n : _normals) {
          n = {n.x, n.z, -n.y};
        }
        break;
      }
      case 'MOTV': {
        if (_texcoords.empty()) {
          _texcoords.resize(size / sizeof(glm::vec2));
          f.read(_texcoords.data(), size);
        } else {
          _texcoords_2.resize(size / sizeof(glm::vec2));
          f.read(_texcoords_2.data(), size);
        }
        break;
      }
      case 'MOBA': {
        _batches.resize(size / sizeof(wmo_batch));
        f.read(_batches.data(), size);
        _renderer.initRenderBatches();
        break;
      }
      case 'MOLR': {
        f.seekRelative(size);
        break;
      }
      case 'MODR': {
        _doodad_ref.resize(size / sizeof(int16_t));
        f.read(_doodad_ref.data(), size);
        break;
      }
      case 'MOBN': {
        f.seekRelative(size);
        break;
      }
      case 'MOBR': {
        f.seekRelative(size);
        break;
      }
      case 'MPBV':
      case 'MPBP':
      case 'MPBI':
      case 'MPBG': {
        f.seekRelative(size);
        break;
      }
      case 'MOCV': {
        if (_vertex_colors.empty()) {
          load_mocv(f, size);
        } else {
          std::vector<CImVector> mocv_2(size / sizeof(CImVector));
          f.read(mocv_2.data(), size);
          for (int i = 0; i < mocv_2.size(); ++i) {
            float alpha = static_cast<float>(mocv_2[i].a) / 255.f;
            if (header.flags.has_vertex_color) {
              _vertex_colors[i].w = alpha;
            } else {
              _vertex_colors.emplace_back(0.f, 0.f, 0.f, alpha);
            }
          }
        }
        break;
      }
      case 'MLIQ': {
        WMOLiquidHeader hlq;
        f.read(&hlq, 0x1E);
        lq = std::make_unique<wmo_liquid>(
          &f,
          hlq,
          header.group_liquid,
          (bool)wmo->flags.use_liquid_type_dbc_id,
          (bool)header.flags.ocean
        );
        f.seekRelative(size - 0x1E);
        break;
      }
      case 'MORI': {
        f.seekRelative(size);
        break;
      }
      case 'MORB': {
        f.seekRelative(size);
        break;
      }
      default: {
        LogDebug << "Unknown Group WMO chunk: " << fourcc << " ('" << fourcc_to_str(fourcc) << "', " << size << " bytes)" << std::endl;
        f.seekRelative(size);
        break;
      }
    }

  }

  //dl_light = 0;
  // "real" lighting?
  if (header.flags.indoor && header.flags.has_vertex_color)
  {
    ::glm::vec3 dirmin(1, 1, 1);
    float lenmin;

    for (auto doodad : _doodad_ref)
    {
      if (doodad >= wmo->modelis.size())
      {
          continue;
          LogError << "The WMO file currently loaded is potentially corrupt. Non-existing doodad referenced." << std::endl;
      }

      lenmin = 999999.0f * 999999.0f;
      ModelInstance& mi = wmo->modelis[doodad];
      for (unsigned int j = 0; j < wmo->lights.size(); j++)
      {
        WMOLight& l = wmo->lights[j];
        ::glm::vec3 dir = l.pos - mi.pos;

        float ll = glm::length(dir) * glm::length(dir);
        if (ll < lenmin)
        {
          lenmin = ll;
          dirmin = dir;
        }
      }
      wmo->model_nearest_light_vector[doodad] = dirmin;
    }

    use_outdoor_lights = false;
  }
  else
  {
    use_outdoor_lights = true;
  }
}

void WMOGroup::load_mocv(BlizzardArchive::ClientFile& f, uint32_t size)
{
  uint32_t const* colors = reinterpret_cast<uint32_t const*> (f.getPointer());
  _vertex_colors.resize(size / sizeof(uint32_t));

  for (size_t i(0); i < size / sizeof(uint32_t); ++i)
  {
    _vertex_colors[i] = colorFromInt(colors[i]);
  }

  if (wmo->flags.do_not_fix_vertex_color_alpha)
  {
    int interior_batchs_start = 0;

    if (header.transparency_batches_count > 0)
    {
      interior_batchs_start = _batches[header.transparency_batches_count - 1].vertex_end + 1;
    }

    for (int n = interior_batchs_start; n < _vertex_colors.size(); ++n)
    {
      _vertex_colors[n].w = header.flags.exterior ? 1.f : 0.f;
    }
  }
  else
  {
    fix_vertex_color_alpha();
  }

  // there's no read so this is required
  f.seekRelative(size);
}

void WMOGroup::fix_vertex_color_alpha()
{
  int interior_batchs_start = 0;

  if (header.transparency_batches_count > 0)
  {
    interior_batchs_start = _batches[header.transparency_batches_count - 1].vertex_end + 1;
  }

  glm::vec4 wmo_ambient_color;

  if (wmo->flags.use_unified_render_path)
  {
    wmo_ambient_color = {0.f, 0.f, 0.f, 0.f};
  }
  else
  {
    wmo_ambient_color = wmo->ambient_light_color;
    // w is not used, set it to 0 to avoid changing the vertex color alpha
    wmo_ambient_color.w = 0.f;
  }

  for (int i = 0; i < _vertex_colors.size(); ++i)
  {
    auto& color = _vertex_colors[i];
    float r = color.x;
    float g = color.y;
    float b = color.z;
    float a = color.w;

    // I removed the color = color/2 because it's just multiplied by 2 in the shader afterward in blizzard's code
    if (i >= interior_batchs_start)
    {
      r += ((r * a / 64.f) - wmo_ambient_color.x);
      g += ((g * a / 64.f) - wmo_ambient_color.y);
      r += ((b * a / 64.f) - wmo_ambient_color.z);
    }
    else
    {
      r -= wmo_ambient_color.x;
      g -= wmo_ambient_color.y;
      b -= wmo_ambient_color.z;

      r = (r * (1.f - a));
      g = (g * (1.f - a));
      b = (b * (1.f - a));
    }

    color.x = std::min(255.f, std::max(0.f, r));
    color.y = std::min(255.f, std::max(0.f, g));
    color.z = std::min(255.f, std::max(0.f, b));
    color.w = 1.f; // default value used in the shader so I simplified it here,
                   // it can be overriden by the 2nd mocv chunk
  }
}

bool WMOGroup::is_visible( glm::mat4x4 const& transform
                         , math::frustum const& frustum
                         , float const& cull_distance
                         , glm::vec3 const& camera
                         , display_mode display
                         ) const
{
   // glm::vec3 pos = transform * glm::vec4(center, 0);
   // 
    // glm::vec3 pos = transform[3] * glm::vec4(center, 1.0f);
    // glm::vec3 test_pos = transform[3] + glm::vec4(center, 0);
    // glm::vec3 test_pos2 = transform[3];


    // TODO center is just the center of the group vertex box, and rad is distance from box max to center.
    // to do operation on group we need to get its true position
    // 
    // adjusted group transform mat = 
    glm::vec3 pos = transform *  glm::vec4(center, 1.0f);

  float dist = display == display_mode::in_3D
    ? glm::distance(pos, camera) - rad
    : std::abs(pos.y - camera.y) - rad;

  // Camera is within the bounding sphere, always draw
  if (dist < 0)
      return true;

  float cull = cull_distance;

  if (dist > cull_distance)
      return false;


  if (!frustum.intersects(pos + BoundingBoxMin, pos + BoundingBoxMax))
  {
    return false;
  }

  return true;
}

[[nodiscard]]
std::vector<uint16_t> WMOGroup::doodad_ref() const
{
  return _doodad_ref;
}

[[nodiscard]]
bool WMOGroup::has_skybox() const
{
  return header.flags.skybox;
}

[[nodiscard]]
bool WMOGroup::is_indoor() const
{
  return header.flags.indoor;
}

[[nodiscard]]
Noggit::Rendering::WMOGroupRender* WMOGroup::renderer()
{
  return &_renderer;
}

void WMOGroup::intersect (math::ray const& ray, std::vector<float>* results) const
{
  if (!ray.intersect_bounds (VertexBoxMin, VertexBoxMax))
  {
    return;
  }

  //! \todo Also allow clicking on doodads and liquids.
  for (auto&& batch : _batches)
  {
    for (size_t i (batch.index_start); i < batch.index_start + batch.index_count; i += 3)
    {
      // TODO : only intersect visible triangles
      // TODO : option to only check collision
      if ( auto&& distance
         = ray.intersect_triangle ( _vertices[_indices[i + 0]]
                                  , _vertices[_indices[i + 1]]
                                  , _vertices[_indices[i + 2]]
                                  )
         )
      {
        results->emplace_back (*distance);
      }
    }
  }
}

/*
void WMOGroup::drawLiquid ( glm::mat4x4 const& transform
                          , liquid_render& render
                          , bool // draw_fog
                          , int animtime
                          )
{
  // draw liquid
  //! \todo  culling for liquid boundingbox or something
  if (lq) 
  { 
    gl.enable(GL_BLEND);
    gl.depthMask(GL_TRUE);

    lq->draw ( transform, render, animtime);

    gl.disable(GL_BLEND);
  }
}
*/

void WMOGroup::setupFog (bool draw_fog, std::function<void (bool)> setup_fog)
{
  if (use_outdoor_lights || fog == -1) {
    setup_fog (draw_fog);
  }
  else {
    wmo->fogs[fog].setup();
  }
}

void WMOFog::init(BlizzardArchive::ClientFile* f)
{
  f->read(this, 0x30);
  color = glm::vec4(((color1 & 0x00FF0000) >> 16) / 255.0f, ((color1 & 0x0000FF00) >> 8) / 255.0f,
    (color1 & 0x000000FF) / 255.0f, ((color1 & 0xFF000000) >> 24) / 255.0f);
  float temp;
  temp = pos.y;
  pos.y = pos.z;
  pos.z = -temp;
  fogstart = fogstart * fogend * 1.5f;
  fogend *= 1.5;
}

void WMOFog::setup()
{

}

decltype (WMOManager::_) WMOManager::_;

void WMOManager::report()
{
  std::string output = "Still in the WMO manager:\n";
  _.apply ( [&] (BlizzardArchive::Listfile::FileKey const& key, WMO const&)
            {
              output += " - " + key.stringRepr() + "\n";
            }
          );
  LogDebug << output;
}

void WMOManager::clear_hidden_wmos()
{
  _.apply ( [&] (BlizzardArchive::Listfile::FileKey const&, WMO& wmo)
            {
              wmo.show();
            }
          );
}

void WMOManager::unload_all(Noggit::NoggitRenderContext context)
{
    _.context_aware_apply(
        [&] (BlizzardArchive::Listfile::FileKey const&, WMO& wmo)
        {
            wmo.renderer()->unload();
        }
        , context
    );
}

bool wmo_triangle_material_info::isTransFace() const
{
  return flags.flag_0x01 && (flags.detail || flags.render);
}

bool wmo_triangle_material_info::isColor() const
{
  return !flags.collision;
}

bool wmo_triangle_material_info::isRenderFace() const
{
  return flags.render && !flags.detail;
}

bool wmo_triangle_material_info::isCollidable() const
{
  return flags.collision || isRenderFace();
}

bool wmo_triangle_material_info::isCollision() const
{
  return texture == 0xff;
}
