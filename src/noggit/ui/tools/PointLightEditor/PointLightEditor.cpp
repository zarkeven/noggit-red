// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "PointLightEditor.hpp"

#include <noggit/MapView.h>
#include <noggit/World.h>
#include <noggit/tools/PointLightTool.hpp>
#include <noggit/ui/tools/PointLightEditor/PointLightClipboard.hpp>
#include <noggit/ui/tools/PointLightEditor/PointLightPropertyDialog.hpp>

#include <math/coordinates.hpp>

#include <QAbstractItemView>
#include <QColor>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Noggit::Ui::Tools;

namespace
{
  constexpr int kColGrid = 0;
  constexpr int kColServer = 1;
  constexpr int kColType = 2;
  constexpr int kColCount = 3;

  QIcon colorSwatchIcon(glm::vec3 const& color)
  {
    QPixmap pix(16, 16);
    pix.fill(QColor::fromRgbF(color.r, color.g, color.b));
    return QIcon(pix);
  }

  QString lightTypeLabel(World::MapLightType type)
  {
    return type == World::MapLightType::Spot ? QStringLiteral("Spot") : QStringLiteral("Point");
  }

  //! Numeric / composite sort via Qt::UserRole+1 while DisplayRole stays human-readable.
  class SortableTableItem final : public QTableWidgetItem
  {
  public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(QTableWidgetItem const& other) const override
    {
      QVariant const a = data(Qt::UserRole + 1);
      QVariant const b = other.data(Qt::UserRole + 1);
      if (a.isValid() && b.isValid())
      {
        if (a.userType() == QMetaType::QString)
          return a.toString() < b.toString();
        return a.toDouble() < b.toDouble();
      }
      return text() < other.text();
    }
  };

  void setRowForLight(QTableWidget* table, int row, std::size_t world_index, World::PointLight const& light)
  {
    glm::vec3 const server = math::to_server(light.position.x, light.position.y, light.position.z);
    int const sx = static_cast<int>(std::lround(server.x));
    int const sy = static_cast<int>(std::lround(server.y));
    int const sz = static_cast<int>(std::lround(server.z));

    auto* grid = new SortableTableItem(
      QStringLiteral("%1,%2").arg(light.tile_x).arg(light.tile_y));
    grid->setIcon(colorSwatchIcon(light.color));
    grid->setData(Qt::UserRole, QVariant::fromValue(static_cast<qulonglong>(world_index)));
    // Pack tile_x << 16 | tile_y for stable numeric sort.
    grid->setData(Qt::UserRole + 1,
                  (static_cast<qlonglong>(light.tile_x) << 16) | static_cast<qlonglong>(light.tile_y));
    grid->setFlags(grid->flags() & ~Qt::ItemIsEditable);

    auto* server_item = new SortableTableItem(
      QStringLiteral("%1, %2, %3").arg(sx).arg(sy).arg(sz));
    // Sort primarily by server X, then Y, then Z.
    server_item->setData(Qt::UserRole + 1,
                         static_cast<double>(sx) * 1e10 + static_cast<double>(sy) * 1e5
                           + static_cast<double>(sz));
    server_item->setFlags(server_item->flags() & ~Qt::ItemIsEditable);

    auto* type_item = new SortableTableItem(lightTypeLabel(light.light_type));
    type_item->setData(Qt::UserRole + 1, lightTypeLabel(light.light_type));
    type_item->setFlags(type_item->flags() & ~Qt::ItemIsEditable);

    table->setItem(row, kColGrid, grid);
    table->setItem(row, kColServer, server_item);
    table->setItem(row, kColType, type_item);
  }
}

