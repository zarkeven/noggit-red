#include "ApplicationProject.h"
#include "ApplicationProjectReader.h"
#include "ApplicationProjectWriter.h"
#include "WowExportListfileDownload.hpp"

#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <noggit/map_light_target.hpp>
#include <noggit/World.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>

#include <blizzard-database-library/include/BlizzardDatabase.h>
#include <blizzard-archive-library/include/CASCArchive.hpp>
#include <string>
#include <blizzard-archive-library/include/Exception.hpp>
#include <blizzard-archive-library/include/ClientFile.hpp>
#include <noggit/Log.h>

#include <QFile>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTextStream>

#include <cmath>
#include <cstdint>

namespace Noggit::Project
{
  ApplicationProject::ApplicationProject(std::shared_ptr<Application::NoggitApplicationConfiguration> configuration)
  {
    _active_project = nullptr;
    _configuration = configuration;
  }

  void ApplicationProject::createProject(std::filesystem::path const& project_path, std::filesystem::path const& client_path, std::string const& client_version, std::string const& project_name)
  {
    if (!std::filesystem::exists(project_path))
      std::filesystem::create_directory(project_path);

    auto project = NoggitProject();
    project.ProjectName = project_name;
    project.projectVersion = ClientVersionFactory::mapToEnumVersion(client_version);
    project.ClientPath = client_path.generic_string();
    project.ProjectPath = project_path.generic_string();

    auto project_writer = ApplicationProjectWriter();
    project_writer.saveProject(&project, project_path);
  }

  std::shared_ptr<NoggitProject> ApplicationProject::loadProject(std::filesystem::path const& project_path)
  {
    ApplicationProjectReader project_reader{};
    auto project = project_reader.readProject(project_path);

    if (!project.has_value())
    {
      LogError << "loadProject() failed, Project is null" << std::endl;
      return {};
    }
    else
    {
      Log << "loadProject(): Loading Project Data" << std::endl;
    }


    project_reader.readPalettes(&project.value());
    project_reader.readObjectSelectionGroups(&project.value());

    std::string dbd_file_directory = _configuration->ApplicationDatabaseDefinitionsPath;

    BlizzardDatabaseLib::Structures::Build client_build("3.3.5.12340");
    auto client_archive_version = BlizzardArchive::ClientVersion::WOTLK;
    auto client_archive_locale = BlizzardArchive::Locale::AUTO;
    if (project->projectVersion == ProjectVersion::SL)
    {
      client_archive_version = BlizzardArchive::ClientVersion::SL;
      client_build = BlizzardDatabaseLib::Structures::Build(Noggit::MapLightTarget::client_build_string);
      client_archive_locale = BlizzardArchive::Locale::enUS;
    }

    else if (project->projectVersion == ProjectVersion::WOTLK)
    {
      client_archive_version = BlizzardArchive::ClientVersion::WOTLK;
      client_build = BlizzardDatabaseLib::Structures::Build("3.3.5.12340");
      client_archive_locale = BlizzardArchive::Locale::AUTO;
    }

    else
    {
      LogError << "Unsupported project version" << std::endl;
      return {};
    }

    project->ClientDatabase = std::make_shared<BlizzardDatabaseLib::BlizzardDatabase>(dbd_file_directory, client_build);

    Log << "Loading Client Path : " << project->ClientPath << std::endl;

    if (project->projectVersion == ProjectVersion::SL
        && ! _configuration->ApplicationListfileWowExportUrlTemplate.empty())
    {
      QString const url_t = QString::fromStdString (_configuration->ApplicationListfileWowExportUrlTemplate);
      bool const needs_build =
        url_t.contains (QStringLiteral ("%s")) || url_t.contains (QStringLiteral ("%1"));

      std::filesystem::path const listfile_csv = project_path / "listfile.csv";
      bool const need_download =
        _configuration->ApplicationListfileWowExportAlwaysDownload || ! std::filesystem::exists (listfile_csv);

      if (need_download && (! needs_build || ! _configuration->ApplicationListfileWowExportBuild.empty()))
      {
        QString dl_err;
        if (wow_export_download_listfile_csv (
              url_t,
              QString::fromStdString (_configuration->ApplicationListfileWowExportBuild),
              listfile_csv,
              &dl_err))
        {
          Log << "Downloaded listfile.csv (remote listfile)" << std::endl;
        }
        else
        {
          LogError << "Listfile download failed: " << dl_err.toStdString() << std::endl;
          QMessageBox::warning (nullptr,
                                QStringLiteral("Listfile download"),
                                QStringLiteral("Could not download listfile.\n%1").arg (dl_err));
        }
      }
      else if (need_download && needs_build)
      {
        Log << "Listfile download skipped: set ApplicationListfileWowExportBuild for this URL pattern." << std::endl;
      }
    }

    try
    {
      project->ClientData = std::make_shared<BlizzardArchive::ClientData>(
        project->ClientPath, client_archive_version, client_archive_locale, project_path.generic_string());
    }
    catch (BlizzardArchive::Exceptions::Locale::LocaleNotFoundError& e)
    {
      LogError << e.what() << std::endl;
      QMessageBox::critical(nullptr, "Error", e.what());
      return {};
    }
    catch (BlizzardArchive::Exceptions::Locale::IncorrectLocaleModeError& e)
    {
      LogError << e.what() << std::endl;
      QMessageBox::critical(nullptr, "Error", e.what());
      return {};
    }
    catch (BlizzardArchive::Exceptions::Archive::ArchiveOpenError& e)
    {
      LogError << e.what() << std::endl;
      QMessageBox::critical(nullptr, "Error", e.what());
      return {};
    }
    catch (...)
    {
      LogError << "Failed loading Client data. Unhandled exception." << std::endl;
      return {};
    }

    if (!project->ClientData)
    {
      LogError << "Failed loading Client data." << std::endl;
      return {};
    }

    // Log << "Client Version: " << static_cast<int>(project->ClientData->version()) << std::endl;

    Log << "Client Locale: " << project->ClientData->locale_name() << std::endl;

    for (auto const loaded_achive : *project->ClientData->loadedArchives())
    {
      Log << "Loaded client Archive: " << loaded_achive->path() << std::endl;
    }

    // QSettings settings;
    // bool modern_features = settings.value("modern_features", false).toBool();
    bool modern_features = _configuration->modern_features;
    if (modern_features)
    {
      Log << "Modern Features Enabled" << std::endl;
      loadExtraData(project.value());
    }
    else
    {
      Log << "Modern Features Disabled" << std::endl;
    }
    return std::make_shared<NoggitProject>(project.value());
  }

