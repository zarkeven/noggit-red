// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/MapCheckoutWindow.hpp>
#include <noggit/TileIndex.hpp>
#include <noggit/MapHeaders.h>
#include <noggit/MapView.h>
#include <noggit/World.h>
#include <noggit/integrations/MapCheckoutManager.hpp>
#include <noggit/ui/minimap_widget.hpp>

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <QShowEvent>

namespace Noggit::Ui
{
  MapCheckoutWindow::MapCheckoutWindow(MapView* map_view, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , _map_view(map_view)
    , _selection(4096, 0)
  {
    setWindowTitle(tr("Map Checkout"));
    resize(960, 640);

    auto* root = new QHBoxLayout(this);

    auto* left = new QVBoxLayout();
    root->addLayout(left, 0);

    _map_label = new QLabel(this);
    left->addWidget(_map_label);

    auto* form = new QFormLayout();
    _adt_x = new QSpinBox(this);
    _adt_x->setRange(0, 63);
    _adt_y = new QSpinBox(this);
    _adt_y->setRange(0, 63);
    form->addRow(tr("ADT X"), _adt_x);
    form->addRow(tr("ADT Y"), _adt_y);
    left->addLayout(form);

    _selection_bounds = new QLineEdit(this);
    _selection_bounds->setReadOnly(true);
    left->addWidget(new QLabel(tr("Selection Coordinates"), this));
    left->addWidget(_selection_bounds);

    left->addWidget(new QLabel(tr("Current Checkouts"), this));
    _current_checkouts = new QListWidget(this);
    left->addWidget(_current_checkouts, 1);

    _status_label = new QLabel(this);
    _status_label->setWordWrap(true);
    left->addWidget(_status_label);

    _checkout_btn = new QPushButton(tr("Checkout"), this);
    _checkin_btn = new QPushButton(tr("Check In"), this);
    _reset_btn = new QPushButton(tr("Reset Selection"), this);
    _refresh_btn = new QPushButton(tr("Refresh"), this);

    left->addWidget(_checkout_btn);
    left->addWidget(_checkin_btn);
    left->addWidget(_reset_btn);
    left->addWidget(_refresh_btn);

    _minimap = new minimap_widget(this);
    _minimap->draw_boundaries(true);
    _minimap->set_use_orange_selection_overlay(true);
    _minimap->use_selection(&_selection);
    _minimap->setMinimumSize(512, 512);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(_minimap);
    root->addWidget(scroll, 1);

    connect(_checkout_btn, &QPushButton::clicked, this, [this]() {
      auto const tiles = selectedTiles();
      auto result = Noggit::Integrations::MapCheckoutManager::instance().checkoutTiles(tiles);
      if (!result.success)
      {
        QMessageBox::warning(this, tr("Checkout failed"), result.message + "\n" + result.blocked_tiles.join("\n"));
      }
      else
      {
        _status_label->setText(result.message);
      }
      refreshFromManager();
    });

    connect(_checkin_btn, &QPushButton::clicked, this, [this]() {
      auto const tiles = selectedTiles();
      auto result = Noggit::Integrations::MapCheckoutManager::instance().checkInTiles(tiles, _map_view->_world.get());
      if (!result.success)
      {
        QMessageBox::warning(this, tr("Check in failed"), result.message + "\n" + result.blocked_tiles.join("\n"));
      }
      else
      {
        _status_label->setText(result.message);
      }
      refreshFromManager();
    });

    connect(_reset_btn, &QPushButton::clicked, this, [this]() {
      std::fill(_selection.begin(), _selection.end(), 0);
      _minimap->update();
      updateSelectionBounds();
    });

    connect(_refresh_btn, &QPushButton::clicked, this, [this]() {
      auto result = Noggit::Integrations::MapCheckoutManager::instance().refresh();
      _status_label->setText(result.message);
      refreshFromManager();
    });

    connect(_minimap, &minimap_widget::tile_clicked, this, [this](QPoint const&) {
      syncSpinboxesFromSelection();
      updateSelectionBounds();
    });

    connect(_minimap, &minimap_widget::reset_selection, this, [this]() {
      updateSelectionBounds();
    });

    connect(&Noggit::Integrations::MapCheckoutManager::instance()
           , &Noggit::Integrations::MapCheckoutManager::checkoutsChanged
           , this
           , &MapCheckoutWindow::refreshFromManager);

    refreshFromManager();
  }

  void MapCheckoutWindow::showEvent(QShowEvent* event)
  {
    QWidget::showEvent(event);
    Noggit::Integrations::MapCheckoutManager::instance().refresh();
    refreshFromManager();
  }

  void MapCheckoutWindow::refreshFromManager()
  {
    if (!_map_view || !_map_view->_world)
    {
      return;
    }

    World* world = _map_view->_world.get();
    _minimap->world(world);

    auto& mgr = Noggit::Integrations::MapCheckoutManager::instance();
    mgr.setActiveMap(world->getMapID(), world->basename);

    _map_label->setText(QStringLiteral("%1 - %2")
      .arg(world->getMapID())
      .arg(QString::fromStdString(world->basename)));

    _status_label->setText(mgr.readinessMessage());

    _current_checkouts->clear();
    auto const cfg = Noggit::Integrations::MapCheckoutManager::loadConfigFromSettings();
    auto const owned = mgr.checkoutsForCurrentUser();
    if (owned.empty())
    {
      _current_checkouts->addItem(
        tr("No Current Checkouts for %1").arg(cfg.github_username.isEmpty() ? tr("GithubUsername") : cfg.github_username));
    }
    else
    {
      for (auto const& entry : owned)
      {
        _current_checkouts->addItem(QStringLiteral("ADT %1_%2").arg(entry.x).arg(entry.z));
      }
    }

    rebuildCheckoutOverlays();
    updateSelectionBounds();
  }

  void MapCheckoutWindow::rebuildCheckoutOverlays()
  {
    std::vector<minimap_widget::checkout_overlay_entry> overlays;
    for (auto const& entry : Noggit::Integrations::MapCheckoutManager::instance().checkoutsForMap())
    {
      overlays.push_back({entry.x, entry.z, entry.user});
    }
    _minimap->set_checkout_overlays(std::move(overlays));
  }

  void MapCheckoutWindow::updateSelectionBounds()
  {
    int min_x = 64;
    int min_z = 64;
    int max_x = -1;
    int max_z = -1;

    for (int z = 0; z < 64; ++z)
    {
      for (int x = 0; x < 64; ++x)
      {
        if (_selection[64 * x + z])
        {
          min_x = std::min(min_x, x);
          min_z = std::min(min_z, z);
          max_x = std::max(max_x, x);
          max_z = std::max(max_z, z);
        }
      }
    }

    if (max_x < 0)
    {
      _selection_bounds->clear();
      return;
    }

    float const x0 = min_x * TILESIZE;
    float const x1 = (max_x + 1) * TILESIZE;
    float const z0 = min_z * TILESIZE;
    float const z1 = (max_z + 1) * TILESIZE;

    _selection_bounds->setText(QStringLiteral("%1 to %2, %3 to %4")
      .arg(x0, 0, 'f', 1)
      .arg(x1, 0, 'f', 1)
      .arg(z0, 0, 'f', 1)
      .arg(z1, 0, 'f', 1));
  }

  std::vector<TileIndex> MapCheckoutWindow::selectedTiles() const
  {
    std::vector<TileIndex> tiles;
    for (int z = 0; z < 64; ++z)
    {
      for (int x = 0; x < 64; ++x)
      {
        if (_selection[64 * x + z])
        {
          tiles.emplace_back(static_cast<std::size_t>(x), static_cast<std::size_t>(z));
        }
      }
    }
    return tiles;
  }

  void MapCheckoutWindow::syncSpinboxesFromSelection()
  {
    for (int z = 0; z < 64; ++z)
    {
      for (int x = 0; x < 64; ++x)
      {
        if (_selection[64 * x + z])
        {
          _adt_x->setValue(x);
          _adt_y->setValue(z);
          return;
        }
      }
    }
  }
}
