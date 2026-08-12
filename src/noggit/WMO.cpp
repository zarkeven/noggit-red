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
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
  // Corrupt / misaligned modern WMO chunks have produced multi-GB resize attempts
  // (bad allocation → every paintGL frame fails → map appears to unload).
  constexpr std::size_t kMaxWmoChunkBytes = 64u * 1024u * 1024u;

  template <typename T>
  void resize_wmo_chunk(std::vector<T>& out, std::uint32_t size, char const* chunk, char const* wmo_name)
  {
    if (size > kMaxWmoChunkBytes)
    {
      throw std::runtime_error(std::string(chunk) + " chunk too large (" + std::to_string(size)
                               + " bytes) in " + wmo_name);
    }
    if (sizeof(T) == 0 || (size % sizeof(T)) != 0)
    {
      throw std::runtime_error(std::string(chunk) + " chunk size not aligned to element in " + wmo_name);
    }
    out.resize(size / sizeof(T));
  }
}


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
  auto* client_data = Noggit::Application::NoggitApplication::instance()->clientData();
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
  std::map<std::uint32_t, std::uint32_t> texture_key_to_inmem_index;
  bool have_motx = false;

  auto load_texture
    ( [&] (std::uint32_t key) -> std::uint32_t
      {
        std::string texture_path;
        // wowlib: MOTX presence selects MOTX-offset mode; otherwise texture_* are FileDataIDs.
        if (have_motx)
        {
          if (key < texbuf.size() && texbuf[key])
            texture_path = &texbuf[key];
        }
        else if (key != 0)
        {
          texture_path = client_data->listfile()->getPath(key);
        }

        if (texture_path.empty())
        {
          texture_path = "textures/shanecube.blp";
        }

        auto const mapping
          (texture_key_to_inmem_index.emplace(key, static_cast<std::uint32_t>(textures.size())));

        if (mapping.second)
        {
          textures.emplace_back(texture_path, _context);
        }
        return mapping.first->second;
      }
    );

  // Read root chunks in any order (Legion+ inserts GFID/MODI and may omit MOTX/MODN).
  while (!f.isEof())
  {
    std::size_t const chunk_header_pos = f.getPos();
    if (!f.read(&fourcc, 4) || !f.read(&size, 4))
      break;

    std::size_t const payload_pos = f.getPos();
    std::size_t const chunk_end = payload_pos + size;
    if (chunk_end > f.getSize())
    {
      LogError << "WMO \"" << _file_key.stringRepr() << "\" chunk overruns file at "
               << chunk_header_pos << std::endl;
      break;
    }

    switch (fourcc)
    {
      case 'MVER':
      {
        f.read(&version, 4);
        if (version != 17)
        {
          LogError << "WMO \"" << _file_key.stringRepr() << "\" unexpected version " << version << std::endl;
        }
        break;
      }
      case 'MOHD':
      {
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
      case 'MOTX':
      {
        if (size > kMaxWmoChunkBytes)
          throw std::runtime_error("MOTX chunk too large in " + _file_key.stringRepr());
        texbuf.resize(size);
        f.read(texbuf.data(), texbuf.size());
        have_motx = !texbuf.empty();
        break;
      }
      case 'MOMT':
      {
        if (size > kMaxWmoChunkBytes || (size % 0x40) != 0)
          throw std::runtime_error("MOMT chunk invalid in " + _file_key.stringRepr());
        std::size_t const num_materials(size / 0x40);
        materials.resize(num_materials);
        for (size_t i(0); i < num_materials; ++i)
        {
          // Disk MOMT entry is exactly 0x40 (wowlib SMOMaterial). texture1/2 are runtime-only.
          // Defer load_texture until after the chunk loop so MOTX (any order) is known —
          // otherwise offsets are misread as FileDataIDs and materials look untextured.
          f.read(static_cast<WMOMaterialDisk*>(&materials[i]), sizeof(WMOMaterialDisk));
          materials[i].texture1 = 0;
          materials[i].texture2 = 0;
        }
        break;
      }
      case 'MOGN':
      {
        groupnames = reinterpret_cast<char const*>(f.getPointer());
        break;
      }
      case 'MOGI':
      {
        groups.reserve(nGroups);
        for (unsigned int i = 0; i < nGroups; ++i)
        {
          groups.emplace_back(this, &f, i, groupnames);
        }
        break;
      }
      case 'MOSB':
      {
        if (size > 4)
        {
          std::string path = BlizzardArchive::ClientData::normalizeFilenameInternal(
            std::string(reinterpret_cast<char const*>(f.getPointer())));
          auto from = std::string("mdx");
          auto to = std::string("m2");
          size_t start_pos = 0;
          while ((start_pos = path.find(from, start_pos)) != std::string::npos)
          {
            path.replace(start_pos, from.length(), to);
            start_pos += to.length();
          }
          if (!path.empty() && client_data->exists(path))
          {
            skybox = scoped_model_reference(path, _context);
          }
        }
        break;
      }
      case 'GFID':
      {
        // LOD0 group FileDataIDs occupy the first nGroups entries.
        if (size > kMaxWmoChunkBytes || (size % 4) != 0)
          throw std::runtime_error("GFID chunk invalid in " + _file_key.stringRepr());
        std::size_t const count = size / 4;
        group_file_data_ids.resize(count);
        f.read(group_file_data_ids.data(), size);
        break;
      }
      case 'MOPV':
      case 'MOPT':
      case 'MOPR':
      case 'MOVV':
      case 'MOVB':
        break;
      case 'MOLT':
      {
        lights.reserve(nLights);
        for (size_t i = 0; i < nLights; ++i)
        {
          WMOLight l;
          l.init(&f);
          lights.push_back(l);
        }
        break;
      }
      case 'MODS':
      {
        doodadsets.reserve(nDoodadSets);
        for (size_t i = 0; i < nDoodadSets; ++i)
        {
          WMODoodadSet dds;
          f.read(&dds, 32);
          doodadsets.push_back(dds);
        }
        break;
      }
      case 'MODN':
      {
        if (size)
        {
          ddnames = reinterpret_cast<char const*>(f.getPointer());
        }
        break;
      }
      case 'MODI':
      {
        if (size > kMaxWmoChunkBytes || (size % 4) != 0)
          throw std::runtime_error("MODI chunk invalid in " + _file_key.stringRepr());
        uses_modi_doodads = true;
        doodad_file_data_ids.resize(size / 4);
        f.read(doodad_file_data_ids.data(), size);
        break;
      }
      case 'MODD':
      {
        if (size > kMaxWmoChunkBytes || (size % 0x28) != 0)
          throw std::runtime_error("MODD chunk invalid in " + _file_key.stringRepr());
        modelis.reserve(size / 0x28);
        for (size_t i = 0; i < size / 0x28; ++i)
        {
          struct
          {
            uint32_t name_offset : 24;
            uint32_t flag_AcceptProjTex : 1;
            uint32_t flag_0x2 : 1;
            uint32_t flag_0x4 : 1;
            uint32_t flag_0x8 : 1;
            uint32_t flags_unused : 4;
          } x;

          size_t after_entry(f.getPos() + 0x28);
          f.read(&x, sizeof(x));

          BlizzardArchive::Listfile::FileKey doodad_key;
          if (uses_modi_doodads)
          {
            if (x.name_offset >= doodad_file_data_ids.size() || doodad_file_data_ids[x.name_offset] == 0)
            {
              f.seek(after_entry);
              continue;
            }
            std::uint32_t const fdid = doodad_file_data_ids[x.name_offset];
            doodad_key = BlizzardArchive::Listfile::FileKey(fdid);
            doodad_key.deduceOtherComponent(client_data->listfile());
          }
          else if (ddnames)
          {
            doodad_key = BlizzardArchive::Listfile::FileKey(ddnames + x.name_offset);
          }
          else
          {
            f.seek(after_entry);
            continue;
          }

          modelis.emplace_back(doodad_key, &f, _context);
          model_nearest_light_vector.emplace_back();
          f.seek(after_entry);
        }
        break;
      }
      case 'MFOG':
      {
        int nfogs = size / 0x30;
        fogs.reserve(nfogs);
        for (size_t i = 0; i < static_cast<size_t>(nfogs); ++i)
        {
          WMOFog fog;
          fog.init(&f);
          fogs.push_back(std::move(fog));
        }
        break;
      }
      default:
        break;
    }

    f.seek(chunk_end);
  }

  // Resolve MOMT textures after MOTX is known (wowlib: MOTX present → byte offsets;
  // MOTX absent → FileDataIDs). Chunk order is not guaranteed on Legion+.
  for (auto& mat : materials)
  {
    uint32_t const shader = mat.shader;
    if (shader >= 23)
    {
      std::uint32_t tex1_key = 0;
      std::uint32_t tex2_key = 0;
      wmo_resolve_shader23_texture_keys(mat, tex1_key, tex2_key);
      mat.texture1 = load_texture(tex1_key);
      mat.texture2 = load_texture(tex2_key != 0 ? tex2_key : tex1_key);
    }
    else
    {
      mat.texture1 = load_texture(mat.texture_offset_1);
      if (wmo_material_uses_second_texture(shader))
        mat.texture2 = load_texture(mat.texture_offset_2);
    }
  }

  for (auto& group : groups)
  {
    try
    {
      group.load();
    }
    catch (std::exception const& e)
    {
      LogError << "WMO group load failed for \"" << _file_key.stringRepr() << "\": " << e.what() << std::endl;
      throw;
    }
  }

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
      if (ref >= start && ref < end && ref < modelis.size())
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
  auto* client_data = Noggit::Application::NoggitApplication::instance()->clientData();

  BlizzardArchive::Listfile::FileKey group_key;
  std::string fname;

  // Prefer GFID FileDataID when present (required for FDID-only listfile entries).
  if (static_cast<std::size_t>(num) < wmo->group_file_data_ids.size()
      && wmo->group_file_data_ids[static_cast<std::size_t>(num)] != 0)
  {
    std::uint32_t const fdid = wmo->group_file_data_ids[static_cast<std::size_t>(num)];
    group_key = BlizzardArchive::Listfile::FileKey(fdid);
    group_key.deduceOtherComponent(client_data->listfile());
    fname = group_key.hasFilepath() ? group_key.filepath() : ("FileDataID:" + std::to_string(fdid));
  }
  else
  {
    std::stringstream curNum;
    curNum << "_" << std::setw(3) << std::setfill('0') << num;
    fname = wmo->file_key().filepath();
    auto const dot = fname.find(".wmo");
    if (dot == std::string::npos)
    {
      LogError << "Error loading WMO group for \"" << fname << "\" (no .wmo suffix)." << std::endl;
      return;
    }
    fname.insert(dot, curNum.str());
    group_key = BlizzardArchive::Listfile::FileKey(fname);
  }

  BlizzardArchive::ClientFile f(group_key, client_data);
  if (f.isEof())
  {
    LogError << "Error loading WMO \"" << fname << "\"." << std::endl;
    return;
  }

  uint32_t fourcc = 0;
  uint32_t size = 0;
  uint32_t version = 0;

  _indices.clear();
  _vertices.clear();
  _normals.clear();
  _texcoords.clear();
  _texcoords_2.clear();
  _vertex_colors.clear();
  _batches.clear();
  _doodad_ref.clear();
  lq.reset();

  // Modern groups may contain 3–4 MOTV chunks (mesh UVs + atlas tile UVs). Collect all
  // passes then pick primary/atlas sets — do not gate on MOGP.has_two_motv alone.
  std::vector<std::vector<glm::vec2>> motv_sets;

  // Group files: MOGP.size covers the rest of the file; subchunks follow the header
  // as siblings in the stream — do not seek past MOGP's declared size after the header.
  while (!f.isEof())
  {
    if (!f.read(&fourcc, 4) || !f.read(&size, 4))
      break;

    std::size_t const payload_pos = f.getPos();
    if (payload_pos + size > f.getSize())
    {
      LogError << "Broken header in WMO \"" << fname << "\" (chunk overruns file)." << std::endl;
      break;
    }

    switch (fourcc)
    {
      case 'MVER':
      {
        f.read(&version, 4);
        f.seek(payload_pos + size);
        break;
      }
      case 'MOGP':
      {
        f.read(&header, sizeof(wmo_group_header));
        unsigned fog_index = header.fogs[0];
        if (fog_index >= wmo->fogs.size())
        {
          fog = -1;
        }
        else
        {
          WMOFog& wf = wmo->fogs[fog_index];
          if (wf.r2 <= 0)
            fog = -1;
          else
            fog = header.fogs[0];
        }
        BoundingBoxMin = ::glm::vec3(header.box1[0], header.box1[2], -header.box1[1]);
        BoundingBoxMax = ::glm::vec3(header.box2[0], header.box2[2], -header.box2[1]);
        // Leave position at first subchunk (do not consume MOGP size).
        break;
      }
      case 'MOPY':
      {
        f.seek(payload_pos + size);
        break;
      }
      case 'MOVI':
      {
        // Classic 16-bit indices. Prefer MOVX when present (handled below).
        if (!_indices.empty())
        {
          f.seek(payload_pos + size);
          break;
        }
        if (size > kMaxWmoChunkBytes || (size % sizeof(std::uint16_t)) != 0)
          throw std::runtime_error(std::string("MOVI chunk invalid in ") + fname);
        std::size_t const count = size / sizeof(std::uint16_t);
        std::vector<std::uint16_t> raw(count);
        f.read(raw.data(), size);
        _indices.resize(count);
        for (std::size_t i = 0; i < count; ++i)
          _indices[i] = raw[i];
        break;
      }
      case 'MOVX':
      {
        // SL 9.0+: 32-bit indices (wowlib large_indices). Replaces MOVI when present.
        if (size > kMaxWmoChunkBytes || (size % sizeof(std::uint32_t)) != 0)
          throw std::runtime_error(std::string("MOVX chunk invalid in ") + fname);
        resize_wmo_chunk(_indices, size, "MOVX", fname.c_str());
        f.read(_indices.data(), size);
        break;
      }
      case 'MOVT':
      {
        ::glm::vec3 const* vertices = reinterpret_cast<::glm::vec3 const*>(f.getPointer());
        resize_wmo_chunk(_vertices, size, "MOVT", fname.c_str());
        VertexBoxMin = ::glm::vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        VertexBoxMax = ::glm::vec3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        rad = 0;
        for (size_t i = 0; i < _vertices.size(); ++i)
        {
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
        f.seek(payload_pos + size);
        break;
      }
      case 'MONR':
      {
        resize_wmo_chunk(_normals, size, "MONR", fname.c_str());
        f.read(_normals.data(), size);
        for (auto& n : _normals)
        {
          n = {n.x, n.z, -n.y};
        }
        break;
      }
      case 'MOTV':
      {
        if (size > kMaxWmoChunkBytes || (size % sizeof(glm::vec2)) != 0)
          throw std::runtime_error(std::string("MOTV chunk invalid in ") + fname);
        auto& motv = motv_sets.emplace_back();
        motv.resize(size / sizeof(glm::vec2));
        if (!motv.empty())
          f.read(motv.data(), size);
        break;
      }
      case 'MOBA':
      {
        resize_wmo_chunk(_batches, size, "MOBA", fname.c_str());
        f.read(_batches.data(), size);
        break;
      }
      case 'MOLR':
      case 'MOBN':
      case 'MOBR':
      case 'MPBV':
      case 'MPBP':
      case 'MPBI':
      case 'MPBG':
      case 'MORI':
      case 'MORB':
      {
        f.seek(payload_pos + size);
        break;
      }
      case 'MODR':
      {
        resize_wmo_chunk(_doodad_ref, size, "MODR", fname.c_str());
        f.read(_doodad_ref.data(), size);
        break;
      }
      case 'MOCV':
      {
        if (_vertex_colors.empty())
        {
          load_mocv(f, size);
        }
        else
        {
          if (size > kMaxWmoChunkBytes || (size % sizeof(CImVector)) != 0)
            throw std::runtime_error(std::string("MOCV2 chunk invalid in ") + fname);
          std::size_t const count = size / sizeof(CImVector);
          std::vector<CImVector> mocv_2(count);
          f.read(mocv_2.data(), size);
          for (std::size_t i = 0; i < count; ++i)
          {
            float const alpha = static_cast<float>(mocv_2[i].a) / 255.f;
            if (header.flags.has_vertex_color)
            {
              if (i < _vertex_colors.size())
                _vertex_colors[i].w = alpha;
            }
            else
            {
              _vertex_colors.emplace_back(0.f, 0.f, 0.f, alpha);
            }
          }
          // Keep MOCV2 length aligned with vertices when it was used as alpha-only init.
          if (!header.flags.has_vertex_color && _vertex_colors.size() > _vertices.size())
            _vertex_colors.resize(_vertices.size());
        }
        break;
      }
      case 'MLIQ':
      {
        WMOLiquidHeader hlq;
        f.read(&hlq, 0x1E);
        lq = std::make_unique<wmo_liquid>(
          &f
        , hlq
        , header.group_liquid
        , (bool)wmo->flags.use_liquid_type_dbc_id
        , (bool)header.flags.ocean
        );
        f.seek(payload_pos + size);
        break;
      }
      default:
      {
        f.seek(payload_pos + size);
        break;
      }
    }
  }

  // Prefer pass 0 as mesh UVs. Among later passes, pick the strongest atlas-magnitude
  // set for texcoord_2 (shader 23 / dual-layer). Promote primary to an atlas pass when
  // pass 0 is unused and a later pass clearly holds tile coords.
  if (!motv_sets.empty())
  {
    _texcoords = std::move(motv_sets[0]);
    if (motv_sets.size() > 1)
    {
      _texcoords_2.resize(_texcoords.size());
      for (std::size_t v = 0; v < _texcoords.size(); ++v)
      {
        int atlas_idx = -1;
        float atlas_mag = 2.f;
        for (std::size_t idx = 1; idx < motv_sets.size(); ++idx)
        {
          if (v >= motv_sets[idx].size())
            continue;
          glm::vec2 const& uv = motv_sets[idx][v];
          float const mag = std::max(std::abs(uv.x), std::abs(uv.y));
          if (mag > atlas_mag)
          {
            atlas_mag = mag;
            atlas_idx = static_cast<int>(idx);
          }
        }

        if (atlas_idx >= 0)
          _texcoords_2[v] = motv_sets[static_cast<std::size_t>(atlas_idx)][v];
        else if (v < motv_sets[1].size())
          _texcoords_2[v] = motv_sets[1][v];
        else
          _texcoords_2[v] = _texcoords[v];

        // Some shader-23 meshes store the only usable atlas on pass 1+ with magnitude > 8.
        float const primary_mag = std::max(std::abs(_texcoords[v].x), std::abs(_texcoords[v].y));
        if (primary_mag < 0.0001f && atlas_mag > 8.f)
          _texcoords[v] = _texcoords_2[v];
      }
    }
  }

  // Build render-batch mapping/flags now that optional MOCV/MOCV2 data is loaded.
  _renderer.initRenderBatches();

  if (header.flags.indoor && header.flags.has_vertex_color)
  {
    ::glm::vec3 dirmin(1, 1, 1);
    float lenmin;

    for (auto doodad : _doodad_ref)
    {
      if (doodad >= wmo->modelis.size())
      {
        continue;
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
  // MOCV stores packed uint32 colors; we expand each to glm::vec4. Do not use
  // resize_wmo_chunk (that divides by sizeof(vec4) and under-allocates → heap corruption).
  if (size > kMaxWmoChunkBytes || (size % sizeof(uint32_t)) != 0)
  {
    throw std::runtime_error(std::string("MOCV chunk invalid in ") + name);
  }
  std::size_t const count = size / sizeof(uint32_t);
  _vertex_colors.resize(count);

  for (size_t i(0); i < count; ++i)
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
    // Ambient is stored 0..1; FixColor math is in 0..255 byte space.
    wmo_ambient_color = wmo->ambient_light_color * 255.f;
    // w is not used, set it to 0 to avoid changing the vertex color alpha
    wmo_ambient_color.w = 0.f;
  }

  for (int i = 0; i < static_cast<int>(_vertex_colors.size()); ++i)
  {
    auto& color = _vertex_colors[i];
    // FixColorVertexAlpha is defined in 0..255 byte space (wowdev / client).
    float r = color.x * 255.f;
    float g = color.y * 255.f;
    float b = color.z * 255.f;
    float a = color.w * 255.f;

    if (i >= interior_batchs_start)
    {
      r += ((r * a / 64.f) - wmo_ambient_color.x);
      g += ((g * a / 64.f) - wmo_ambient_color.y);
      b += ((b * a / 64.f) - wmo_ambient_color.z);
    }
    else
    {
      r -= wmo_ambient_color.x;
      g -= wmo_ambient_color.y;
      b -= wmo_ambient_color.z;

      r = (r * (1.f - a / 255.f));
      g = (g * (1.f - a / 255.f));
      b = (b * (1.f - a / 255.f));
    }

    color.x = std::min(255.f, std::max(0.f, r)) / 255.f;
    color.y = std::min(255.f, std::max(0.f, g)) / 255.f;
    color.z = std::min(255.f, std::max(0.f, b)) / 255.f;
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
