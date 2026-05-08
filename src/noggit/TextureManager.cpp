// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#include <noggit/TextureManager.h>
#include <noggit/Log.h> // LogDebug
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <ClientFile.hpp>

#include <QtGui/QImage>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLFramebufferObjectFormat>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QThread>

#include <algorithm>
#include <exception>
#include <mutex>
#include <glm/vec2.hpp>

decltype (TextureManager::_) TextureManager::_;
decltype (TextureManager::_tex_arrays) TextureManager::_tex_arrays;

constexpr unsigned N_ARRAY_TEX = 1;

void TextureManager::report()
{
  std::string output = "Still in the Texture manager:\n";
  _.apply ( [&] (BlizzardArchive::Listfile::FileKey const& key, blp_texture const&)
            {
              output += " - " + key.stringRepr() + "\n";
            }
          );
  LogDebug << output;
}

void TextureManager::unload_all(Noggit::NoggitRenderContext context)
{
  _.context_aware_apply(
      [&] (BlizzardArchive::Listfile::FileKey const&, blp_texture& blp_texture)
      {
          blp_texture.unload();
      }
      , context
  );

  // cleanup texture arrays
  auto& arrays_for_context = _tex_arrays[context];

  for (auto& pair : arrays_for_context)
  {
    gl.deleteTextures(static_cast<GLuint>(pair.second.arrays.size()), pair.second.arrays.data());
  }
}

TexArrayParams& TextureManager::get_tex_array(int width, int height, int mip_level,
                                              Noggit::NoggitRenderContext context)
{
  TexArrayParams& array_params = _tex_arrays[context][std::make_tuple(-1, width, height, mip_level)];

  GLint n_layers = N_ARRAY_TEX;
  //gl.getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &n_layers);

  int index_x = array_params.n_used / n_layers;

  if (array_params.arrays.size() <= index_x)
  {
    GLuint array;

    gl.genTextures(1, &array);
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array);

    array_params.arrays.emplace_back(array);

    int width_ = width;
    int height_ = height;

    for (int i = 0; i < mip_level; ++i)
    {
      gl.texImage3D(GL_TEXTURE_2D_ARRAY, i, GL_RGBA8, width_, height_, n_layers, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);

      width_ = std::max(width_ >> 1, 1);
      height_ = std::max(height_ >> 1, 1);
    }

    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mip_level - 1);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  else
  {
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array_params.arrays[index_x]);
  }

  return array_params;
}

TexArrayParams& TextureManager::get_tex_array(GLint compression, int width, int height, int mip_level,
                              std::map<int, std::vector<uint8_t>>& comp_data, Noggit::NoggitRenderContext context)
{

  TexArrayParams& array_params = _tex_arrays[context][std::make_tuple(compression, width, height, mip_level)];

  GLint n_layers = N_ARRAY_TEX;
  //gl.getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &n_layers);

  int index_x = array_params.n_used / n_layers;

  if (array_params.arrays.size() <= index_x)
  {
    GLuint array;

    gl.genTextures(1, &array);
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array);

    array_params.arrays.emplace_back(array);

    int width_ = width;
    int height_ = height;

    for (int i = 0; i < mip_level; ++i)
    {
      gl.compressedTexImage3D(GL_TEXTURE_2D_ARRAY, i, compression, width_, height_, n_layers, 0, static_cast<GLsizei>(comp_data[i].size() * n_layers), nullptr);

      width_ = std::max(width_ >> 1, 1);
      height_ = std::max(height_ >> 1, 1);
    }

    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mip_level - 1);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  else
  {
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array_params.arrays[index_x]);
  }

  return array_params;
}

#include <cstdint>
//! \todo Cross-platform syntax for packed structs.
#pragma pack(push,1)
struct BLPHeader
{
  int32_t magix;
  int32_t version;
  uint8_t attr_0_compression;
  uint8_t attr_1_alphadepth;
  uint8_t attr_2_alphatype;
  uint8_t attr_3_mipmaplevels;
  int32_t resx;
  int32_t resy;
  int32_t offsets[16];
  int32_t sizes[16];
};
#pragma pack(pop)

