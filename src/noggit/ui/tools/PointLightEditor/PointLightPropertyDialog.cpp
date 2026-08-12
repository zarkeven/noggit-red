// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "PointLightPropertyDialog.hpp"

#include <noggit/MapTile.h>
#include <noggit/MapView.h>
#include <noggit/TileIndex.hpp>
#include <noggit/World.h>
#include <noggit/map_lights/LightInfoCatalog.hpp>
#include <noggit/tools/PointLightTool.hpp>
#include <noggit/ui/tools/PointLightEditor/PointLightClipboard.hpp>
#include <noggit/ui/tools/PointLightEditor/PointLightEditor.hpp>

#include <external/glm/gtc/constants.hpp>

#include <qt-color-widgets/color_selector.hpp>
#include <qt-color-widgets/color_wheel.hpp>

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <random>

using namespace Noggit::Ui::Tools;

PointLightPropertyDialog::PointLightPropertyDialog(Noggit::PointLightTool* tool
                                                  , ::MapView* mapView
                                                  , std::optional<std::size_t> light_index
                                                  , QWidget* parent)
  : QWidget(parent, Qt::Window)
  , _tool(tool)
  , _mapView(mapView)
  , _light_index(light_index)
{
  setWindowTitle(tr("Point/Spot Light Editor"));
  setMinimumSize(640, 480);
  setFocusPolicy(Qt::StrongFocus);

  Noggit::MapLights::LightInfoCatalog::instance().reload_from_settings();

  if (!_light_index || !_mapView || !_mapView->getWorld()
      || *_light_index >= _mapView->getWorld()->pointLights().size())
  {
    QTimer::singleShot(0, this, [this]() { close(); });
    return;
  }

  _working = _mapView->getWorld()->pointLights()[*_light_index];

  buildUi();
  loadFromWorkingCopy();
}

