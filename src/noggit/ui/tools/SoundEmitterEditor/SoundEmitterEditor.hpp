// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <QWidget>

class QListWidget;
class MapView;
class MapChunk;
class QKeyEvent;

#include <functional>

namespace Noggit::Ui::Tools
{
  class SoundEmitterEditor final : public QWidget
  {
    Q_OBJECT

  public:
    explicit SoundEmitterEditor(::MapView* mapView, QWidget* parent = nullptr);

    void refreshFromWorld();

  protected:
    void keyPressEvent(QKeyEvent* event) override;

  private:
    ::MapView* _mapView = nullptr;
    MapChunk* _listed_chunk = nullptr;
    QListWidget* _emitterList = nullptr;
    std::function<void()> _deleteSelected;
    std::function<void()> _refresh_emitter_list;
    std::function<void()> _sync_selected_emitter_to_ui;
  };
}