void blp_texture::bind()
{
  if (!finished)
  {
    return;
  }

  if (!_uploaded)
  {
    upload();
  }

  gl.bindTexture(GL_TEXTURE_2D_ARRAY, _texture_array);
}

void blp_texture::uploadToArray(unsigned layer)
{
  finishLoading();

  int width = _width, height = _height;

  if (!_compression_format)
  {

    for (int i = 0; i < _data.size(); ++i)
    {
      gl.texSubImage3D(GL_TEXTURE_2D_ARRAY, i, 0, 0, layer, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, _data[i].data());

      width = std::max(width >> 1, 1);
      height = std::max(height >> 1, 1);
    }

    _data.clear();

  }
  else
  {
    for (int i = 0; i < _compressed_data.size(); ++i)
    {
      gl.compressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, i, 0, 0, layer, width, height, 1, _compression_format.value(), static_cast<GLsizei>(_compressed_data[i].size()), _compressed_data[i].data());

      width = std::max(width >> 1, 1);
      height = std::max(height >> 1, 1);
    }

    _compressed_data.clear();
  }
}

void blp_texture::upload()
{
  if (!finished)
  {
    return;
  }

  if (_uploaded)
  {
    return;
  }

  int width = _width, height = _height;

  GLint n_layers = N_ARRAY_TEX;
  //gl.getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &n_layers);

  if (!_compression_format)
  {
    auto& params = TextureManager::get_tex_array( _width, _height, static_cast<int>(_data.size()), _context);

    int index_x = params.n_used / n_layers;
    int index_y = params.n_used % n_layers;

    _texture_array = params.arrays[index_x];
    _array_index = index_y;

    for (int i = 0; i < _data.size(); ++i)
    {
      gl.texSubImage3D(GL_TEXTURE_2D_ARRAY, i, 0, 0, index_y, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, _data[i].data());

      width = std::max(width >> 1, 1);
      height = std::max(height >> 1, 1);
    }

    params.n_used++;

    //LogDebug << "Mip level: " << std::to_string(_data.size()) << std::endl;

    _data.clear();
  }
  else
  {
    auto& params = TextureManager::get_tex_array(_compression_format.value(), _width, _height, static_cast<int>(_compressed_data.size()), _compressed_data, _context);

    int index_x = params.n_used / n_layers;
    int index_y = params.n_used % n_layers;

    _texture_array = params.arrays[index_x];
    _array_index = index_y;

    for (int i = 0; i < _compressed_data.size(); ++i)
    {
      gl.compressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, i, 0, 0, index_y, width, height, 1, _compression_format.value(), static_cast<GLsizei>(_compressed_data[i].size()), _compressed_data[i].data());

      width = std::max(width >> 1, 1);
      height = std::max(height >> 1, 1);
    }

    params.n_used++;

    //LogDebug << "Mip level (compressed): " << std::to_string(_compressed_data.size()) << std::endl;
    _compressed_data.clear();
  }

  _uploaded = true;
}

void blp_texture::unload()
{
  _uploaded = false;
  finished = false;
  if (hasHeightMap() && heightMap)
  {
      heightMap->unload();
  }
  _compression_format.reset();
  _texture_array = 0;
  _array_index = -1;
  _data.clear();
  _compressed_data.clear();

  // load data back from file. pretty sad. maybe keep it after loading?
  finishLoading();
}

bool blp_texture::is_uploaded() const
{
  return _uploaded;
}

GLuint blp_texture::texture_array() const
{
  return _texture_array;
}

int blp_texture::array_index() const
{
  return _array_index;
}

bool blp_texture::is_specular() const
{
  return _is_specular;
}

unsigned blp_texture::mip_level() const
{
  return static_cast<unsigned>(!_compression_format ? _data.size() : _compressed_data.size());
}

std::map<int, std::vector<uint32_t>>& blp_texture::data()
{
  return _data;
}

std::map<int, std::vector<uint8_t>>& blp_texture::compressed_data()
{
  return _compressed_data;
}

std::optional<GLint> const& blp_texture::compression_format() const
{
  return _compression_format;
}

Noggit::NoggitRenderContext blp_texture::getContext() const
{
  return _context;
}

[[nodiscard]]
async_priority blp_texture::loading_priority() const
{
  return async_priority::high;
}

