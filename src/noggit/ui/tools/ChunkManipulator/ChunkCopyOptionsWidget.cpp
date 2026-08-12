// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "ChunkCopyOptionsWidget.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

using namespace Noggit::Ui::Tools::ChunkManipulator;

namespace
{
  QString settingsPath(QString const& map_key)
  {
    return QStringLiteral("maps/%1/chunk_manipulator/copy_flags").arg(map_key);
  }
}

ChunkCopyOptionsWidget::ChunkCopyOptionsWidget(ChunkClipboard* clipboard, QSettings* settings, QString map_basename_key, QWidget* parent)
  : QWidget(parent)
  , _clipboard(clipboard)
  , _settings(settings)
  , _map_key(std::move(map_basename_key))
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  auto* group = new QGroupBox(tr("Paste allow:"), this);
  auto* grid = new QGridLayout(group);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(4);

  _models = new QCheckBox(tr("M2s"), group);
  _wmos = new QCheckBox(tr("WMOs"), group);
  _terrain = new QCheckBox(tr("Terrain"), group);
  _holes = new QCheckBox(tr("Holes"), group);
  _liquid = new QCheckBox(tr("Water"), group);
  _vertex_colors = new QCheckBox(tr("Vertex shading"), group);
  _flags = new QCheckBox(tr("Flags"), group);
  _shadows = new QCheckBox(tr("Point lighting"), group);
  _shadows->setToolTip(tr("Baked shadow map on terrain vertices."));

  // Mockup column order: col0 m2s, Terrain, Water, Flags — col1 WMOs, Holes, Vertex shading, Point lighting
  grid->addWidget(_models, 0, 0);
  grid->addWidget(_wmos, 0, 1);
  grid->addWidget(_terrain, 1, 0);
  grid->addWidget(_holes, 1, 1);
  grid->addWidget(_liquid, 2, 0);
  grid->addWidget(_vertex_colors, 2, 1);
  grid->addWidget(_flags, 3, 0);
  grid->addWidget(_shadows, 3, 1);

  for (auto* cb : { _models, _wmos, _terrain, _holes, _liquid, _vertex_colors, _flags, _shadows })
  {
    connect(cb, &QCheckBox::toggled, this, &ChunkCopyOptionsWidget::onAnyToggled);
  }

  root->addWidget(group);

  readPersisted();
  if (_clipboard)
  {
    setUiFromFlags(_clipboard->copyParams());
  }
}

ChunkCopyFlags ChunkCopyOptionsWidget::flagsFromUi() const
{
  unsigned v = 0;
  if (_terrain->isChecked()) v |= to_underlying(ChunkCopyFlags::TERRAIN);
  if (_vertex_colors->isChecked()) v |= to_underlying(ChunkCopyFlags::VERTEX_COLORS);
  if (_shadows->isChecked()) v |= to_underlying(ChunkCopyFlags::SHADOWS);
  if (_liquid->isChecked()) v |= to_underlying(ChunkCopyFlags::LIQUID);
  if (_holes->isChecked()) v |= to_underlying(ChunkCopyFlags::HOLES);
  if (_flags->isChecked()) v |= to_underlying(ChunkCopyFlags::FLAGS);
  if (_wmos->isChecked()) v |= to_underlying(ChunkCopyFlags::WMOs);
  if (_models->isChecked()) v |= to_underlying(ChunkCopyFlags::MODELS);
  v |= (_extra_copy_bits & extra_copy_bits_mask());
  return static_cast<ChunkCopyFlags>(v);
}

void ChunkCopyOptionsWidget::setUiFromFlags(ChunkCopyFlags flags)
{
  _extra_copy_bits = to_underlying(flags) & extra_copy_bits_mask();

  auto const set = [&](QCheckBox* cb, ChunkCopyFlags bit)
  {
    QSignalBlocker b(cb);
    cb->setChecked(chunk_copy_flags_test(flags, bit));
  };

  set(_terrain, ChunkCopyFlags::TERRAIN);
  set(_vertex_colors, ChunkCopyFlags::VERTEX_COLORS);
  set(_shadows, ChunkCopyFlags::SHADOWS);
  set(_liquid, ChunkCopyFlags::LIQUID);
  set(_holes, ChunkCopyFlags::HOLES);
  set(_flags, ChunkCopyFlags::FLAGS);
  set(_wmos, ChunkCopyFlags::WMOs);
  set(_models, ChunkCopyFlags::MODELS);
}

void ChunkCopyOptionsWidget::onAnyToggled()
{
  pushFlagsToClipboard();
  persist();
}

void ChunkCopyOptionsWidget::pushFlagsToClipboard()
{
  if (_clipboard)
  {
    _clipboard->setCopyParams(flagsFromUi());
  }
}

void ChunkCopyOptionsWidget::persist() const
{
  if (!_settings || _map_key.isEmpty())
  {
    return;
  }
  _settings->setValue(settingsPath(_map_key), static_cast<qlonglong>(to_underlying(flagsFromUi())));
}

void ChunkCopyOptionsWidget::readPersisted()
{
  if (!_settings || _map_key.isEmpty())
  {
    setUiFromFlags(chunk_copy_flags_all());
    return;
  }

  bool ok = false;
  unsigned const stored = static_cast<unsigned>(_settings->value(settingsPath(_map_key), static_cast<qlonglong>(to_underlying(chunk_copy_flags_all()))).toLongLong(&ok));
  if (!ok)
  {
    setUiFromFlags(chunk_copy_flags_all());
    return;
  }
  setUiFromFlags(static_cast<ChunkCopyFlags>(stored & to_underlying(chunk_copy_flags_all())));
  pushFlagsToClipboard();
}
