// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "ChunkGroupsWidget.hpp"

#include <noggit/MapView.h>
#include <noggit/World.h>

#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <optional>

using namespace Noggit::Ui::Tools::ChunkManipulator;

namespace
{
  QString settingsPath(QString const& map_key)
  {
    return QStringLiteral("maps/%1/chunk_manipulator/groups_json").arg(map_key);
  }

  QJsonObject indexToJson(SelectedChunkIndex const& idx)
  {
    QJsonObject o;
    o[QStringLiteral("tx")] = static_cast<qint64>(idx.tile_index.x);
    o[QStringLiteral("tz")] = static_cast<qint64>(idx.tile_index.z);
    o[QStringLiteral("cx")] = static_cast<int>(idx.x);
    o[QStringLiteral("cz")] = static_cast<int>(idx.z);
    return o;
  }

  std::optional<SelectedChunkIndex> jsonToIndex(QJsonObject const& o)
  {
    if (!o.contains(QStringLiteral("tx")) || !o.contains(QStringLiteral("tz"))
        || !o.contains(QStringLiteral("cx")) || !o.contains(QStringLiteral("cz")))
    {
      return std::nullopt;
    }
    TileIndex const ti{ static_cast<std::size_t>(o.value(QStringLiteral("tx")).toVariant().toULongLong()),
                        static_cast<std::size_t>(o.value(QStringLiteral("tz")).toVariant().toULongLong()) };
    if (!ti.is_valid())
      return std::nullopt;
    return SelectedChunkIndex{ ti
      , static_cast<unsigned>(o.value(QStringLiteral("cx")).toInt())
      , static_cast<unsigned>(o.value(QStringLiteral("cz")).toInt()) };
  }
}

ChunkGroupsWidget::ChunkGroupsWidget(ChunkClipboard* clipboard, QSettings* settings, QString map_basename_key, MapView* map_view, QWidget* parent)
  : QWidget(parent)
  , _clipboard(clipboard)
  , _settings(settings)
  , _map_key(std::move(map_basename_key))
  , _map_view(map_view)
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* group = new QGroupBox(tr("Chunk groups"), this);
  auto* gl = new QVBoxLayout(group);
  gl->setSpacing(6);

  _list = new QListWidget(group);
  _list->setMinimumHeight(72);
  _list->setMaximumHeight(140);
  _list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  gl->addWidget(_list);

  auto* add_quick = new QPushButton(tr("Add to quick selection"), group);
  add_quick->setToolTip(tr("Load the highlighted saved group into the quick (wireframe) selection."));
  auto* remove_quick = new QPushButton(tr("Remove from quick selection"), group);
  remove_quick->setToolTip(tr("Clear the quick selection (named groups in the list are not deleted)."));
  auto* rot_btn = new QPushButton(tr("Rotate 90 degrees"), group);
  rot_btn->setEnabled(false);
  rot_btn->setToolTip(tr("Not implemented yet."));
  auto* flip_btn = new QPushButton(tr("Flip selection"), group);
  flip_btn->setEnabled(false);
  flip_btn->setToolTip(tr("Not implemented yet."));

  for (auto* b : { add_quick, remove_quick, rot_btn, flip_btn })
  {
    b->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    gl->addWidget(b);
  }

  root->addWidget(group);

  connect(add_quick, &QPushButton::clicked, this, &ChunkGroupsWidget::onLoadGroup);
  connect(remove_quick, &QPushButton::clicked, this, &ChunkGroupsWidget::onClearSelection);
  connect(_list, &QListWidget::itemDoubleClicked, this, &ChunkGroupsWidget::onLoadGroup);

  _list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(_list, &QWidget::customContextMenuRequested, this, [this](QPoint const& pos)
  {
    QMenu menu(this);
    menu.addAction(tr("Save current selection as new group…"), this, &ChunkGroupsWidget::onAddFromSelection);
    menu.addAction(tr("Delete selected group"), this, &ChunkGroupsWidget::onRemoveGroup);
    menu.exec(_list->mapToGlobal(pos));
  });

  reloadList();
}

void ChunkGroupsWidget::onAddFromSelection()
{
  if (!_clipboard || !_settings)
    return;

  if (_clipboard->selectedChunks().empty())
  {
    QMessageBox::information(this, tr("Chunk groups"), tr("Nothing selected. Shift+LMB (or Ctrl+LMB) paint chunks first."));
    return;
  }

  bool ok = false;
  QString const name = QInputDialog::getText(this, tr("Save chunk group"), tr("Group name:"), QLineEdit::Normal, QString{}, &ok);
  if (!ok || name.trimmed().isEmpty())
    return;

  QJsonArray chunks;
  for (auto const& idx : _clipboard->selectedChunks())
    chunks.append(indexToJson(idx));

  QJsonObject group_obj;
  group_obj[QStringLiteral("name")] = name.trimmed();
  group_obj[QStringLiteral("chunks")] = chunks;

  QJsonArray all = QJsonDocument::fromJson(_settings->value(settingsPath(_map_key)).toByteArray()).array();
  all.append(group_obj);
  _settings->setValue(settingsPath(_map_key), QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
  reloadList();
}

void ChunkGroupsWidget::onRemoveGroup()
{
  if (!_settings || !_list || _list->currentRow() < 0)
    return;

  QJsonArray all = QJsonDocument::fromJson(_settings->value(settingsPath(_map_key)).toByteArray()).array();
  if (_list->currentRow() >= 0 && _list->currentRow() < all.size())
  {
    all.removeAt(_list->currentRow());
    _settings->setValue(settingsPath(_map_key), QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
  }
  reloadList();
}

void ChunkGroupsWidget::onLoadGroup()
{
  if (!_clipboard || !_settings || !_list)
    return;

  if (_list->currentRow() < 0)
  {
    QMessageBox::information(this, tr("Chunk groups"), tr("Select a saved group in the list first (or double-click it)."));
    return;
  }

  QJsonArray const all = QJsonDocument::fromJson(_settings->value(settingsPath(_map_key)).toByteArray()).array();
  int const row = _list->currentRow();
  if (row < 0 || row >= all.size())
    return;

  QJsonObject const group_obj = all.at(row).toObject();
  QJsonArray const chunks = group_obj.value(QStringLiteral("chunks")).toArray();

  _clipboard->clearSelection();
  for (auto const& v : chunks)
  {
    if (auto idx = jsonToIndex(v.toObject()))
    {
      if (_map_view && _map_view->getWorld()->mapIndex.hasTile(idx->tile_index))
        _clipboard->selectChunk(idx->tile_index, idx->x, idx->z, ChunkSelectionMode::SELECT);
    }
  }

  if (_map_view)
    _map_view->invalidate();
}

void ChunkGroupsWidget::onClearSelection()
{
  if (!_clipboard)
    return;
  _clipboard->clearSelection();
  if (_map_view)
    _map_view->invalidate();
}

void ChunkGroupsWidget::reloadList()
{
  if (!_list || !_settings)
    return;
  _list->clear();
  QJsonArray const all = QJsonDocument::fromJson(_settings->value(settingsPath(_map_key)).toByteArray()).array();
  for (auto const& v : all)
  {
    QJsonObject const o = v.toObject();
    _list->addItem(o.value(QStringLiteral("name")).toString());
  }
}