// Mists HeightMapping
bool blp_texture::hasHeightMap() const
{
  return _has_heightmap;
}

blp_texture* blp_texture::getHeightMap()
{
  return heightMap.get();
}

void blp_texture::loadFromUncompressedData(BLPHeader const* lHeader, char const* lData)
{
  unsigned int const* pal = reinterpret_cast<unsigned int const*>(lData + sizeof(BLPHeader));

  unsigned char const* buf;
  unsigned int *p;
  unsigned char const* c;
  unsigned char const* a;

  int alphabits = lHeader->attr_1_alphadepth;
  bool hasalpha = alphabits != 0;

  int width = _width, height = _height;

  for (int i = 0; i<16; ++i)
  {
    width = std::max(1, width);
    height = std::max(1, height);

    if (lHeader->offsets[i] > 0 && lHeader->sizes[i] > 0)
    {
      buf = reinterpret_cast<unsigned char const*>(&lData[lHeader->offsets[i]]);

      std::vector<uint32_t> data(lHeader->sizes[i]);

      int cnt = 0;
      p = data.data();
      c = buf;
      a = buf + width*height;
      for (int y = 0; y<height; y++)
      {
        for (int x = 0; x<width; x++)
        {
          unsigned int k = pal[*c++];
          k = ((k & 0x00FF0000) >> 16) | ((k & 0x0000FF00)) | ((k & 0x000000FF) << 16);

          int alpha = 0xFF;

          if (_is_tileset && !_is_specular)
          {
            alpha = 0x00;
          }
          else if (hasalpha)
          {
            if (alphabits == 8)
            {
              alpha = (*a++);
            }
            else if (alphabits == 1)
            {
              alpha = (*a & (1 << cnt++)) ? 0xff : 0;
              if (cnt == 8)
              {
                cnt = 0;
                a++;
              }
            }
          }

          k |= alpha << 24;
          *p++ = k;
        }
      }

      _data[i] = data;
    }
    else
    {
      return;
    }

    width >>= 1;
    height >>= 1;
  }
}

void blp_texture::loadFromCompressedData(BLPHeader const* lHeader, char const* lData)
{
  //                         0 (0000) & 3 == 0                1 (0001) & 3 == 1                    7 (0111) & 3 == 3
  const int alphatypes[] = { GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT };
  const int blocksizes[] = { 8, 16, 0, 16 };

  int alpha_type = lHeader->attr_2_alphatype & 3;
  GLint format = alphatypes[alpha_type];
  _compression_format = format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? (lHeader->attr_1_alphadepth == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT) : format;

  int width = _width, height = _height;

  for (int i = 0; i < 16; ++i)
  {
    if (lHeader->sizes[i] <= 0 || lHeader->offsets[i] <= 0)
    {
      return;
    }

    // make sure the vector is of the right size, blizzard seems to fuck those up for some small mipmaps
    int size = std::floor((width + 3) / 4) * std::floor((height + 3) / 4) * blocksizes[alpha_type];

    if (size < lHeader->sizes[i])
    {
      LogDebug << "mipmap size mismatch in '" << _file_key.stringRepr() << "'" << std::endl;
      return;
    }

    _compressed_data[i].resize(size);

    char const* start = lData + lHeader->offsets[i];
    std::copy(start, start + lHeader->sizes[i], _compressed_data[i].begin());

    width = std::max(width >> 1, 1);
    height = std::max(height >> 1, 1);
  }
}

int blp_texture::width() const
{
  return _width;
}

int blp_texture::height() const
{
  return _height;
}

blp_texture::blp_texture(BlizzardArchive::Listfile::FileKey const& file_key, Noggit::NoggitRenderContext context)
  : AsyncObject(file_key)
  , _context(context)
{
}