void PointLightPropertyDialog::buildUi()
{
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);

  auto* columns = new QHBoxLayout();
  root->addLayout(columns, 1);

  auto* left_col = new QVBoxLayout();
  columns->addLayout(left_col, 1);

  auto* color_box = new QGroupBox(tr("Color"), this);
  auto* color_layout = new QVBoxLayout(color_box);
  _color_selector = new color_widgets::ColorSelector(color_box);
  _color_selector->setDisplayMode(color_widgets::ColorSelector::NoAlpha);
  _color_selector->setMinimumHeight(25);
  _color_wheel = new color_widgets::ColorWheel(color_box);
  _color_wheel->setMinimumSize(QSize(200, 200));
  color_layout->addWidget(new QLabel(tr("Color:"), color_box));
  color_layout->addWidget(_color_selector);
  color_layout->addWidget(_color_wheel);
  left_col->addWidget(color_box);

  auto* cookie_box = new QGroupBox(tr("ShadowMask Texture"), this);
  auto* cookie_layout = new QVBoxLayout(cookie_box);
  auto* pick_cookie_btn = new QPushButton(tr("Select ShadowMask Texture"), cookie_box);
  _cookie_fdid_spin = new QSpinBox(cookie_box);
  _cookie_fdid_spin->setRange(0, 2147483647);
  _cookie_fdid_spin->setToolTip(tr("Light cookie FileDataID (MTEX entry). 0 = none."));
  _cookie_preview_lbl = new QLabel(tr("SHADOWMASK TEXTURE PREVIEW WINDOW"), cookie_box);
  _cookie_preview_lbl->setAlignment(Qt::AlignCenter);
  _cookie_preview_lbl->setMinimumHeight(120);
  _cookie_preview_lbl->setStyleSheet(QStringLiteral("background: #111; color: #888; border: 1px solid #444;"));
  cookie_layout->addWidget(pick_cookie_btn);
  cookie_layout->addWidget(new QLabel(tr("FileDataID:"), cookie_box));
  cookie_layout->addWidget(_cookie_fdid_spin);
  cookie_layout->addWidget(_cookie_preview_lbl, 1);
  left_col->addWidget(cookie_box, 1);

  auto* right_col = new QVBoxLayout();
  columns->addLayout(right_col, 1);

  auto make_slider_row = [&](QString const& label, QDoubleSpinBox*& out_spin, double min_v, double max_v, double step)
  {
    right_col->addWidget(new QLabel(label, this));
    auto* row = new QHBoxLayout();
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 1000);
    out_spin = new QDoubleSpinBox(this);
    out_spin->setRange(min_v, max_v);
    out_spin->setDecimals(2);
    out_spin->setSingleStep(step);

    QDoubleSpinBox* spin_box = out_spin;

    auto sync_slider_from_spin = [=]()
    {
      double const t = (spin_box->value() - min_v) / (max_v - min_v);
      QSignalBlocker const b(slider);
      slider->setValue(static_cast<int>(std::clamp(t, 0.0, 1.0) * 1000.0));
    };
    auto sync_spin_from_slider = [=]()
    {
      double const t = slider->value() / 1000.0;
      QSignalBlocker const b(spin_box);
      spin_box->setValue(min_v + t * (max_v - min_v));
    };

    QObject::connect(out_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, sync_slider_from_spin](double)
    {
      sync_slider_from_spin();
      applyFieldsFromUi();
      touchUndo();
    });
    QObject::connect(slider, &QSlider::valueChanged, [this, sync_spin_from_slider](int)
    {
      sync_spin_from_slider();
      applyFieldsFromUi();
      touchUndo();
    });

    row->addWidget(slider, 1);
    row->addWidget(out_spin);
    right_col->addLayout(row);
    sync_slider_from_spin();
  };

  make_slider_row(tr("Radius:"), _radius_spin, 0.0, 200.0, 0.5);
  make_slider_row(tr("Attenuation Start:"), _atten_start_spin, 0.0, 200.0, 0.5);
  make_slider_row(tr("Attenuation End:"), _atten_end_spin, 0.0, 200.0, 0.5);

  QObject::connect(_radius_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double v)
  {
    QSignalBlocker const block(_atten_end_spin);
    _atten_end_spin->setValue(v);
  });
  QObject::connect(_atten_end_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double v)
  {
    QSignalBlocker const block(_radius_spin);
    _radius_spin->setValue(v);
  });

  make_slider_row(tr("Flicker Speed"), _flicker_speed_spin, 0.0, 100.0, 0.5);
  make_slider_row(tr("Flicker Intensity"), _flicker_intensity_spin, 0.0, 100.0, 0.5);

  right_col->addWidget(new QLabel(tr("Animation style:"), this));
  _flicker_type_combo = new QComboBox(this);
  _flicker_type_combo->setToolTip(tr("None disables flicker. Presets come from MapUpconverter meta/LightInfo.json (LightAnims)."));
  right_col->addWidget(_flicker_type_combo);
  _flicker_source_lbl = new QLabel(this);
  _flicker_source_lbl->setWordWrap(true);
  _flicker_source_lbl->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
  right_col->addWidget(_flicker_source_lbl);
  rebuildFlickerTypeCombo();

  auto* type_box = new QGroupBox(tr("Light Type"), this);
  auto* type_layout = new QVBoxLayout(type_box);
  _light_type_combo = new QComboBox(type_box);
  _light_type_combo->addItem(tr("Point"));
  _light_type_combo->addItem(tr("Spot"));
  type_layout->addWidget(_light_type_combo);
  right_col->addWidget(type_box);

  _spot_panel = new QGroupBox(tr("Spot settings"), this);
  auto* spot_form = new QFormLayout(_spot_panel);
  _inner_angle_spin = new QDoubleSpinBox(_spot_panel);
  _outer_angle_spin = new QDoubleSpinBox(_spot_panel);
  _spot_radius_spin = new QDoubleSpinBox(_spot_panel);
  for (auto* spin : { _inner_angle_spin, _outer_angle_spin })
  {
    spin->setRange(0.0, 180.0);
    spin->setSuffix(QStringLiteral("°"));
  }
  _spot_radius_spin->setRange(0.0, 500.0);
  spot_form->addRow(tr("Inner angle:"), _inner_angle_spin);
  spot_form->addRow(tr("Outer angle:"), _outer_angle_spin);
  spot_form->addRow(tr("Spot radius:"), _spot_radius_spin);
  right_col->addWidget(_spot_panel);

  auto* ngpl_box = new QGroupBox(tr("ADT light cap (NGPL)"), this);
  auto* ngpl_layout = new QHBoxLayout(ngpl_box);
  _ngpl_status_lbl = new QLabel(ngpl_box);
  _ngpl_cap_spin = new QSpinBox(ngpl_box);
  _ngpl_cap_spin->setRange(1, 255);
  _ngpl_cap_spin->setValue(104);
  auto* apply_cap_btn = new QPushButton(tr("Apply cap"), ngpl_box);
  ngpl_layout->addWidget(_ngpl_status_lbl, 1);
  ngpl_layout->addWidget(_ngpl_cap_spin);
  ngpl_layout->addWidget(apply_cap_btn);
  right_col->addWidget(ngpl_box);

  auto* bottom = new QHBoxLayout();
  bottom->addStretch();
  auto* delete_btn = new QPushButton(tr("Delete"), this);
  delete_btn->setToolTip(tr("Remove this light from the map (Ctrl+C / Ctrl+V to duplicate)."));
  bottom->addWidget(delete_btn);
  root->addLayout(bottom);

  QObject::connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, this, [this](QColor const& c)
  {
    onColorChanged(c);
  });
  QObject::connect(_color_selector, &color_widgets::ColorSelector::colorChanged, this, [this](QColor const& c)
  {
    onColorChanged(c);
  });

  QObject::connect(pick_cookie_btn, &QPushButton::clicked, [this]()
  {
    bool ok = false;
    int const v = QInputDialog::getInt(this, tr("ShadowMask texture"), tr("FileDataID:"),
                                       static_cast<int>(_working.cookie_file_data_id), 0, 2147483647, 1, &ok);
    if (!ok)
      return;
    _cookie_fdid_spin->setValue(v);
  });

  QObject::connect(_cookie_fdid_spin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v)
  {
    _working.cookie_file_data_id = static_cast<std::uint32_t>(v);
    _cookie_preview_lbl->setText(v ? tr("Cookie FDID: %1").arg(v) : tr("SHADOWMASK TEXTURE PREVIEW WINDOW"));
    touchUndo();
  });

  QObject::connect(_flicker_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int)
  {
    syncFlickerTypeToWorkingCopy();
    syncFlickerControlsVisibility();
    applyWorkingCopyToWorld();
    touchUndo();
  });

  QObject::connect(_light_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index)
  {
    applyFieldsFromUi();
    _working.light_type = (index == 0) ? World::MapLightType::Point : World::MapLightType::Spot;
    if (_working.light_type == World::MapLightType::Spot)
    {
      World::ensureSpotLightDefaults(_working);
      QSignalBlocker const block_inner(_inner_angle_spin);
      QSignalBlocker const block_outer(_outer_angle_spin);
      QSignalBlocker const block_spot_radius(_spot_radius_spin);
      _inner_angle_spin->setValue(glm::degrees(_working.inner_angle));
      _outer_angle_spin->setValue(glm::degrees(_working.outer_angle));
      _spot_radius_spin->setValue(_working.spotlight_radius);
    }
    syncSpotControlsVisibility();
    touchUndo();
  });

  for (auto* spin : { _inner_angle_spin, _outer_angle_spin, _spot_radius_spin })
  {
    QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double)
    {
      applyFieldsFromUi();
      touchUndo();
    });
  }

  QObject::connect(apply_cap_btn, &QPushButton::clicked, [this]()
  {
    World::syncPointLightTileFromPosition(_working);
    MapTile* tile = _mapView->getWorld()->mapIndex.getTile(TileIndex(_working.tile_x, _working.tile_y));
    if (!tile)
    {
      QMessageBox::warning(this, tr("NGPL"), tr("Load ADT %1_%2 before applying cap.")
                                              .arg(_working.tile_x).arg(_working.tile_y));
      return;
    }
    tile->setAdtPointLightCap(static_cast<std::uint32_t>(_ngpl_cap_spin->value()));
    _mapView->getWorld()->mapIndex.setChanged(tile);
    loadFromWorkingCopy();
  });

  QObject::connect(delete_btn, &QPushButton::clicked, [this]()
  {
    deleteCurrentLight();
  });

  auto* copy_shortcut = new QShortcut(QKeySequence::Copy, this);
  copy_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(copy_shortcut, &QShortcut::activated, this, [this]()
  {
    applyFieldsFromUi();
    PointLightClipboard::set(_working);
  });

  auto* paste_shortcut = new QShortcut(QKeySequence::Paste, this);
  paste_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(paste_shortcut, &QShortcut::activated, this, [this]()
  {
    applyFieldsFromUi();
    applyWorkingCopyToWorld();
    _mapView->flushPointLightPropertyUndoBatch();
    PointLightClipboard::set(_working);

    if (_tool && _tool->editor())
    {
      close();
      _tool->editor()->pasteFromClipboard();
    }
  });
}

