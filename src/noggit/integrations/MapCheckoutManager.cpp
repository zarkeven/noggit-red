// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/integrations/MapCheckoutManager.hpp>
#include <noggit/Log.h>
#include <noggit/World.h>
#include <noggit/map_index.hpp>
#include <noggit/project/CurrentProject.hpp>

#include <ClientData.hpp>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtWidgets/QMessageBox>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace Noggit::Integrations
{
  namespace
  {
    std::optional<TileIndex> tileFromAdtPath(QString const& rel_path, QString const& basename)
    {
      QString normalized = rel_path;
      normalized.replace('\\', '/');

      QString const prefix = QStringLiteral("world/maps/%1/%2_").arg(basename, basename);
      if (!normalized.startsWith(prefix, Qt::CaseInsensitive))
      {
        return std::nullopt;
      }

      if (!normalized.endsWith(QStringLiteral(".adt"), Qt::CaseInsensitive))
      {
        return std::nullopt;
      }

      QString const coords = normalized.mid(prefix.size(), normalized.size() - prefix.size() - 4);
      QStringList const parts = coords.split('_');
      if (parts.size() != 2)
      {
        return std::nullopt;
      }

      bool ok_x = false;
      bool ok_z = false;
      int const x = parts[0].toInt(&ok_x);
      int const z = parts[1].toInt(&ok_z);
      if (!ok_x || !ok_z || x < 0 || z < 0)
      {
        return std::nullopt;
      }

      return TileIndex(static_cast<std::size_t>(x), static_cast<std::size_t>(z));
    }

    std::set<TileIndex> tilesFromDiffOutput(QString const& diff_output, QString const& basename)
    {
      std::set<TileIndex> tiles;
      for (QString const& line : diff_output.split('\n', Qt::SkipEmptyParts))
      {
        if (auto const tile = tileFromAdtPath(line.trimmed(), basename))
        {
          tiles.insert(*tile);
        }
      }
      return tiles;
    }

    std::set<TileIndex> intersectTileSets(std::set<TileIndex> const& a, std::set<TileIndex> const& b)
    {
      std::set<TileIndex> result;
      std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.begin()));
      return result;
    }

    QString mapDirectoryRelativePath(std::string const& basename)
    {
      std::stringstream filename;
      filename << "World\\Maps\\" << basename << "\\";
      return QString::fromStdString(
        BlizzardArchive::ClientData::normalizeFilenameInternal(filename.str()));
    }

    bool isGitNothingToCommit(GitCommandResult const& commit_result)
    {
      QString const combined = commit_result.standard_output + commit_result.standard_error;
      return combined.contains(QStringLiteral("nothing to commit"), Qt::CaseInsensitive)
          || combined.contains(QStringLiteral("nothing added to commit"), Qt::CaseInsensitive)
          || combined.contains(QStringLiteral("no changes added to commit"), Qt::CaseInsensitive);
    }

    std::mutex g_save_commit_mutex;

    MapCheckoutOperationResult runLocalCommit(std::filesystem::path const& root
                                             , QStringList const& paths
                                             , QString const& message)
    {
      MapCheckoutOperationResult result;

      if (paths.isEmpty())
      {
        result.message = QStringLiteral("Nothing to commit.");
        result.success = true;
        return result;
      }

      auto const add_result = GitCommandRunner::add(root, paths);
      if (!add_result.success)
      {
        result.message = add_result.standard_error.trimmed();
        if (result.message.isEmpty())
        {
          result.message = QStringLiteral("git add failed.");
        }
        return result;
      }

      auto const commit_result = GitCommandRunner::commit(root, message, paths);
      if (!commit_result.success)
      {
        if (isGitNothingToCommit(commit_result))
        {
          result.success = true;
          result.message = QStringLiteral("Nothing to commit.");
          return result;
        }

        result.message = commit_result.standard_error.trimmed();
        if (result.message.isEmpty())
        {
          result.message = commit_result.standard_output.trimmed();
        }
        if (result.message.isEmpty())
        {
          result.message = QStringLiteral("git commit failed.");
        }
        return result;
      }

      result.success = true;
      result.message = QStringLiteral("Changes committed locally.");
      return result;
    }
  }

  MapCheckoutManager& MapCheckoutManager::instance()
  {
    static MapCheckoutManager mgr;
    return mgr;
  }

  MapCheckoutManager::MapCheckoutManager()
    : QObject(nullptr)
  {
  }

  MapCheckoutConfig MapCheckoutManager::loadConfigFromSettings()
  {
    QSettings settings;
    MapCheckoutConfig cfg;
    cfg.enabled = settings.value(QStringLiteral("integrations/checkout_enabled"), false).toBool();
    cfg.offline_mode = settings.value(QStringLiteral("integrations/checkout_offline_mode"), false).toBool();
    cfg.github_username = settings.value(QStringLiteral("integrations/github_username")).toString().trimmed();
    cfg.github_pat = settings.value(QStringLiteral("integrations/github_pat")).toString();
    return cfg;
  }

  std::optional<GitAuthCredentials> MapCheckoutManager::authFromSettings()
  {
    auto const cfg = loadConfigFromSettings();
    if (cfg.github_pat.isEmpty())
    {
      return std::nullopt;
    }

    GitAuthCredentials auth;
    auth.username = cfg.github_username.isEmpty() ? QStringLiteral("git") : cfg.github_username;
    auth.personal_access_token = cfg.github_pat;
    return auth;
  }

  bool MapCheckoutManager::isEnabled() const
  {
    return loadConfigFromSettings().enabled;
  }

  bool MapCheckoutManager::isOfflineMode() const
  {
    return loadConfigFromSettings().offline_mode;
  }

  void MapCheckoutManager::applySettings()
  {
    if (_basename.empty() || !isProjectReady() || isOfflineMode())
    {
      stopAutoSync();
    }
    else
    {
      startAutoSync();
    }
  }

  bool MapCheckoutManager::isProjectReady() const
  {
    if (!isEnabled())
    {
      return false;
    }

    auto const cfg = loadConfigFromSettings();
    if (cfg.github_username.isEmpty())
    {
      return false;
    }

    return GitCommandRunner::isGitRepository(projectPath());
  }

  QString MapCheckoutManager::readinessMessage() const
  {
    auto const cfg = loadConfigFromSettings();
    if (!cfg.enabled)
    {
      return QStringLiteral("Map checkout is disabled in Settings.");
    }
    if (cfg.offline_mode)
    {
      return QStringLiteral("Offline mode — remote checkout sync and save blocking are disabled.");
    }
    if (cfg.github_username.isEmpty())
    {
      return QStringLiteral("Set your GitHub username in Settings → External Apps.");
    }
    if (!GitCommandRunner::isGitRepository(projectPath()))
    {
      return QStringLiteral("Project folder is not a git repository.");
    }
    if (auto const url = GitCommandRunner::originRemoteUrl(projectPath()))
    {
      return QStringLiteral("Ready — origin: %1").arg(QString::fromStdString(*url));
    }
    return QStringLiteral("Ready — no origin remote configured.");
  }

  void MapCheckoutManager::setActiveMap(int map_id, std::string const& basename)
  {
    _map_id = map_id;
    _basename = basename;
    loadManifestFromDisk();

    applySettings();
  }

  void MapCheckoutManager::startAutoSync(int interval_ms)
  {
    if (!_auto_sync_timer)
    {
      _auto_sync_timer = new QTimer(this);
      connect(_auto_sync_timer, &QTimer::timeout, this, [this]() {
        kickAutoSync();
      });
    }

    if (isProjectReady() && !_basename.empty() && !isOfflineMode())
    {
      _auto_sync_timer->start(interval_ms);
    }
  }

  void MapCheckoutManager::kickAutoSync()
  {
    if (_auto_sync_in_progress || !isProjectReady() || _basename.empty() || isOfflineMode())
    {
      return;
    }

    _auto_sync_in_progress = true;

    auto const root = projectPath();
    auto const auth = authFromSettings();
    auto const before = _entries;

    std::thread([this, root, auth, before]() {
      GitCommandResult pull_result;
      {
        std::lock_guard const lock(g_save_commit_mutex);
        pull_result = GitCommandRunner::pull(root, true, auth);
      }

      QTimer::singleShot(0, this, [this, pull_result, before]() {
        finishAutoSync(pull_result.success, before);
      });
    }).detach();
  }

  void MapCheckoutManager::finishAutoSync(bool pull_ok, std::vector<MapCheckoutEntry> before)
  {
    _auto_sync_in_progress = false;

    if (!pull_ok || !loadManifestFromDisk())
    {
      return;
    }

    if (entriesEqual(before, _entries))
    {
      return;
    }

    emit checkoutsChanged();

    auto const cfg = loadConfigFromSettings();
    std::string const current_user = cfg.github_username.toStdString();

    auto owner_before = [&](std::size_t x, std::size_t z) -> std::optional<std::string> {
      for (MapCheckoutEntry const& entry : before)
      {
        if (entry.x == x && entry.z == z)
        {
          return entry.user;
        }
      }
      return std::nullopt;
    };

    QStringList notifications;
    for (MapCheckoutEntry const& entry : _entries)
    {
      if (entry.user == current_user)
      {
        continue;
      }

      auto const previous_owner = owner_before(entry.x, entry.z);
      if (!previous_owner || *previous_owner != entry.user)
      {
        notifications.append(QStringLiteral("ADT %1_%2 checked out by %3")
          .arg(entry.x)
          .arg(entry.z)
          .arg(QString::fromStdString(entry.user)));
      }
    }

    if (!notifications.isEmpty())
    {
      emit remoteCheckoutsUpdated(notifications);
    }
  }

  void MapCheckoutManager::stopAutoSync()
  {
    if (_auto_sync_timer)
    {
      _auto_sync_timer->stop();
    }
  }

  bool MapCheckoutManager::entriesEqual(std::vector<MapCheckoutEntry> const& a
                                       , std::vector<MapCheckoutEntry> const& b)
  {
    if (a.size() != b.size())
    {
      return false;
    }

    auto sorted = [](std::vector<MapCheckoutEntry> entries) {
      std::sort(entries.begin(), entries.end(), [](MapCheckoutEntry const& lhs, MapCheckoutEntry const& rhs) {
        if (lhs.x != rhs.x)
        {
          return lhs.x < rhs.x;
        }
        if (lhs.z != rhs.z)
        {
          return lhs.z < rhs.z;
        }
        return lhs.user < rhs.user;
      });
      return entries;
    };

    auto const sorted_a = sorted(a);
    auto const sorted_b = sorted(b);

    for (std::size_t i = 0; i < sorted_a.size(); ++i)
    {
      MapCheckoutEntry const& lhs = sorted_a[i];
      MapCheckoutEntry const& rhs = sorted_b[i];
      if (lhs.x != rhs.x || lhs.z != rhs.z || lhs.user != rhs.user)
      {
        return false;
      }
    }

    return true;
  }

  bool MapCheckoutManager::syncFromRemote(bool ff_only, bool silent)
  {
    if (!isProjectReady() || _basename.empty() || isOfflineMode())
    {
      return false;
    }

    auto const before = _entries;

    auto const pull_result = GitCommandRunner::pull(projectPath(), ff_only, authFromSettings());
    if (!pull_result.success)
    {
      if (!silent)
      {
        _last_error = pull_result.standard_error.trimmed();
      }
      return false;
    }

    if (!loadManifestFromDisk())
    {
      if (!silent)
      {
        // _last_error set by loadManifestFromDisk
      }
      return false;
    }

    if (!entriesEqual(before, _entries))
    {
      emit checkoutsChanged();

      auto const cfg = loadConfigFromSettings();
      std::string const current_user = cfg.github_username.toStdString();

      auto owner_before = [&](std::size_t x, std::size_t z) -> std::optional<std::string> {
        for (MapCheckoutEntry const& entry : before)
        {
          if (entry.x == x && entry.z == z)
          {
            return entry.user;
          }
        }
        return std::nullopt;
      };

      QStringList notifications;
      for (MapCheckoutEntry const& entry : _entries)
      {
        if (entry.user == current_user)
        {
          continue;
        }

        auto const previous_owner = owner_before(entry.x, entry.z);
        if (!previous_owner || *previous_owner != entry.user)
        {
          notifications.append(QStringLiteral("ADT %1_%2 checked out by %3")
            .arg(entry.x)
            .arg(entry.z)
            .arg(QString::fromStdString(entry.user)));
        }
      }

      if (!notifications.isEmpty())
      {
        emit remoteCheckoutsUpdated(notifications);
      }
    }

    return true;
  }

  std::filesystem::path MapCheckoutManager::projectPath() const
  {
    return std::filesystem::path(Noggit::Project::CurrentProject::get()->ProjectPath);
  }

  std::filesystem::path MapCheckoutManager::manifestDirectory() const
  {
    return projectPath() / ".noggit" / "checkouts";
  }

  std::filesystem::path MapCheckoutManager::manifestPath() const
  {
    return manifestDirectory() / (_basename + ".json");
  }

  bool MapCheckoutManager::loadManifestFromDisk()
  {
    _entries.clear();

    if (_basename.empty())
    {
      return true;
    }

    std::error_code ec;
    auto const path = manifestPath();
    if (!std::filesystem::exists(path, ec))
    {
      return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      _last_error = QStringLiteral("Failed to read checkout manifest.");
      return false;
    }

    QByteArray const data = QByteArray::fromStdString(std::string((std::istreambuf_iterator<char>(file))
                                                                 , std::istreambuf_iterator<char>()));

    QJsonParseError parse_error;
    QJsonDocument const doc = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
    {
      _last_error = QStringLiteral("Invalid checkout manifest JSON: %1").arg(parse_error.errorString());
      return false;
    }

    QJsonObject const root = doc.object();
    QJsonArray const checkouts = root.value(QStringLiteral("checkouts")).toArray();
    for (QJsonValue const& value : checkouts)
    {
      QJsonObject const obj = value.toObject();
      MapCheckoutEntry entry;
      entry.x = static_cast<std::size_t>(obj.value(QStringLiteral("x")).toInt());
      entry.z = static_cast<std::size_t>(obj.value(QStringLiteral("z")).toInt());
      entry.user = obj.value(QStringLiteral("user")).toString().toStdString();
      entry.checked_out_at = QDateTime::fromString(obj.value(QStringLiteral("checked_out_at")).toString(), Qt::ISODate);
      _entries.push_back(entry);
    }

    return true;
  }

  bool MapCheckoutManager::saveManifestToDisk()
  {
    std::error_code ec;
    std::filesystem::create_directories(manifestDirectory(), ec);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("map_id"), _map_id);

    QJsonArray checkouts;
    for (MapCheckoutEntry const& entry : _entries)
    {
      QJsonObject obj;
      obj.insert(QStringLiteral("x"), static_cast<int>(entry.x));
      obj.insert(QStringLiteral("z"), static_cast<int>(entry.z));
      obj.insert(QStringLiteral("user"), QString::fromStdString(entry.user));
      obj.insert(QStringLiteral("checked_out_at"), entry.checked_out_at.toUTC().toString(Qt::ISODate));
      checkouts.append(obj);
    }
    root.insert(QStringLiteral("checkouts"), checkouts);

    QJsonDocument doc(root);
    QByteArray const json = doc.toJson(QJsonDocument::Indented);

    std::ofstream file(manifestPath(), std::ios::binary | std::ios::trunc);
    if (!file)
    {
      _last_error = QStringLiteral("Failed to write checkout manifest.");
      return false;
    }

    file.write(json.constData(), json.size());
    return true;
  }

  MapCheckoutOperationResult MapCheckoutManager::commitAndPushManifest(QString const& message)
  {
    return commitManifestAndFiles(message, {});
  }

  MapCheckoutOperationResult MapCheckoutManager::commitManifestAndFiles(QString const& message
                                                                       , QStringList const& extra_paths)
  {
    auto const root = projectPath();

    QString const manifest_rel = QString::fromStdString(
      std::filesystem::relative(manifestPath(), root).generic_string());

    QStringList paths {manifest_rel};
    paths << extra_paths;

    return commitFiles(message, paths, true);
  }

  MapCheckoutOperationResult MapCheckoutManager::commitFiles(QString const& message
                                                            , QStringList const& paths
                                                            , bool push)
  {
    MapCheckoutOperationResult result;
    auto const root = projectPath();
    QStringList const staging_paths = mergeCommitPaths(paths);

    {
      std::lock_guard const lock(g_save_commit_mutex);
      result = runLocalCommit(root, staging_paths, message);
    }

    if (!result.success)
    {
      _last_error = result.message;
      return result;
    }

    if (result.message == QStringLiteral("Nothing to commit."))
    {
      result.success = true;
      return result;
    }

    if (!push)
    {
      _has_pending_push = true;
      return result;
    }

    auto const push_result = GitCommandRunner::push(root, authFromSettings());
    if (!push_result.success)
    {
      result.message = push_result.standard_error.trimmed();
      if (result.message.isEmpty())
      {
        result.message = QStringLiteral("git push failed.");
      }
      _last_error = result.message;
      return result;
    }

    result.success = true;
    result.message = QStringLiteral("Changes pushed to remote.");
    return result;
  }

  QStringList MapCheckoutManager::pathsForMapCommitScope() const
  {
    QStringList paths;
    if (_basename.empty())
    {
      return paths;
    }

    paths << mapDirectoryRelativePath(_basename);
    paths << QString::fromStdString(
      std::filesystem::relative(manifestPath(), projectPath()).generic_string());
    return paths;
  }

  QStringList MapCheckoutManager::mergeCommitPaths(QStringList const& explicit_paths) const
  {
    std::set<QString> unique_paths;
    for (QString const& path : explicit_paths)
    {
      unique_paths.insert(path);
    }
    for (QString const& path : pathsForMapCommitScope())
    {
      unique_paths.insert(path);
    }

    QStringList merged;
    merged.reserve(static_cast<int>(unique_paths.size()));
    for (QString const& path : unique_paths)
    {
      merged << path;
    }
    return merged;
  }

  QStringList MapCheckoutManager::pathsForSaveBatch(World* world) const
  {
    Q_UNUSED(world);
    return {};
  }

  MapCheckoutOperationResult MapCheckoutManager::commitMapSave(World* world)
  {
    MapCheckoutOperationResult result;

    if (!world || !GitCommandRunner::isGitRepository(projectPath()))
    {
      result.success = true;
      return result;
    }

    if (!_save_batch_had_writes)
    {
      result.success = true;
      return result;
    }

    auto const cfg = loadConfigFromSettings();
    QString const author = cfg.github_username.isEmpty()
      ? QStringLiteral("noggit")
      : cfg.github_username;

    return commitFiles(
      QStringLiteral("save: %1 %2").arg(author, QString::fromStdString(world->basename))
      , pathsForSaveBatch(world)
      , false);
  }

  void MapCheckoutManager::commitMapSaveAsync(World* world)
  {
    if (!world || !GitCommandRunner::isGitRepository(projectPath()) || !_save_batch_had_writes)
    {
      return;
    }

    auto const cfg = loadConfigFromSettings();
    QString const author = cfg.github_username.isEmpty()
      ? QStringLiteral("noggit")
      : cfg.github_username;

    QString const message = QStringLiteral("save: %1 %2").arg(author, QString::fromStdString(world->basename));
    QStringList const paths = mergeCommitPaths(pathsForSaveBatch(world));
    auto const root = projectPath();

    std::thread([this, root, paths, message]() {
      MapCheckoutOperationResult result;
      {
        std::lock_guard const lock(g_save_commit_mutex);
        result = runLocalCommit(root, paths, message);
      }

      QTimer::singleShot(0, this, [this, result]() {
        if (result.success)
        {
          if (result.message == QStringLiteral("Changes committed locally."))
          {
            _has_pending_push = true;
          }
        }
        else
        {
          _last_error = result.message;
          LogError << "Git commit after save failed: " << result.message.toStdString() << std::endl;
        }

        emit mapSaveCommitFinished(result.success, result.message);
      });
    }).detach();
  }

  MapCheckoutOperationResult MapCheckoutManager::pushPendingChanges()
  {
    MapCheckoutOperationResult result;

    std::lock_guard const commit_lock(g_save_commit_mutex);

    if (!GitCommandRunner::isGitRepository(projectPath()))
    {
      result.message = QStringLiteral("Project folder is not a git repository.");
      _last_error = result.message;
      return result;
    }

    if (isOfflineMode())
    {
      result.success = true;
      result.message = QStringLiteral("Offline mode — push skipped.");
      return result;
    }

    auto const push_result = GitCommandRunner::push(projectPath(), authFromSettings());
    if (!push_result.success)
    {
      result.message = push_result.standard_error.trimmed();
      if (result.message.isEmpty())
      {
        result.message = QStringLiteral("git push failed.");
      }
      _last_error = result.message;
      return result;
    }

    _has_pending_push = false;
    result.success = true;
    result.message = QStringLiteral("Changes pushed to remote.");
    return result;
  }

  MapCheckoutOperationResult MapCheckoutManager::forcePullMap(World* world, QWidget* parent)
  {
    MapCheckoutOperationResult result;

    if (!world)
    {
      result.message = QStringLiteral("No map loaded.");
      return result;
    }

    if (!GitCommandRunner::isGitRepository(projectPath()))
    {
      result.message = QStringLiteral("Project folder is not a git repository.");
      _last_error = result.message;
      return result;
    }

    if (isOfflineMode())
    {
      result.message = QStringLiteral("Offline mode — force pull is disabled.");
      _last_error = result.message;
      return result;
    }

    auto const root = projectPath();
    auto const fetch_result = GitCommandRunner::fetch(root, authFromSettings());
    if (!fetch_result.success)
    {
      result.message = fetch_result.standard_error.trimmed();
      if (result.message.isEmpty())
      {
        result.message = QStringLiteral("git fetch failed.");
      }
      _last_error = result.message;
      return result;
    }

    auto const upstream = GitCommandRunner::upstreamRef(root);
    if (!upstream)
    {
      result.message = QStringLiteral("No upstream branch configured for the current branch.");
      _last_error = result.message;
      return result;
    }

    QString const upstream_q = QString::fromStdString(*upstream);
    QString const basename = QString::fromStdString(world->basename);

    auto const remote_changed = tilesFromDiffOutput(
      GitCommandRunner::diffNameOnly(root, QStringLiteral("HEAD"), upstream_q).standard_output
      , basename);

    auto const unpushed_committed = tilesFromDiffOutput(
      GitCommandRunner::diffNameOnly(root, upstream_q, QStringLiteral("HEAD")).standard_output
      , basename);

    std::set<TileIndex> local_at_risk = unpushed_committed;
    for (std::size_t x = 0; x < 64; ++x)
    {
      for (std::size_t z = 0; z < 64; ++z)
      {
        TileIndex const tile(x, z);
        if (world->mapIndex.tileLoaded(tile) && world->mapIndex.has_unsaved_changes(tile))
        {
          local_at_risk.insert(tile);
        }
      }
    }

    auto const conflicts = intersectTileSets(remote_changed, local_at_risk);
    if (!conflicts.empty() && parent)
    {
      QStringList conflict_lines;
      for (TileIndex const& tile : conflicts)
      {
        conflict_lines.append(QStringLiteral("tile %1,%2").arg(tile.x).arg(tile.z));
      }

      QMessageBox dialog(parent);
      dialog.setIcon(QMessageBox::Warning);
      dialog.setWindowTitle(QStringLiteral("Force Pull"));
      dialog.setText(QStringLiteral(
        "There are unpushed changes on %1.\n\n"
        "If you pull from the repo, those changes will be overwritten.\n\n"
        "Proceed?")
        .arg(conflict_lines.join(QStringLiteral(", "))));
      dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      dialog.setDefaultButton(QMessageBox::No);

      if (dialog.exec() != QMessageBox::Yes)
      {
        result.message = QStringLiteral("Force pull cancelled.");
        return result;
      }
    }

    auto const reset_result = GitCommandRunner::resetHard(root, upstream_q);
    if (!reset_result.success)
    {
      result.message = reset_result.standard_error.trimmed();
      if (result.message.isEmpty())
      {
        result.message = QStringLiteral("git reset --hard failed.");
      }
      _last_error = result.message;
      return result;
    }

    _has_pending_push = false;

    int reloaded = 0;
    for (TileIndex const& tile : remote_changed)
    {
      if (world->mapIndex.tileLoaded(tile))
      {
        world->reload_tile(tile);
        ++reloaded;
      }
    }

    refresh();

    result.success = true;
    result.message = QStringLiteral("Force pull complete — reloaded %1 tile(s).").arg(reloaded);
    return result;
  }

  MapCheckoutOperationResult MapCheckoutManager::refresh()
  {
    MapCheckoutOperationResult result;

    if (!isProjectReady())
    {
      result.message = readinessMessage();
      _last_error = result.message;
      return result;
    }

    if (isOfflineMode())
    {
      if (!loadManifestFromDisk())
      {
        result.message = _last_error;
        return result;
      }

      result.success = true;
      result.message = QStringLiteral("Loaded local checkouts (offline mode).");
      emit checkoutsChanged();
      return result;
    }

    if (!syncFromRemote(false, false))
    {
      result.message = _last_error.isEmpty()
        ? QStringLiteral("git pull failed.")
        : _last_error;
      return result;
    }

    result.success = true;
    result.message = QStringLiteral("Checkouts refreshed.");
    emit checkoutsChanged();
    return result;
  }

  MapCheckoutOperationResult MapCheckoutManager::checkoutTiles(std::vector<TileIndex> const& tiles)
  {
    MapCheckoutOperationResult result;

    if (!isProjectReady())
    {
      result.message = readinessMessage();
      _last_error = result.message;
      return result;
    }

    if (tiles.empty())
    {
      result.message = QStringLiteral("No ADTs selected.");
      return result;
    }

    auto const cfg = loadConfigFromSettings();
    std::string const current_user = cfg.github_username.toStdString();

    auto const pull_result = GitCommandRunner::pull(projectPath(), true, authFromSettings());
    if (!pull_result.success)
    {
      result.message = pull_result.standard_error.trimmed();
      if (result.message.isEmpty())
      {
        result.message = QStringLiteral("git pull failed. Resolve conflicts and try again.");
      }
      _last_error = result.message;
      return result;
    }

    if (!loadManifestFromDisk())
    {
      result.message = _last_error;
      return result;
    }

    if (!isOfflineMode())
    {
      for (TileIndex const& tile : tiles)
      {
        if (auto const owner = ownerOf(tile))
        {
          if (*owner != current_user)
          {
            result.blocked_tiles.append(QStringLiteral("ADT %1_%2 (checked out by %3)")
              .arg(tile.x).arg(tile.z).arg(QString::fromStdString(*owner)));
          }
        }
      }

      if (!result.blocked_tiles.isEmpty())
      {
        result.message = QStringLiteral("Some tiles are already checked out by another user.");
        return result;
      }
    }

    QDateTime const now = QDateTime::currentDateTimeUtc();
    for (TileIndex const& tile : tiles)
    {
      auto it = std::find_if(_entries.begin(), _entries.end(), [&](MapCheckoutEntry const& e) {
        return e.x == tile.x && e.z == tile.z;
      });

      if (it == _entries.end())
      {
        _entries.push_back({tile.x, tile.z, current_user, now});
      }
      else
      {
        it->user = current_user;
        it->checked_out_at = now;
      }
    }

    if (!saveManifestToDisk())
    {
      result.message = _last_error;
      return result;
    }

    QStringList tile_names;
    for (TileIndex const& tile : tiles)
    {
      tile_names.append(QStringLiteral("%1_%2").arg(tile.x).arg(tile.z));
    }

    auto const commit_result = commitAndPushManifest(
      QStringLiteral("checkout: %1 %2 [%3]")
        .arg(cfg.github_username)
        .arg(QString::fromStdString(_basename))
        .arg(tile_names.join(QStringLiteral(", "))));

    if (!commit_result.success)
    {
      result.message = commit_result.message;
      return result;
    }

    result.success = true;
    result.message = QStringLiteral("Checked out %1 ADT(s).").arg(tiles.size());
    emit checkoutsChanged();
    return result;
  }

  MapCheckoutOperationResult MapCheckoutManager::checkInTiles(std::vector<TileIndex> const& tiles, World* world)
  {
    MapCheckoutOperationResult result;

    if (!isProjectReady())
    {
      result.message = readinessMessage();
      _last_error = result.message;
      return result;
    }

    if (tiles.empty())
    {
      result.message = QStringLiteral("No ADTs selected.");
      return result;
    }

    auto const cfg = loadConfigFromSettings();
    std::string const current_user = cfg.github_username.toStdString();

    for (TileIndex const& tile : tiles)
    {
      auto const owner = ownerOf(tile);
      if (!owner || *owner != current_user)
      {
        result.blocked_tiles.append(QStringLiteral("ADT %1_%2").arg(tile.x).arg(tile.z));
      }
    }

    if (!result.blocked_tiles.isEmpty())
    {
      result.message = QStringLiteral("You can only check in ADTs checked out by you.");
      return result;
    }

    auto const pull_result = GitCommandRunner::pull(projectPath(), false, authFromSettings());
    if (!pull_result.success)
    {
      result.message = pull_result.standard_error.trimmed();
      _last_error = result.message;
      return result;
    }

    if (!loadManifestFromDisk())
    {
      result.message = _last_error;
      return result;
    }

    for (TileIndex const& tile : tiles)
    {
      if (world && world->mapIndex.tileLoaded(tile) && world->mapIndex.has_unsaved_changes(tile))
      {
        world->mapIndex.saveTile(tile, world, false);
      }
    }

    QStringList adt_paths;
    for (TileIndex const& tile : tiles)
    {
      std::stringstream filename;
      filename << "World\\Maps\\" << _basename << "\\" << _basename << "_" << tile.x << "_" << tile.z << ".adt";
      auto const rel = std::filesystem::relative(
        std::filesystem::path(Noggit::Project::CurrentProject::get()->ProjectPath)
          / BlizzardArchive::ClientData::normalizeFilenameInternal(filename.str())
        , projectPath()).generic_string();
      adt_paths.append(QString::fromStdString(rel));

      _entries.erase(std::remove_if(_entries.begin(), _entries.end(), [&](MapCheckoutEntry const& e) {
        return e.x == tile.x && e.z == tile.z;
      }), _entries.end());
    }

    if (!saveManifestToDisk())
    {
      result.message = _last_error;
      return result;
    }

    auto const commit_result = commitManifestAndFiles(
      QStringLiteral("checkin: %1 %2").arg(cfg.github_username).arg(QString::fromStdString(_basename))
      , adt_paths);

    if (!commit_result.success)
    {
      result.message = commit_result.message;
      return result;
    }

    result.success = true;
    result.message = QStringLiteral("Checked in %1 ADT(s).").arg(tiles.size());
    emit checkoutsChanged();
    return result;
  }

  std::optional<std::string> MapCheckoutManager::ownerOf(TileIndex const& tile) const
  {
    for (MapCheckoutEntry const& entry : _entries)
    {
      if (entry.x == tile.x && entry.z == tile.z)
      {
        return entry.user;
      }
    }
    return std::nullopt;
  }

  bool MapCheckoutManager::canSaveTile(TileIndex const& tile) const
  {
    if (!isEnabled() || isOfflineMode())
    {
      return true;
    }

    auto const cfg = loadConfigFromSettings();
    if (cfg.github_username.isEmpty())
    {
      return true;
    }

    if (auto const owner = ownerOf(tile))
    {
      return *owner == cfg.github_username.toStdString();
    }

    return true;
  }

  bool MapCheckoutManager::isOwnedByCurrentUser(TileIndex const& tile) const
  {
    auto const cfg = loadConfigFromSettings();
    if (auto const owner = ownerOf(tile))
    {
      return *owner == cfg.github_username.toStdString();
    }
    return false;
  }

  std::vector<MapCheckoutEntry> MapCheckoutManager::checkoutsForMap() const
  {
    return _entries;
  }

  std::vector<MapCheckoutEntry> MapCheckoutManager::checkoutsForCurrentUser() const
  {
    auto const cfg = loadConfigFromSettings();
    std::string const user = cfg.github_username.toStdString();
    std::vector<MapCheckoutEntry> owned;
    for (MapCheckoutEntry const& entry : _entries)
    {
      if (entry.user == user)
      {
        owned.push_back(entry);
      }
    }
    return owned;
  }

  void MapCheckoutManager::beginSaveBatch()
  {
    _blocked_saves.clear();
    _save_batch_had_writes = false;
    _save_batch_tiles.clear();
  }

  void MapCheckoutManager::recordSuccessfulSave(TileIndex const& tile)
  {
    _save_batch_had_writes = true;
    _save_batch_tiles.push_back(tile);
  }

  void MapCheckoutManager::recordMapLightFileSaved()
  {
    _save_batch_had_writes = true;
  }

  void MapCheckoutManager::recordBlockedSave(TileIndex const& tile, std::string const& owner)
  {
    _blocked_saves.emplace_back(tile, owner);
    Log << "ADT " << tile.x << "_" << tile.z
        << " will not be saved. It is currently checked out by " << owner << "." << std::endl;
  }

  void MapCheckoutManager::showBlockedSaveDialog(QWidget* parent) const
  {
    if (_blocked_saves.empty())
    {
      return;
    }

    QStringList lines;
    for (auto const& [tile, owner] : _blocked_saves)
    {
      lines.append(QStringLiteral("ADT %1_%2 will not be saved. It is currently checked out by %3.")
        .arg(tile.x).arg(tile.z).arg(QString::fromStdString(owner)));
    }

    QMessageBox::warning(parent
                        , QStringLiteral("Save blocked")
                        , lines.join('\n'));
  }

  void MapCheckoutManager::clearBlockedSaves()
  {
    _blocked_saves.clear();
  }
}