void blp_texture::finishLoading()
{
  bool exists = Noggit::Application::NoggitApplication::instance()->clientData()->exists( _file_key.filepath());
  if (!exists)
  {
    LogError << "file not found: '" <<  _file_key.stringRepr() << "'" << std::endl;
  }

  std::string spec_filename = "", height_filename = "";
  bool has_specular = false, has_height = false;
  bool use_specular_file = false;

  if (_file_key.filepath().starts_with("tileset/") )
  {
    _is_tileset = true;

    spec_filename = _file_key.filepath().substr(0, _file_key.filepath().find_last_of(".")) + "_s.blp";
    has_specular = Noggit::Application::NoggitApplication::instance()->clientData()->exists(spec_filename);

    // Open the *_s.blp only for live terrain rendering. Thumbnails / asset UI request the
    // diffuse path and should decode that file, not the specular map.
    if (has_specular && _context == Noggit::NoggitRenderContext::MAP_VIEW)
    {
      _is_specular = true;
      use_specular_file = true;
    }

    bool modern_features = Noggit::Application::NoggitApplication::instance()->getConfiguration()->modern_features;

    // Only load _h in map view when modern features are enabled
    if(_context == Noggit::NoggitRenderContext::MAP_VIEW && modern_features)
    {
        height_filename = _file_key.filepath().substr(0, _file_key.filepath().find_last_of(".")) + "_h.blp";
        has_height = Noggit::Application::NoggitApplication::instance()->clientData()->exists(height_filename);
        if (has_height)
        {
            _has_heightmap = true;
            heightMap = std::make_unique<blp_texture>(height_filename,_context);
            heightMap->finishLoading();
        }
    }
  }

  std::string const open_path =
      exists ? (use_specular_file ? spec_filename : _file_key.filepath()) : "textures/shanecube.blp";

  BlizzardArchive::ClientFile f(
      open_path
      , Noggit::Application::NoggitApplication::instance()->clientData());
  if (f.isEof())
  {
    finished = true;
    throw std::runtime_error ("File " + _file_key.stringRepr() + " does not exist");
  }

  char const* lData = f.getPointer();
  BLPHeader const* lHeader = reinterpret_cast<BLPHeader const*>(lData);
  _width = lHeader->resx;
  _height = lHeader->resy;

  if (lHeader->attr_0_compression == 1)
  {
    loadFromUncompressedData(lHeader, lData);
  }
  else if (lHeader->attr_0_compression == 2)
  {
    loadFromCompressedData(lHeader, lData);
  }
  else
  {
      BlizzardArchive::ClientFile fallback("textures/shanecube.blp", Noggit::Application::NoggitApplication::instance()->clientData());

      char const* lData_f = fallback.getPointer();
      BLPHeader const* lHeader_f = reinterpret_cast<BLPHeader const*>(lData_f);
      _width = lHeader_f->resx;
      _height = lHeader_f->resy;

      if (lHeader_f->attr_0_compression == 1)
      {
          loadFromUncompressedData(lHeader_f, lData_f);
      }
      else if (lHeader_f->attr_0_compression == 2)
      {
          loadFromCompressedData(lHeader_f, lData_f);
      }
      else
      {
          finished = true;
          throw std::logic_error("Unsupported BLP compression");
      }

      fallback.close();

      LogError << "Unsupported BLP compression: " << _file_key.filepath() << std::endl;
  }

  f.close();
  finished = true;
  _state_changed.notify_all();
}

namespace
{
  constexpr int kGL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0;
  constexpr int kGL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1;
  constexpr int kGL_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2;
  constexpr int kGL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;
  constexpr unsigned kGL_OUT_OF_MEMORY = 0x0505;

  void rgb565_to_rgb8 (uint16_t c, uint8_t out[3])
  {
    unsigned const r = (c >> 11) & 31u;
    unsigned const g = (c >> 5) & 63u;
    unsigned const b = c & 31u;
    out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
  }