PointLightEditor::PointLightEditor(Noggit::PointLightTool* tool, ::MapView* mapView, QWidget* parent)
  : QWidget(parent)
  , _tool(tool)
  , _mapView(mapView)
{
  setMinimumWidth(250);

  auto* root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(8);

  auto* panel_box = new QGroupBox(tr("Light Creation Tool"), this);
  panel_box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  root_layout->addWidget(panel_box, 1);

  auto* panel_layout = new QVBoxLayout(panel_box);
  panel_layout->setContentsMargins(6, 6, 6, 6);
  panel_layout->setSpacing(6);

  _lightTable = new QTableWidget(0, kColCount, panel_box);
  _lightTable->setHorizontalHeaderLabels({ tr("gridx,gridy"), tr("Server coord"), tr("Type") });
  _lightTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  _lightTable->setSelectionMode(QAbstractItemView::SingleSelection);
  _lightTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  _lightTable->setSortingEnabled(true);
  _lightTable->verticalHeader()->setVisible(false);
  _lightTable->horizontalHeader()->setStretchLastSection(true);
  _lightTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  _lightTable->setIconSize(QSize(16, 16));
  panel_layout->addWidget(_lightTable, 1);

  auto* edit_btn = new QPushButton(tr("Edit selected light"), panel_box);
  auto* create_btn = new QPushButton(tr("+ Create Light"), panel_box);
  auto* delete_btn = new QPushButton(tr("Delete selected light"), panel_box);
  auto* port_btn = new QPushButton(tr("Port to light (IN DEV)"), panel_box);

  port_btn->setEnabled(false);
  port_btn->setToolTip(tr("Not implemented yet."));

  panel_layout->addWidget(edit_btn);
  panel_layout->addWidget(create_btn);
  panel_layout->addWidget(delete_btn);
  panel_layout->addWidget(port_btn);

  QObject::connect(_lightTable, &QTableWidget::itemSelectionChanged, [this]()
  {
    _mapView->flushPointLightPropertyUndoBatch();
    if (_lightTable->selectedItems().isEmpty() || _lightTable->currentRow() < 0)
    {
      _mapView->getWorld()->selectedPointLightIndex(std::nullopt);
      _mapView->clearPointLightPropertyEditFallback();
      _mapView->invalidate();
      return;
    }

    if (QTableWidgetItem* item = _lightTable->item(_lightTable->currentRow(), kColGrid))
    {
      syncListSelectionToWorld(
        static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong()));
    }
  });

  QObject::connect(_lightTable, &QTableWidget::itemDoubleClicked, [this](QTableWidgetItem*)
  {
    if (auto const idx = selectedLightIndex())
      openPropertyDialog(*idx);
  });

  QObject::connect(edit_btn, &QPushButton::clicked, [this]()
  {
    if (auto const idx = selectedLightIndex())
      openPropertyDialog(*idx);
    else
      QMessageBox::information(this, tr("Point lights"), tr("Select a light in the list first."));
  });

  QObject::connect(create_btn, &QPushButton::clicked, [this]()
  {
    if (_mapView->displayMode() != display_mode::in_3D)
    {
      QMessageBox::warning(this, tr("Point lights"), tr("Switch to 3D mode to place lights."));
      return;
    }

    glm::vec3 spawn = _mapView->cursorPosition();
    if (auto const hit = _mapView->pickTerrainWorldPositionUnderCursor())
      spawn = *hit;

    std::size_t new_index = 0;
    _mapView->recordPointLightListChange([this, spawn, &new_index]()
    {
      World::PointLight light{};
      light.id = allocateNextLightId();
      light.position = spawn;
      World::syncPointLightTileFromPosition(light);
      light.color = { 1.f, 1.f, 1.f };
      light.attenuation_start = 0.f;
      light.attenuation_end = 10.f;
      light.intensity = 1.f;
      light.light_type = World::MapLightType::Point;
      light.flicker_mode = 0;

      auto& lights = _mapView->getWorld()->pointLights();
      lights.push_back(light);
      new_index = lights.size() - 1;
      _mapView->getWorld()->selectedPointLightIndex(new_index);
      _mapView->setPointLightPropertyEditFallback(new_index);
    });

    refreshFromWorld();
    selectWorldIndexInTable(new_index);
    openPropertyDialog(new_index);
  });

  _deleteSelected = [this]()
  {
    auto const idx = selectedLightIndex();
    if (!idx)
      return;

    if (_tool)
      _tool->closePropertyDialogs();

    _mapView->flushPointLightPropertyUndoBatch();

    _mapView->recordPointLightListChange([this, idx = *idx]()
    {
      auto& lights = _mapView->getWorld()->pointLights();
      if (idx >= lights.size())
        return;

      lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(idx));
      _mapView->getWorld()->selectedPointLightIndex(std::nullopt);
      _mapView->clearPointLightPropertyEditFallback();
    });

    refreshFromWorld();
    clearTableSelection();
    _mapView->invalidate();
  };

  QObject::connect(delete_btn, &QPushButton::clicked, [this]()
  {
    if (selectedLightIndex())
      _deleteSelected();
    else
      QMessageBox::information(this, tr("Point lights"), tr("Select a light in the list first."));
  });

  auto* copy_shortcut = new QShortcut(QKeySequence::Copy, this);
  copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(copy_shortcut, &QShortcut::activated, this, [this]() { copySelectedToClipboard(); });

  auto* paste_shortcut = new QShortcut(QKeySequence::Paste, this);
  paste_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(paste_shortcut, &QShortcut::activated, this, [this]() { pasteFromClipboard(); });
}

void PointLightEditor::keyPressEvent(QKeyEvent* event)
{
  if (!event)
  {
    QWidget::keyPressEvent(event);
    return;
  }

  if (event->matches(QKeySequence::Copy))
  {
    copySelectedToClipboard();
    event->accept();
    return;
  }

  if (event->matches(QKeySequence::Paste))
  {
    pasteFromClipboard();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Delete)
  {
    deleteSelectedLight();
    event->accept();
    return;
  }

  QWidget::keyPressEvent(event);
}

