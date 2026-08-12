//Folder to contain all of the project related files
#pragma once

#include <memory>
// #include <noggit/application/NoggitApplication.hpp>
#include <noggit/TextureManager.h>
#include <external/tsl/robin_map.h>

#include <filesystem>
#include <cstdint>
#include <vector>
#include <glm/vec3.hpp>


namespace BlizzardDatabaseLib
{
  class BlizzardDatabase;
}

namespace Noggit::Application
{
  struct NoggitApplicationConfiguration;
}

struct TileIndex;
class World;

namespace Noggit::Project
{
  class ApplicationProjectWriter;

  enum class ProjectVersion
  {
    VANILLA,
    BC,
    WOTLK,
    CATA,
    PANDARIA,
    WOD,
    LEGION,
    BFA,
    SL
  };

  struct ClientVersionFactory
  {
    static ProjectVersion mapToEnumVersion(std::string const& projectVersion);

    static std::string MapToStringVersion(ProjectVersion const& projectVersion);

    //! Client data pin used for DBDs / window chrome, e.g. "9.2.7.45745".
    static std::string MapToClientDataVersion(ProjectVersion const& projectVersion);
  };

  struct NoggitProjectBookmarkMap
  {
    int map_id;
    std::string name;
    glm::vec3 position;
    float camera_yaw;
    float camera_pitch;
  };

  struct NoggitProjectPinnedMap
  {
    int MapId;
    std::string MapName;
  };

  struct NoggitExtraMapData
  {
  public:
      //Mists Heightmapping
      // Valid for every map that doesn't override its specific data
      tsl::robin_map< std::string, texture_heightmapping_data > TextureHeightData_Global;
      // MapID,TileX,TileY, SMTextureParams for this specific ADT, fallback to global otherwise.
      tsl::robin_map<int, 
            tsl::robin_map< int, tsl::robin_map<int , tsl::robin_map<std::string, texture_heightmapping_data> >>> TextureHeightData_ADT;
      
      void SetTextureHeightData_Global(const std::string& texture, texture_heightmapping_data data, World* worldToUpdate = nullptr); 
      void SetTextureHeightDataForADT(int mapID, const TileIndex& ti, const std::string& texture, texture_heightmapping_data data, World* worldToUpdate = nullptr);

      const texture_heightmapping_data GetTextureHeightDataForADT(int mapID, const TileIndex& ti, const std::string& texture) const;

      //! Record an ADT MTXP sample. When global.cfg has no entry for this texture yet,
      //! keep the most common sample and eventually write it under project/extraData.
      void ObserveAdtHeightMapping(std::string const& texture, texture_heightmapping_data const& data);
      void PersistGlobalHeightMappingIfNeeded(std::filesystem::path const& project_path);
      void MarkGlobalHeightLoadedFromFile(std::string const& texture);

  private:
      //! texture -> (quantized key -> count). Key packs uvScale + quantized scale/offset.
      tsl::robin_map<std::string, tsl::robin_map<std::uint64_t, std::pair<int, texture_heightmapping_data>>> _adt_height_votes;
      tsl::robin_map<std::string, bool> _global_height_from_file;
      bool _global_height_cfg_dirty = false;
  };
  
  struct NoggitProjectObjectPalette
  {
      int MapId;
      std::vector<std::string> Filepaths;
  };

  struct NoggitProjectTexturePalette
  {
      int MapId;
      std::vector<std::string> Filepaths;
  };

  struct NoggitProjectSelectionGroups
  {
      int MapId;
      // Might let the user name them later if they get some list UI
      std::vector<std::vector<unsigned int>> SelectionGroups;
  };

  class NoggitProject
  {
    std::shared_ptr<ApplicationProjectWriter> _projectWriter;
  public:
    std::string ProjectPath;
    std::string ProjectName;
    std::string ClientPath;
    ProjectVersion projectVersion;
    std::vector<NoggitProjectPinnedMap> PinnedMaps;
    std::vector<NoggitProjectBookmarkMap> Bookmarks;
    std::shared_ptr<BlizzardDatabaseLib::BlizzardDatabase> ClientDatabase;
    std::shared_ptr<BlizzardArchive::ClientData> ClientData;
    std::vector<NoggitProjectObjectPalette> ObjectPalettes;
    std::vector<NoggitProjectTexturePalette> TexturePalettes;
    std::vector<NoggitProjectSelectionGroups> ObjectSelectionGroups;

    NoggitExtraMapData ExtraMapData;
    NoggitProject();

    void createBookmark(const NoggitProjectBookmarkMap& bookmark);

    void deleteBookmark();

    void pinMap(int map_id, const std::string& map_name);

    void unpinMap(int mapId);

    void saveTexturePalette(const NoggitProjectTexturePalette& new_texture_palette);

    void saveObjectPalette(const NoggitProjectObjectPalette& new_object_palette);

    void saveObjectSelectionGroups(const NoggitProjectSelectionGroups& new_selection_groups);
  };

  class ApplicationProject
  {
    std::shared_ptr<NoggitProject> _active_project;
    std::shared_ptr<Application::NoggitApplicationConfiguration> _configuration;
  public:
    ApplicationProject(std::shared_ptr<Application::NoggitApplicationConfiguration> configuration);

    void createProject(std::filesystem::path const& project_path, std::filesystem::path const& client_path,
                       std::string const& client_version, std::string const& project_name);

    std::shared_ptr<NoggitProject> loadProject(std::filesystem::path const& project_path);

    void loadExtraData(NoggitProject& project);
  };
}
