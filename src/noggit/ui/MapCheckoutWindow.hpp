// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/TileIndex.hpp>

#include <QtWidgets/QWidget>

class MapView;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace Noggit::Ui
{
  class minimap_widget;

  class MapCheckoutWindow : public QWidget
  {
    Q_OBJECT

  public:
    explicit MapCheckoutWindow(MapView* map_view, QWidget* parent = nullptr);

    void refreshFromManager();

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void rebuildCheckoutOverlays();
    void updateSelectionBounds();
    std::vector<TileIndex> selectedTiles() const;
    void syncSpinboxesFromSelection();

    MapView* _map_view;
    minimap_widget* _minimap = nullptr;
    std::vector<char> _selection;

    QLabel* _map_label = nullptr;
    QSpinBox* _adt_x = nullptr;
    QSpinBox* _adt_y = nullptr;
    QLineEdit* _selection_bounds = nullptr;
    QListWidget* _current_checkouts = nullptr;
    QLabel* _status_label = nullptr;

    QPushButton* _checkout_btn = nullptr;
    QPushButton* _checkin_btn = nullptr;
    QPushButton* _reset_btn = nullptr;
    QPushButton* _refresh_btn = nullptr;
  };
}