void PointLightPropertyDialog::loadFromWorkingCopy()
{
  QColor const color = QColor::fromRgbF(_working.color.r, _working.color.g, _working.color.b);
  {
    QSignalBlocker const block_selector(_color_selector);
    QSignalBlocker const block_wheel(_color_wheel);
    _color_selector->setColor(color);
    _color_wheel->setColor(color);
  }

  {
    QSignalBlocker const block_radius(_radius_spin);
    QSignalBlocker const block_atten_start(_atten_start_spin);
    QSignalBlocker const block_atten_end(_atten_end_spin);
    QSignalBlocker const block_flicker_speed(_flicker_speed_spin);
    QSignalBlocker const block_flicker_intensity(_flicker_intensity_spin);
    QSignalBlocker const block_inner(_inner_angle_spin);
    QSignalBlocker const block_outer(_outer_angle_spin);
    QSignalBlocker const block_spot_radius(_spot_radius_spin);
    QSignalBlocker const block_cookie(_cookie_fdid_spin);

    _radius_spin->setValue(_working.attenuation_end);
    _atten_start_spin->setValue(_working.attenuation_start);
    _atten_end_spin->setValue(_working.attenuation_end);
    _flicker_speed_spin->setValue(_working.flicker_speed);
    _flicker_intensity_spin->setValue(_working.flicker_intensity);
    _inner_angle_spin->setValue(glm::degrees(_working.inner_angle));
    _outer_angle_spin->setValue(glm::degrees(_working.outer_angle));
    _spot_radius_spin->setValue(_working.spotlight_radius);
    _cookie_fdid_spin->setValue(static_cast<int>(_working.cookie_file_data_id));
  }

  {
    QSignalBlocker const block(_light_type_combo);
    _light_type_combo->setCurrentIndex(_working.light_type == World::MapLightType::Point ? 0 : 1);
  }

  {
    QSignalBlocker const block(_flicker_type_combo);
    syncFlickerComboFromWorkingCopy();
  }

  updateFlickerSourceLabel();
  syncFlickerControlsVisibility();

  syncSpotControlsVisibility();

  World::syncPointLightTileFromPosition(_working);
  std::size_t const count = _mapView->getWorld()->pointLightsInAdtCount(_working.tile_x, _working.tile_y, _light_index);
  std::uint32_t const cap = _mapView->getWorld()->effectivePointLightCapForAdt(_working.tile_x, _working.tile_y);
  _ngpl_status_lbl->setText(tr("ADT %1_%2: %3 / %4 lights")
                              .arg(_working.tile_x).arg(_working.tile_y).arg(count).arg(cap));
  _ngpl_cap_spin->setValue(static_cast<int>(cap));
}

