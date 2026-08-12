// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/TileIndex.hpp>
#include <noggit/integrations/GitCommandRunner.hpp>

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class QWidget;

class World;

namespace Noggit::Integrations
{
  struct MapCheckoutEntry
  {
    std::size_t x = 0;
    std::size_t z = 0;
    std::string user;
    QDateTime checked_out_at;
  };

  struct MapCheckoutConfig
  {
    bool enabled = false;
    bool offline_mode = false;
    QString github_username;
    QString github_pat;
  };

  struct MapCheckoutOperationResult
  {
    bool success = false;
    QString message;
    QStringList blocked_tiles;
  };

  class MapCheckoutManager : public QObject
  {
    Q_OBJECT

  public:
    static MapCheckoutManager& instance();

    static MapCheckoutConfig loadConfigFromSettings();
    static std::optional<GitAuthCredentials> authFromSettings();

    bool isEnabled() const;
    bool isOfflineMode() const;
    bool isProjectReady() const;

    void applySettings();
    QString readinessMessage() const;

    void setActiveMap(int map_id, std::string const& basename);

    void startAutoSync(int interval_ms = 60000);
    void stopAutoSync();

    MapCheckoutOperationResult refresh();
    MapCheckoutOperationResult checkoutTiles(std::vector<TileIndex> const& tiles);
    MapCheckoutOperationResult checkInTiles(std::vector<TileIndex> const& tiles, World* world);

    MapCheckoutOperationResult commitMapSave(World* world);
    void commitMapSaveAsync(World* world);
    MapCheckoutOperationResult pushPendingChanges();
    MapCheckoutOperationResult forcePullMap(World* world, QWidget* parent);

    bool hasPendingPush() const { return _has_pending_push; }

    std::optional<std::string> ownerOf(TileIndex const& tile) const;
    bool canSaveTile(TileIndex const& tile) const;
    bool isOwnedByCurrentUser(TileIndex const& tile) const;

    std::vector<MapCheckoutEntry> checkoutsForMap() const;
    std::vector<MapCheckoutEntry> checkoutsForCurrentUser() const;

    QString lastError() const { return _last_error; }

    void beginSaveBatch();
    void recordSuccessfulSave(TileIndex const& tile);
    void recordMapLightFileSaved();
    void recordBlockedSave(TileIndex const& tile, std::string const& owner);
    void showBlockedSaveDialog(QWidget* parent) const;
    void clearBlockedSaves();

  signals:
    void checkoutsChanged();
    void remoteCheckoutsUpdated(QStringList messages);
    void mapSaveCommitFinished(bool success, QString message);

  private:
    MapCheckoutManager();

    static bool entriesEqual(std::vector<MapCheckoutEntry> const& a
                            , std::vector<MapCheckoutEntry> const& b);
    bool syncFromRemote(bool ff_only, bool silent);
    void kickAutoSync();
    void finishAutoSync(bool pull_ok, std::vector<MapCheckoutEntry> before);

    std::filesystem::path projectPath() const;
    std::filesystem::path manifestPath() const;
    std::filesystem::path manifestDirectory() const;

    bool loadManifestFromDisk();
    bool saveManifestToDisk();
    MapCheckoutOperationResult commitAndPushManifest(QString const& message);
    MapCheckoutOperationResult commitManifestAndFiles(QString const& message, QStringList const& extra_paths);
    MapCheckoutOperationResult commitFiles(QString const& message, QStringList const& paths, bool push);
    QStringList pathsForMapCommitScope() const;
    QStringList mergeCommitPaths(QStringList const& explicit_paths) const;
    QStringList pathsForSaveBatch(World* world) const;

    int _map_id = 0;
    std::string _basename;
    std::vector<MapCheckoutEntry> _entries;
    QString _last_error;
    mutable std::vector<std::pair<TileIndex, std::string>> _blocked_saves;
    bool _save_batch_had_writes = false;
    std::vector<TileIndex> _save_batch_tiles;
    bool _has_pending_push = false;
    QTimer* _auto_sync_timer = nullptr;
    bool _auto_sync_in_progress = false;
  };
}