  void decode_bc1_color_block (uint8_t const* s, uint32_t out_rgba[16])
  {
    uint16_t const c0 = static_cast<uint16_t>(s[0] | (s[1] << 8));
    uint16_t const c1 = static_cast<uint16_t>(s[2] | (s[3] << 8));
    uint32_t const bits = static_cast<uint32_t>(s[4] | (s[5] << 8) | (s[6] << 16) | (s[7] << 24));
    uint8_t rgb0[3]{}, rgb1[3]{};
    rgb565_to_rgb8 (c0, rgb0);
    rgb565_to_rgb8 (c1, rgb1);
    auto pack = [] (uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t
    {
      return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
          | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    };
    uint32_t col[4]{};
    col[0] = pack (rgb0[0], rgb0[1], rgb0[2], 255);
    col[1] = pack (rgb1[0], rgb1[1], rgb1[2], 255);
    if (c0 > c1)
    {
      col[2] = pack (static_cast<uint8_t>((2 * rgb0[0] + rgb1[0]) / 3),
                     static_cast<uint8_t>((2 * rgb0[1] + rgb1[1]) / 3),
                     static_cast<uint8_t>((2 * rgb0[2] + rgb1[2]) / 3), 255);
      col[3] = pack (static_cast<uint8_t>((rgb0[0] + 2 * rgb1[0]) / 3),
                     static_cast<uint8_t>((rgb0[1] + 2 * rgb1[1]) / 3),
                     static_cast<uint8_t>((rgb0[2] + 2 * rgb1[2]) / 3), 255);
    }
    else
    {
      col[2] = pack (static_cast<uint8_t>((rgb0[0] + rgb1[0]) / 2),
                     static_cast<uint8_t>((rgb0[1] + rgb1[1]) / 2),
                     static_cast<uint8_t>((rgb0[2] + rgb1[2]) / 2), 255);
      col[3] = pack (0, 0, 0, 0);
    }
    for (int i = 0; i < 16; ++i)
    {
      unsigned const idx = (bits >> (2 * i)) & 3u;
      out_rgba[i] = col[idx];
    }
  }

  uint8_t decode_bc3_alpha_value (unsigned idx, uint8_t a0, uint8_t a1)
  {
    if (a0 > a1)
    {
      if (idx == 0)
      {
        return a0;
      }
      if (idx == 1)
      {
        return a1;
      }
      static int const w0[6]{6, 5, 4, 3, 2, 1};
      static int const w1[6]{1, 2, 3, 4, 5, 6};
      unsigned const j = idx - 2;
      return static_cast<uint8_t>((w0[j] * a0 + w1[j] * a1) / 7);
    }
    if (idx == 0)
    {
      return a0;
    }
    if (idx == 1)
    {
      return a1;
    }
    if (idx < 6)
    {
      static int const w0[4]{4, 3, 2, 1};
      static int const w1[4]{1, 2, 3, 4};
      unsigned const j = idx - 2;
      return static_cast<uint8_t>((w0[j] * a0 + w1[j] * a1) / 5);
    }
    if (idx == 6)
    {
      return 0;
    }
    return 255;
  }

  void decode_bc3_block (uint8_t const* s, uint32_t out_rgba[16])
  {
    uint8_t const a0 = s[0];
    uint8_t const a1 = s[1];
    uint64_t a_bits = s[2];
    a_bits |= static_cast<uint64_t>(s[3]) << 8;
    a_bits |= static_cast<uint64_t>(s[4]) << 16;
    a_bits |= static_cast<uint64_t>(s[5]) << 24;
    a_bits |= static_cast<uint64_t>(s[6]) << 32;
    a_bits |= static_cast<uint64_t>(s[7]) << 40;
    uint32_t rgb[16]{};
    decode_bc1_color_block (s + 8, rgb);
    for (int i = 0; i < 16; ++i)
    {
      unsigned const idx_a = static_cast<unsigned>((a_bits >> (3 * i)) & 7u);
      uint8_t const A = decode_bc3_alpha_value (idx_a, a0, a1);
      uint32_t const c = rgb[i];
      uint8_t const r = static_cast<uint8_t>(c & 0xFFu);
      uint8_t const g = static_cast<uint8_t>((c >> 8) & 0xFFu);
      uint8_t const b = static_cast<uint8_t>((c >> 16) & 0xFFu);
      out_rgba[i] = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
          | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(A) << 24);
    }
  }