  void ApplicationProject::loadExtraData(NoggitProject& project)
    {
        std::filesystem::path extraDataFolder = (project.ProjectPath);
        extraDataFolder /= "extraData";

        Log << "Loading extra data from " << extraDataFolder << std::endl;

        if (std::filesystem::exists(extraDataFolder) && std::filesystem::is_directory(extraDataFolder))
        {
            for (const auto& entry : std::filesystem::directory_iterator(extraDataFolder))
            {
                if (entry.path().extension() == ".cfg")
                {
                    QFile input_file(QString::fromStdString(entry.path().generic_string()));
                    input_file.open(QIODevice::ReadOnly);
                    QJsonParseError err;
                    auto document = QJsonDocument().fromJson(input_file.readAll(), &err);
                    auto root = document.object();
                    auto keys = root.keys();
                    if (entry.path().stem() == "global")
                    {
                        for (auto const& entry : keys)
                        {
                            texture_heightmapping_data newData;
                            newData.uvScale = root[entry].toObject()["Scale"].toInt();
                            newData.heightOffset = root[entry].toObject()["HeightOffset"].toDouble();
                            newData.heightScale = root[entry].toObject()["HeightScale"].toDouble();
                            project.ExtraMapData.SetTextureHeightData_Global(entry.toStdString(), newData);
                            project.ExtraMapData.MarkGlobalHeightLoadedFromFile(entry.toStdString());
                        }
                    }
                }
            }
        }
    }
    void NoggitExtraMapData::SetTextureHeightData_Global(const std::string& texture, texture_heightmapping_data data, World* worldToUpdate)
    {
        TextureHeightData_Global[texture] = data;
        if (worldToUpdate)
        {
            for (MapTile* tile : worldToUpdate->mapIndex.loaded_tiles())
            {
                tile->registerChunkUpdate(ChunkUpdateFlags::ALPHAMAP);
                tile->forceAlphaUpdate();
                tile->forceRecalcExtents();
            }
        }
    }
    void NoggitExtraMapData::SetTextureHeightDataForADT(int mapID, const TileIndex& ti, const std::string& texture, texture_heightmapping_data data, World* worldToUpdate)
    {
        TextureHeightData_ADT[mapID][ti.x][ti.z][texture] = data;
        if (worldToUpdate)
        {
            MapTile* tile = worldToUpdate->mapIndex.getTile(ti);
            tile->registerChunkUpdate(ChunkUpdateFlags::ALPHAMAP);
            tile->forceAlphaUpdate();
            tile->forceRecalcExtents();
        }
    }
    const texture_heightmapping_data NoggitExtraMapData::GetTextureHeightDataForADT(int mapID, const TileIndex& tileIndex, const std::string& texture) const
    {
        static texture_heightmapping_data defaultValue;
        auto foundMapIter = TextureHeightData_ADT.find(mapID);
        if (foundMapIter != TextureHeightData_ADT.end())
        {
            auto foundXIter = foundMapIter->second.find(tileIndex.x);
            if (foundXIter != foundMapIter->second.end())
            {
                auto foundYIter = foundXIter->second.find(tileIndex.z);
                if (foundYIter != foundXIter->second.end())
                {
                    auto foundTexData = foundYIter->second.find(texture);
                    if (foundTexData != foundYIter->second.end())
                    {
                        return foundTexData->second;
                    }
                }
            }
        }
        auto foundGenericSettings = TextureHeightData_Global.find(texture);
        if (foundGenericSettings != TextureHeightData_Global.end())
        {
            return foundGenericSettings->second;
        }
        return defaultValue;
    }

