// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ui/tools/ChunkManipulator/ChunkClipboard.hpp>

#include <QWidget>

class QListWidget;
class QSettings;
class MapView;

namespace Noggit::Ui::Tools::ChunkManipulator
{
  /// Named chunk selection groups: save/load from QSettings (JSON), independent of the copy cache.
  class ChunkGroupsWidget final : public QWidget
  {
    Q_OBJECT

  public:
    ChunkGroupsWidget(ChunkClipboard* clipboard, QSettings* settings, QString map_basename_key, MapView* map_view, QWidget* parent = nullptr);

  private slots:
    void onAddFromSelection();
    void onRemoveGroup();
    void onLoadGroup();
    void onClearSelection();

  private:
    void persist() const;
    void reloadList();

    ChunkClipboard* _clipboard;
    QSettings* _settings;
    QString const _map_key;
    MapView* _map_view;
    QListWidget* _list = nullptr;
  };
}