  void decode_bc2_block (uint8_t const* s, uint32_t out_rgba[16])
  {
    uint32_t rgb[16]{};
    decode_bc1_color_block (s + 8, rgb);
    for (int i = 0; i < 16; ++i)
    {
      unsigned const n = static_cast<unsigned>((s[i / 2] >> ((i % 2) * 4)) & 0xFu);
      uint8_t const A = static_cast<uint8_t>(n * 17u);
      uint32_t const c = rgb[i];
      uint8_t const r = static_cast<uint8_t>(c & 0xFFu);
      uint8_t const g = static_cast<uint8_t>((c >> 8) & 0xFFu);
      uint8_t const b = static_cast<uint8_t>((c >> 16) & 0xFFu);
      out_rgba[i] = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
          | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(A) << 24);
    }
  }

  bool decompress_s3tc ( int format
                       , uint8_t const* data
                       , std::size_t data_size
                       , int width
                       , int height
                       , std::vector<uint8_t>& rgba
                       )
  {
    if (width <= 0 || height <= 0)
    {
      return false;
    }
    int const bx = (width + 3) / 4;
    int const by = (height + 3) / 4;
    std::size_t const bsize
        = (format == kGL_COMPRESSED_RGB_S3TC_DXT1_EXT || format == kGL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ? 8u : 16u;
    std::size_t const need = static_cast<std::size_t>(bx * by) * bsize;
    if (data_size < need)
    {
      return false;
    }

    rgba.assign (static_cast<std::size_t>(width * height * 4), 0);
    for (int byi = 0; byi < by; ++byi)
    {
      for (int bxi = 0; bxi < bx; ++bxi)
      {
        std::size_t const block_idx = static_cast<std::size_t>(byi * bx + bxi);
        uint8_t const* block = data + block_idx * bsize;
        uint32_t pix[16]{};
        if (format == kGL_COMPRESSED_RGB_S3TC_DXT1_EXT || format == kGL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
        {
          decode_bc1_color_block (block, pix);
        }
        else if (format == kGL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
        {
          decode_bc2_block (block, pix);
        }
        else if (format == kGL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
        {
          decode_bc3_block (block, pix);
        }
        else
        {
          return false;
        }

        for (int py = 0; py < 4; ++py)
        {
          for (int px = 0; px < 4; ++px)
          {
            int const x = bxi * 4 + px;
            int const y = byi * 4 + py;
            if (x >= width || y >= height)
            {
              continue;
            }
            uint32_t const p = pix[py * 4 + px];
            uint8_t* d = &rgba[static_cast<std::size_t>(y * width + x) * 4u];
            d[0] = static_cast<uint8_t>(p & 0xFFu);
            d[1] = static_cast<uint8_t>((p >> 8) & 0xFFu);
            d[2] = static_cast<uint8_t>((p >> 16) & 0xFFu);
            d[3] = static_cast<uint8_t>((p >> 24) & 0xFFu);
          }
        }
      }
    }
    return true;
  }

  QImage blpr_blp_texture_to_qimage (blp_texture& tex)
  {
    tex.finishLoading();
    if (!tex.compression_format())
    {
      auto const dit = tex.data().find (0);
      if (dit == tex.data().end())
      {
        return {};
      }
      std::vector<uint32_t> const& mip0 = dit->second;
      if (mip0.size() < static_cast<std::size_t>(tex.width() * tex.height()))
      {
        return {};
      }
      QImage img ( reinterpret_cast<uchar const*>(mip0.data())
                 , tex.width()
                 , tex.height()
                 , tex.width() * 4
                 , QImage::Format_RGBA8888);
      return img.copy();
    }

    int const fmt = static_cast<int>(*tex.compression_format());
    auto const cit = tex.compressed_data().find (0);
    if (cit == tex.compressed_data().end())
    {
      return {};
    }
    std::vector<uint8_t> const& comp0 = cit->second;
    std::vector<uint8_t> rgba;
    if (!decompress_s3tc (fmt, comp0.data(), comp0.size(), tex.width(), tex.height(), rgba))
    {
      return {};
    }
    QImage img ( rgba.data()
               , tex.width()
               , tex.height()
               , tex.width() * 4
               , QImage::Format_RGBA8888);
    return img.copy();
  }
}

namespace Noggit
{
    BLPRenderer& BLPRenderer::getInstance()
    {
        // Single instance: all OpenGL for this helper runs on the Qt GUI thread (see render_blp_to_pixmap).
        static BLPRenderer instance;
        return instance;
    }

    QPixmap BLPRenderer::blp_to_pixmap_cpu ( std::string const& blp_filename, int width, int height)
    {
      blp_texture tex (blp_filename, Noggit::NoggitRenderContext::BLP_RENDERER);
      QImage im = blpr_blp_texture_to_qimage (tex);
      if (im.isNull())
      {
        throw std::runtime_error ("CPU BLP decode failed: " + blp_filename);
      }
      int const tw = width;
      int const th = height;
      if (tw > 0 && th > 0 && (tw != im.width() || th != im.height()))
      {
        im = im.scaled (tw, th, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      }
      return QPixmap::fromImage (std::move (im));
    }

    QPixmap* BLPRenderer::render_blp_to_pixmap_impl ( std::string const& blp_filename
                                                    , int width
                                                    , int height
                                                    )
  {
    std::tuple<std::string, int, int> const curEntry{blp_filename, width, height};
    auto it{_cache.find(curEntry)};

    if (it != _cache.end())
    {
      return &it->second;
    }

    // CPU-only icon path (no OpenGL). This avoids driver crashes during map load
    // while keeping thumbnails visually consistent.
    _cpu_fallback = true;
    QPixmap pm = blp_to_pixmap_cpu (blp_filename, width, height);
    return &(_cache[curEntry] = std::move (pm));
  }

    QPixmap* BLPRenderer::render_blp_to_pixmap ( std::string const& blp_filename
                                               , int width
                                               , int height
                                               )
  {
    QCoreApplication* const app = QCoreApplication::instance();
    if (app && QThread::currentThread() != app->thread())
    {
      struct Dispatch
      {
        BLPRenderer* self = nullptr;
        std::string path;
        int w = 0;
        int h = 0;
        QPixmap* out = nullptr;
        std::exception_ptr err{};
      } d{this, blp_filename, width, height};

      QMetaObject::invokeMethod(
          app,
          [&d]()
          {
            try
            {
              d.out = d.self->render_blp_to_pixmap_impl(d.path, d.w, d.h);
            }
            catch (...)
            {
              d.err = std::current_exception();
            }
          },
          Qt::BlockingQueuedConnection);

      if (d.err)
      {
        std::rethrow_exception(d.err);
      }
      return d.out;
    }

    return render_blp_to_pixmap_impl(blp_filename, width, height);
  }

  bool BLPRenderer::upload_gl()
  {
    static std::once_flag blpr_module_built_log;
    std::call_once(
        blpr_module_built_log,
        []
        {
          LogDebug << "BLPRenderer (TextureManager.cpp) built " << __DATE__ << " " << __TIME__ << std::endl;
        });

    if (_uploaded)
    {
      return true;
    }

    _cache = {};

    OpenGL::context::save_current_context const context_save (::gl);

    _context = std::make_unique<QOpenGLContext>();
    _fmt = std::make_unique<QOpenGLFramebufferObjectFormat>();
    _surface = std::make_unique<QOffscreenSurface>();

    // Standalone context (no share group): sharing with the map QOpenGLWidget context has
    // triggered bogus GL_OUT_OF_MEMORY on tiny uploads and nvoglv64 faults during FBO readback
    // on some NVIDIA drivers when mixed with the rest of the app's GL traffic.
    QSurfaceFormat const fmt = QSurfaceFormat::defaultFormat();
    _context->setFormat (fmt);
    _surface->setFormat (fmt);

    // QOpenGLFramebufferObject::toImage() + MSAA has been seen to crash in DrvPresentBuffers;
    // BLP thumbnails do not need multisampling.
    _fmt->setSamples (0);
    _fmt->setInternalTextureFormat (GL_RGBA8);

    _surface->create();
    _context->create();
    if (!_context->isValid())
    {
      LogError << "BLPRenderer: failed to create a valid QOpenGLContext" << std::endl;
      unload();
      _cpu_fallback = true;
      return false;
    }

    if (!_context->makeCurrent (_surface.get()))
    {
      LogError << "BLPRenderer: makeCurrent() failed" << std::endl;
      unload();
      _cpu_fallback = true;
      return false;
    }

    // QOpenGLContext function tables are per-context; initialize them explicitly.
    _context->functions()->initializeOpenGLFunctions();
    QOpenGLFunctions* glf = _context->functions();
    while (glf->glGetError() != GL_NO_ERROR)
    {
    }
    glf->glFinish();

    OpenGL::context::scoped_setter const context_set (::gl, _context.get());

    OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> cull;
    OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_FALSE> depth;

    _vao.upload();
    _buffers.upload();

    GLuint const& indices_vbo = _buffers[0];
    GLuint const& vertices_vbo = _buffers[1];
    GLuint const& texcoords_vbo = _buffers[2];

    std::vector<glm::vec2> vertices =
        {
             {-1.0f, -1.0f}
            ,{-1.0f, 1.0f}
            ,{ 1.0f, 1.0f}
            ,{ 1.0f, -1.0f}
        };
    std::vector<glm::vec2> texcoords =
        {
             {0.f, 0.f}
            ,{0.f, 1.0f}
            ,{1.0f, 1.0f}
            ,{1.0f, 0.f}
        };
    std::vector<std::uint16_t> indices = {0,1,2, 2,3,0};

    gl.bufferData<GL_ARRAY_BUFFER,glm::vec2>(vertices_vbo, vertices, GL_STATIC_DRAW);
    gl.bufferData<GL_ARRAY_BUFFER,glm::vec2>(texcoords_vbo, texcoords, GL_STATIC_DRAW);
    gl.bufferData<GL_ELEMENT_ARRAY_BUFFER, std::uint16_t>(indices_vbo, indices, GL_STATIC_DRAW);

    bool gl_bad = false;
    bool oom = false;
    GLenum e = GL_NO_ERROR;
    while ((e = glf->glGetError()) != GL_NO_ERROR)
    {
      gl_bad = true;
      if (e == static_cast<GLenum>(kGL_OUT_OF_MEMORY))
      {
        oom = true;
      }
    }
    if (gl_bad)
    {
      LogError << "BLPRenderer: OpenGL error during icon geometry upload"
               << (oom ? " (including GL_OUT_OF_MEMORY)" : "") << ", using CPU BLP decode fallback" << std::endl;
      unload();
      _cpu_fallback = true;
      return false;
    }

    try
    {
      _program.reset (new OpenGL::program
                         (
                             {
                                 {
                                     GL_VERTEX_SHADER, R"code(
                                  #version 330 core

                                  in vec4 position;
                                  in vec2 tex_coord;
                                  out vec2 f_tex_coord;

                                  uniform float width;
                                  uniform float height;

                                  void main()
                                  {
                                    f_tex_coord = vec2(tex_coord.x * width, -tex_coord.y * height);
                                    gl_Position = vec4(position.x * width / 2, position.y * height / 2, position.z, 1.0);
                                  }
                                  )code"
                                 },
                                 {
                                     GL_FRAGMENT_SHADER, R"code(
                                  #version 330 core

                                  uniform sampler2DArray tex;
                                  uniform int tex_index;

                                  in vec2 f_tex_coord;

                                  layout(location = 0) out vec4 out_color;

                                  void main()
                                  {
                                    out_color = vec4(texture(tex, vec3(f_tex_coord/2.f + vec2(0.5), tex_index)).rgb, 1.);
                                  }
                                  )code"
                                 }
                             }
                         ));
    }
    catch (...)
    {
      LogError << "BLPRenderer: shader compile failed, using CPU BLP decode fallback" << std::endl;
      unload();
      _cpu_fallback = true;
      return false;
    }

    OpenGL::Scoped::use_program shader (*_program.get());

    OpenGL::Scoped::vao_binder const _ (_vao[0]);

    {
      OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> vertices_binder (vertices_vbo);
      shader.attrib ("position", 2, GL_FLOAT, GL_FALSE, 0, 0);
    }
    {
      OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> texcoords_binder (texcoords_vbo);
      shader.attrib ("tex_coord", 2, GL_FLOAT, GL_FALSE, 0, 0);
    }

    _uploaded = true;
    return true;
  }

  void BLPRenderer::unload()
  {
    _cache.clear();
    // Leave CPU fallback enabled; never touch OpenGL resources here.
    _cpu_fallback = true;
    _uploaded = false;
    _program.reset();
    _surface.reset();
    _fmt.reset();
    _context.reset();
  }

}