void PointLightEditor::copySelectedToClipboard()
{
  if (auto const idx = selectedLightIndex())
    PointLightClipboard::set(_mapView->getWorld()->pointLights()[*idx]);
}

void PointLightEditor::pasteFromClipboard()
{
  if (!PointLightClipboard::hasLight())
    return;

  if (_mapView->displayMode() != display_mode::in_3D)
  {
    QMessageBox::warning(this, tr("Point lights"), tr("Switch to 3D mode to place lights."));
    return;
  }

  glm::vec3 spawn = _mapView->cursorPosition();
  if (auto const hit = _mapView->pickTerrainWorldPositionUnderCursor())
    spawn = *hit;

  World::PointLight light = *PointLightClipboard::light();
  std::size_t new_index = 0;

  _mapView->recordPointLightListChange([this, spawn, &light, &new_index]()
  {
    light.id = allocateNextLightId();
    light.position = spawn;
    World::syncPointLightTileFromPosition(light);
    auto& lights = _mapView->getWorld()->pointLights();
    lights.push_back(light);
    new_index = lights.size() - 1;
    _mapView->getWorld()->selectedPointLightIndex(new_index);
    _mapView->setPointLightPropertyEditFallback(new_index);
  });

  refreshFromWorld();
  selectWorldIndexInTable(new_index);
  _mapView->invalidate();
}

bool PointLightEditor::clipboardHasLight() const
{
  return PointLightClipboard::hasLight();
}

void PointLightEditor::deleteSelectedLight()
{
  if (_deleteSelected)
    _deleteSelected();
}

void PointLightEditor::refreshFromWorld()
{
  if (!_mapView || !_mapView->getWorld())
    return;

  auto const& lights = _mapView->getWorld()->pointLights();

  QSignalBlocker const block(_lightTable);
  _lightTable->setSortingEnabled(false);
  _lightTable->clearContents();
  _lightTable->setRowCount(static_cast<int>(lights.size()));

  for (std::size_t i = 0; i < lights.size(); ++i)
    setRowForLight(_lightTable, static_cast<int>(i), i, lights[i]);

  _lightTable->setSortingEnabled(true);

  if (auto const sel = _mapView->getWorld()->selectedPointLightIndex();
      sel && *sel < lights.size())
  {
    selectWorldIndexInTable(*sel);
  }
  else if (auto const fallback = _mapView->resolvePointLightPropertyEditIndex(_lightTable);
           fallback && *fallback < lights.size())
  {
    selectWorldIndexInTable(*fallback);
  }
}

void PointLightEditor::openPropertyDialog(std::optional<std::size_t> light_index)
{
  if (!light_index || !_mapView || !_mapView->getWorld())
    return;

  if (_tool)
    _tool->closePropertyDialogs();

  auto* dialog = new PointLightPropertyDialog(_tool, _mapView, light_index, _mapView);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  if (_tool)
    _tool->_property_dialog = dialog;

  QObject::connect(dialog, &PointLightPropertyDialog::dialogClosed, [this, dialog]()
  {
    if (_tool && _tool->_property_dialog == dialog)
      _tool->_property_dialog = nullptr;
    refreshFromWorld();
    _mapView->invalidate();
  });

  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void PointLightEditor::syncListSelectionToWorld(std::size_t index)
{
  _mapView->getWorld()->selectedPointLightIndex(index);
  _mapView->setPointLightPropertyEditFallback(index);
  _mapView->invalidate();
}

void PointLightEditor::selectWorldIndexInTable(std::size_t index)
{
  QSignalBlocker const block(_lightTable);
  for (int row = 0; row < _lightTable->rowCount(); ++row)
  {
    if (QTableWidgetItem* item = _lightTable->item(row, kColGrid))
    {
      if (static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong()) == index)
      {
        _lightTable->selectRow(row);
        _lightTable->setCurrentCell(row, 0);
        return;
      }
    }
  }
}

void PointLightEditor::clearTableSelection()
{
  QSignalBlocker const block(_lightTable);
  _lightTable->clearSelection();
  _lightTable->setCurrentCell(-1, -1);
}

std::optional<std::size_t> PointLightEditor::selectedLightIndex() const
{
  return _mapView->resolvePointLightPropertyEditIndex(_lightTable);
}

std::uint32_t PointLightEditor::allocateNextLightId() const
{
  std::uint32_t max_id = 0;
  for (auto const& light : _mapView->getWorld()->pointLights())
    max_id = std::max(max_id, light.id);
  return max_id + 1;
}