void PointLightPropertyDialog::applyFieldsFromUi()
{
  if (!_color_wheel || !_color_selector)
    return;

  QColor const color = _color_wheel->color();
  _working.color = { color.redF(), color.greenF(), color.blueF() };

  _working.attenuation_start = static_cast<float>(_atten_start_spin->value());
  _working.attenuation_end = static_cast<float>(_atten_end_spin->value());
  _working.flicker_speed = static_cast<float>(_flicker_speed_spin->value());
  _working.flicker_intensity = static_cast<float>(_flicker_intensity_spin->value());
  _working.inner_angle = static_cast<float>(glm::radians(_inner_angle_spin->value()));
  _working.outer_angle = static_cast<float>(glm::radians(_outer_angle_spin->value()));
  _working.spotlight_radius = static_cast<float>(_spot_radius_spin->value());
  _working.cookie_file_data_id = static_cast<std::uint32_t>(_cookie_fdid_spin->value());
  _working.light_type = (_light_type_combo->currentIndex() == 0)
                      ? World::MapLightType::Point
                      : World::MapLightType::Spot;
  if (_working.light_type == World::MapLightType::Spot)
  {
    World::ensureSpotLightDefaults(_working);
  }

  syncFlickerComboFromWorkingCopy();
}

void PointLightPropertyDialog::rebuildFlickerTypeCombo()
{
  if (!_flicker_type_combo)
    return;

  QSignalBlocker const block(_flicker_type_combo);
  _flicker_type_combo->clear();
  _flicker_type_combo->addItem(tr("None"));

  auto const& catalog = Noggit::MapLights::LightInfoCatalog::instance();
  for (auto const& preset : catalog.presets())
  {
    _flicker_type_combo->addItem(
      Noggit::MapLights::LightInfoCatalog::preset_display_label (QString::fromStdString (preset.id)));
  }

  _flicker_type_combo->addItem(tr("Custom"));
  updateFlickerSourceLabel();
}