    namespace
    {
      std::uint64_t height_mapping_vote_key(texture_heightmapping_data const& data)
      {
        auto quant = [](float v) -> std::int32_t {
          return static_cast<std::int32_t>(std::lround(v * 1000.f));
        };
        std::uint64_t key = data.uvScale & 0xFFFFu;
        key |= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(quant(data.heightScale))) << 16);
        key |= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(quant(data.heightOffset))) << 40);
        return key;
      }
    }

    void NoggitExtraMapData::ObserveAdtHeightMapping(std::string const& texture, texture_heightmapping_data const& data)
    {
      // Never override an entry that came from extraData/global.cfg.
      if (_global_height_from_file.contains(texture))
        return;

      auto& votes = _adt_height_votes[texture];
      auto const key = height_mapping_vote_key(data);
      auto& slot = votes[key];
      if (slot.first == 0)
        slot.second = data;
      ++slot.first;

      texture_heightmapping_data best = data;
      int best_count = 0;
      for (auto const& [_, vote] : votes)
      {
        if (vote.first > best_count)
        {
          best_count = vote.first;
          best = vote.second;
        }
      }

      TextureHeightData_Global[texture] = best;
      _global_height_cfg_dirty = true;
    }

    void NoggitExtraMapData::MarkGlobalHeightLoadedFromFile(std::string const& texture)
    {
      _global_height_from_file[texture] = true;
    }

    void NoggitExtraMapData::PersistGlobalHeightMappingIfNeeded(std::filesystem::path const& project_path)
    {
      if (!_global_height_cfg_dirty || TextureHeightData_Global.empty())
        return;

      std::filesystem::path extra = project_path / "extraData";
      std::error_code ec;
      std::filesystem::create_directories(extra, ec);

      QJsonObject root;
      for (auto const& [tex, data] : TextureHeightData_Global)
      {
        QJsonObject entry;
        entry.insert(QStringLiteral("Scale"), static_cast<int>(data.uvScale));
        entry.insert(QStringLiteral("HeightScale"), data.heightScale);
        entry.insert(QStringLiteral("HeightOffset"), data.heightOffset);
        root.insert(QString::fromStdString(tex), entry);
      }

      QFile out(QString::fromStdString((extra / "global.cfg").generic_string()));
      if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        LogError << "Failed to write " << (extra / "global.cfg") << std::endl;
        return;
      }

      out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
      out.close();
      _global_height_cfg_dirty = false;
      Log << "Wrote height-blend defaults to " << (extra / "global.cfg") << std::endl;
    }

    ProjectVersion ClientVersionFactory::mapToEnumVersion(std::string const& projectVersion)
    {
      if (projectVersion == "Wrath Of The Lich King")
        return ProjectVersion::WOTLK;
      if (projectVersion == "Shadowlands")
        return ProjectVersion::SL;

      assert(false);
    }

    std::string ClientVersionFactory::MapToStringVersion(ProjectVersion const& projectVersion)
    {
      if (projectVersion == ProjectVersion::WOTLK)
        return std::string("Wrath Of The Lich King");
      if (projectVersion == ProjectVersion::SL)
        return std::string("Shadowlands");

      assert(false);
      return {};
    }

    std::string ClientVersionFactory::MapToClientDataVersion(ProjectVersion const& projectVersion)
    {
      if (projectVersion == ProjectVersion::WOTLK)
        return std::string("3.3.5.12340");
      if (projectVersion == ProjectVersion::SL)
        return std::string(Noggit::MapLightTarget::client_build_string);

      assert(false);
      return {};
    }

    NoggitProject::NoggitProject()
    {
      _projectWriter = std::make_shared<ApplicationProjectWriter>();
    }

    void NoggitProject::createBookmark(const NoggitProjectBookmarkMap& bookmark)
    {
      Bookmarks.push_back(bookmark);

      _projectWriter->saveProject(this, std::filesystem::path(ProjectPath));
    }

    void NoggitProject::deleteBookmark()
    {
    }

    void NoggitProject::pinMap(int map_id, const std::string& map_name)
    {
      auto pinnedMap = NoggitProjectPinnedMap();
      pinnedMap.MapName = map_name;
      pinnedMap.MapId = map_id;

      auto pinnedMapFound = std::find_if(std::begin(PinnedMaps), std::end(PinnedMaps),
        [&](Project::NoggitProjectPinnedMap pinnedMap)
        {
          return pinnedMap.MapId == map_id;
        });

      if (pinnedMapFound != std::end(PinnedMaps))
        return;

      PinnedMaps.push_back(pinnedMap);

      _projectWriter->saveProject(this, std::filesystem::path(ProjectPath));
    }

    void NoggitProject::unpinMap(int mapId)
    {
      PinnedMaps.erase(std::remove_if(PinnedMaps.begin(), PinnedMaps.end(),
        [=](NoggitProjectPinnedMap pinnedMap)
        {
          return pinnedMap.MapId == mapId;
        }),
        PinnedMaps.end());

      _projectWriter->saveProject(this, std::filesystem::path(ProjectPath));
    }

    void NoggitProject::saveTexturePalette(const NoggitProjectTexturePalette& new_texture_palette)
    {
      TexturePalettes.erase(std::remove_if(TexturePalettes.begin(), TexturePalettes.end(),
        [=](NoggitProjectTexturePalette texture_palette)
        {
          return texture_palette.MapId == new_texture_palette.MapId;
        }),
        TexturePalettes.end());

      TexturePalettes.push_back(new_texture_palette);

      _projectWriter->savePalettes(this, std::filesystem::path(ProjectPath));
    }

    void NoggitProject::saveObjectPalette(const NoggitProjectObjectPalette& new_object_palette)
    {
      ObjectPalettes.erase(std::remove_if(ObjectPalettes.begin(), ObjectPalettes.end(),
        [=](NoggitProjectObjectPalette obj_palette)
        {
          return obj_palette.MapId == new_object_palette.MapId;
        }),
        ObjectPalettes.end());

      ObjectPalettes.push_back(new_object_palette);

      _projectWriter->savePalettes(this, std::filesystem::path(ProjectPath));
    }

    void NoggitProject::saveObjectSelectionGroups(const NoggitProjectSelectionGroups& new_selection_groups)
    {
      ObjectSelectionGroups.erase(std::remove_if(ObjectSelectionGroups.begin(), ObjectSelectionGroups.end(),
        [=](NoggitProjectSelectionGroups proj_selection_group)
        {
          return proj_selection_group.MapId == new_selection_groups.MapId;
        }),
        ObjectSelectionGroups.end());

      ObjectSelectionGroups.push_back(new_selection_groups);

      _projectWriter->saveObjectSelectionGroups(this, std::filesystem::path(ProjectPath));
    }
};
