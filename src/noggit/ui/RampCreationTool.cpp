// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/RampCreationTool.hpp>
#include <noggit/MapView.h>

#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

namespace Noggit::Ui
{
  RampCreationTool::RampCreationTool(MapView* map_view, QWidget* parent)
    : QWidget(parent)
    , _map_view(map_view)
  {
    setWindowTitle(tr("Ramp Creation Tool"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(8);

    auto* row_pick = new QHBoxLayout();
    _pick_first = new QPushButton(tr("Pick First Location"), this);
    _pick_second = new QPushButton(tr("Pick Second Location"), this);
    row_pick->addWidget(_pick_first);
    row_pick->addWidget(_pick_second);
    main_layout->addLayout(row_pick);

    _create = new QPushButton(tr("Create Ramp"), this);
    main_layout->addWidget(_create);

    _first_label = new QLabel(tr("First: (not set)"), this);
    _second_label = new QLabel(tr("Second: (not set)"), this);
    _first_label->setWordWrap(true);
    _second_label->setWordWrap(true);
    main_layout->addWidget(_first_label);
    main_layout->addWidget(_second_label);

    main_layout->addWidget(new QLabel(tr("Radius:"), this));
    _radius_slider = new QSlider(Qt::Horizontal, this);
    _radius_slider->setRange(1, 500);
    _radius_slider->setValue(25);
    _radius_slider->setToolTip(tr("Half-width of the ramp corridor (world units)"));
    main_layout->addWidget(_radius_slider);

    main_layout->addWidget(new QLabel(tr("Cap length:"), this));
    _cap_slider = new QSlider(Qt::Horizontal, this);
    _cap_slider->setRange(1, 200);
    _cap_slider->setValue(15);
    _cap_slider->setToolTip(tr("Length along the path at each end for blending into existing terrain"));
    main_layout->addWidget(_cap_slider);

    main_layout->addWidget(new QLabel(tr("Blend strength:"), this));
    _blend_slider = new QSlider(Qt::Horizontal, this);
    _blend_slider->setRange(0, 100);
    _blend_slider->setValue(0);
    _blend_slider->setToolTip(tr("0 = full application of ramp height; 100 = preserve most of existing height"));
    main_layout->addWidget(_blend_slider);

    connect(_pick_first, &QPushButton::clicked, this, [this]
    {
      if (!_map_view)
        return;
      _map_view->setRampPickTarget(1);
      updatePickButtonHighlight();
    });

    connect(_pick_second, &QPushButton::clicked, this, [this]
    {
      if (!_map_view)
        return;
      _map_view->setRampPickTarget(2);
      updatePickButtonHighlight();
    });

    connect(_create, &QPushButton::clicked, this, [this]
    {
      emit createRampRequested();
    });

    connect(_radius_slider, &QSlider::valueChanged, this, [this](int)
    {
      if (_map_view)
        _map_view->invalidate();
    });
    connect(_cap_slider, &QSlider::valueChanged, this, [this](int)
    {
      if (_map_view)
        _map_view->invalidate();
    });

    setMinimumWidth(320);
    updatePickButtonHighlight();
    updateCreateEnabled();
  }

  float RampCreationTool::radius() const
  {
    return _radius_slider ? static_cast<float>(_radius_slider->value()) : 25.f;
  }

  float RampCreationTool::capLength() const
  {
    return _cap_slider ? static_cast<float>(_cap_slider->value()) : 15.f;
  }

  float RampCreationTool::blendStrength() const
  {
    if (!_blend_slider)
      return 0.f;
    return std::clamp(_blend_slider->value() / 100.f, 0.f, 1.f);
  }

  void RampCreationTool::clearPickedPoints()
  {
    if (_map_view)
    {
      _map_view->clearRampPoints();
    }
    refreshPointLabels();
    updateCreateEnabled();
    if (_map_view)
      _map_view->invalidate();
  }

  void RampCreationTool::refreshPointLabels()
  {
    if (!_map_view)
      return;
    auto const a = _map_view->rampPointA();
    auto const b = _map_view->rampPointB();
    if (a)
      _first_label->setText(tr("First: %1, %2, %3").arg(a->x, 0, 'f', 1).arg(a->y, 0, 'f', 1).arg(a->z, 0, 'f', 1));
    else
      _first_label->setText(tr("First: (not set)"));
    if (b)
      _second_label->setText(tr("Second: %1, %2, %3").arg(b->x, 0, 'f', 1).arg(b->y, 0, 'f', 1).arg(b->z, 0, 'f', 1));
    else
      _second_label->setText(tr("Second: (not set)"));
    updateCreateEnabled();
  }

  void RampCreationTool::updatePickButtonHighlight()
  {
    if (!_map_view || !_pick_first || !_pick_second)
      return;
    int const t = _map_view->rampPickTarget();
    _pick_first->setChecked(t == 1);
    _pick_second->setChecked(t == 2);
    _pick_first->setFlat(t != 1);
    _pick_second->setFlat(t != 2);
  }

  void RampCreationTool::updateCreateEnabled()
  {
    if (!_create || !_map_view)
      return;
    auto const a = _map_view->rampPointA();
    auto const b = _map_view->rampPointB();
    bool ok = a.has_value() && b.has_value() && radius() > 0.f;
    if (ok)
    {
      glm::vec2 const d(b->x - a->x, b->z - a->z);
      ok = glm::length(d) > 1e-2f;
    }
    _create->setEnabled(ok);
  }
}