int PointLightPropertyDialog::customFlickerComboIndex() const
{
  if (!_flicker_type_combo)
    return 0;

  auto const& presets = Noggit::MapLights::LightInfoCatalog::instance().presets();
  return static_cast<int>(presets.size()) + 1;
}

void PointLightPropertyDialog::syncFlickerComboFromWorkingCopy()
{
  if (!_flicker_type_combo)
    return;

  if (_working.flicker_mode == 0)
  {
    _flicker_type_combo->setCurrentIndex(0);
    return;
  }

  if (auto const matched = Noggit::MapLights::LightInfoCatalog::instance().match_preset (_working))
  {
    _flicker_type_combo->setCurrentIndex(static_cast<int>(*matched) + 1);
    return;
  }

  _flicker_type_combo->setCurrentIndex(customFlickerComboIndex());
}

void PointLightPropertyDialog::updateFlickerSourceLabel()
{
  if (!_flicker_source_lbl)
    return;

  auto const& catalog = Noggit::MapLights::LightInfoCatalog::instance();
  if (auto const path = catalog.resolved_json_path())
    _flicker_source_lbl->setText(tr("Presets from: %1").arg(QString::fromStdString (path->string())));
  else if (catalog.using_fallback_presets())
    _flicker_source_lbl->setText(tr("Using built-in presets (set MapUpconverter path in Settings)."));
  else
    _flicker_source_lbl->clear();
}

void PointLightPropertyDialog::syncFlickerTypeToWorkingCopy()
{
  if (!_flicker_type_combo)
  {
    return;
  }

  int const index = _flicker_type_combo->currentIndex();
  if (index <= 0)
  {
    _working.flicker_mode = 0;
    return;
  }

  int const custom_index = customFlickerComboIndex();
  if (index == custom_index)
  {
    if (_working.flicker_mode == 0)
      _working.flicker_mode = 2;

    if (_working.flicker_seed == 0)
    {
      static thread_local std::mt19937 rng{std::random_device{}()};
      _working.flicker_seed = rng();
    }

    if (_flicker_intensity_spin)
    {
      QSignalBlocker const block(_flicker_intensity_spin);
      _flicker_intensity_spin->setValue(_working.flicker_intensity);
    }
    if (_flicker_speed_spin)
    {
      QSignalBlocker const block(_flicker_speed_spin);
      _flicker_speed_spin->setValue(_working.flicker_speed);
    }
    return;
  }

  std::size_t const preset_index = static_cast<std::size_t>(index - 1);
  Noggit::MapLights::LightInfoCatalog::instance().apply_preset (_working, preset_index);

  if (_flicker_intensity_spin)
  {
    QSignalBlocker const block(_flicker_intensity_spin);
    _flicker_intensity_spin->setValue(_working.flicker_intensity);
  }
  if (_flicker_speed_spin)
  {
    QSignalBlocker const block(_flicker_speed_spin);
    _flicker_speed_spin->setValue(_working.flicker_speed);
  }
}

void PointLightPropertyDialog::syncFlickerControlsVisibility()
{
  bool const flicker_enabled = _flicker_type_combo && _flicker_type_combo->currentIndex() != 0;
  if (_flicker_speed_spin)
  {
    _flicker_speed_spin->setEnabled(flicker_enabled);
  }
  if (_flicker_intensity_spin)
  {
    _flicker_intensity_spin->setEnabled(flicker_enabled);
  }
}

void PointLightPropertyDialog::onColorChanged(QColor const& color)
{
  if (!color.isValid())
    return;

  QObject* const sender_obj = sender();
  if (sender_obj != _color_wheel)
  {
    QSignalBlocker const block(_color_wheel);
    _color_wheel->setColor(color);
  }
  if (sender_obj != _color_selector)
  {
    QSignalBlocker const block(_color_selector);
    _color_selector->setColor(color);
  }

  _working.color = { color.redF(), color.greenF(), color.blueF() };
  touchUndo();
}

void PointLightPropertyDialog::applyWorkingCopyToWorld()
{
  if (!_light_index || !_mapView || !_mapView->getWorld())
    return;

  World::syncPointLightTileFromPosition(_working);
  if (*_light_index >= _mapView->getWorld()->pointLights().size())
    return;

  _mapView->getWorld()->pointLights()[*_light_index] = _working;
  _mapView->invalidate();
}

void PointLightPropertyDialog::syncSpotControlsVisibility()
{
  bool const is_spot = _working.light_type == World::MapLightType::Spot;
  _spot_panel->setVisible(is_spot);
  if (_light_type_combo)
  {
    QSignalBlocker const block(_light_type_combo);
    _light_type_combo->setCurrentIndex(is_spot ? 1 : 0);
  }
}

void PointLightPropertyDialog::touchUndo()
{
  applyWorkingCopyToWorld();
  _mapView->touchPointLightPropertyUndoBatch();
}

void PointLightPropertyDialog::deleteCurrentLight()
{
  if (!_light_index || !_mapView || !_mapView->getWorld())
    return;

  applyFieldsFromUi();
  applyWorkingCopyToWorld();
  _mapView->flushPointLightPropertyUndoBatch();

  std::size_t const idx = *_light_index;
  _mapView->recordPointLightListChange([this, idx]()
  {
    auto& lights = _mapView->getWorld()->pointLights();
    if (idx >= lights.size())
      return;

    lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(idx));
    _mapView->getWorld()->selectedPointLightIndex(std::nullopt);
    _mapView->clearPointLightPropertyEditFallback();
  });

  _light_index.reset();
  close();
}

void PointLightPropertyDialog::closeEvent(QCloseEvent* event)
{
  if (_light_index)
  {
    applyFieldsFromUi();
    applyWorkingCopyToWorld();
    _mapView->flushPointLightPropertyUndoBatch();
  }
  QWidget::closeEvent(event);
  Q_EMIT dialogClosed();
}
