// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/BrushCursorTool.hpp>
#include <noggit/MapView.h>

#include <external/glm/glm.hpp>
#include <qt-color-widgets/color_selector.hpp>

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QVBoxLayout>
#include <QSignalBlocker>

namespace Noggit::Ui
{
  BrushCursorTool::BrushCursorTool(MapView* map_view, QWidget* parent)
    : QWidget(parent)
    , _map_view(map_view)
  {
    setWindowTitle(tr("Brush Cursor"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(8);

    main_layout->addWidget(new QLabel(tr("Display style (Shift+C cycles):"), this));

    _style_group = new QButtonGroup(this);

    auto add_style_radio = [&](BrushCursorStyle style, char const* label)
    {
      auto* radio = new QRadioButton(tr(label), this);
      _style_group->addButton(radio, static_cast<int>(style));
      main_layout->addWidget(radio);
    };

    add_style_radio(BrushCursorStyle::TerrainWrap, "Terrain wrap");
    add_style_radio(BrushCursorStyle::FlatCircle, "Flat circle");
    add_style_radio(BrushCursorStyle::Sphere, "Sphere");
    add_style_radio(BrushCursorStyle::NoOutline, "No outline");
    add_style_radio(BrushCursorStyle::DottedOutline, "Dotted outline");

    connect(_style_group, qOverload<int>(&QButtonGroup::idClicked), this, [this](int id)
    {
      if (_syncing || !_map_view)
      {
        return;
      }
      applyStyle(static_cast<BrushCursorStyle>(id));
    });

    main_layout->addSpacing(8);
    _outline_colors_label = new QLabel(tr("Dotted outline colors:"), this);
    main_layout->addWidget(_outline_colors_label);

    auto* color_form = new QFormLayout();
    _inner_color = new color_widgets::ColorSelector(this);
    _outer_color = new color_widgets::ColorSelector(this);
    color_form->addRow(tr("Inner"), _inner_color);
    color_form->addRow(tr("Outer"), _outer_color);
    main_layout->addLayout(color_form);

    connect(_inner_color, &color_widgets::ColorSelector::colorChanged, this, [this] { applyOutlineColors(); });
    connect(_outer_color, &color_widgets::ColorSelector::colorChanged, this, [this] { applyOutlineColors(); });

    auto* cycle_btn = new QPushButton(tr("Next style (Shift+C)"), this);
    connect(cycle_btn, &QPushButton::clicked, this, [this]
    {
      if (_map_view)
      {
        _map_view->cycleBrushCursorStyle();
      }
    });
    main_layout->addWidget(cycle_btn);

    syncFromMapView();
  }

  void BrushCursorTool::syncFromMapView()
  {
    if (!_map_view)
    {
      return;
    }

    _syncing = true;

    QSignalBlocker const style_block(_style_group);
    if (auto* btn = _style_group->button(static_cast<int>(_map_view->brushCursorStyle())))
    {
      btn->setChecked(true);
    }

    glm::vec4 const inner = _map_view->brushCursorInnerOutlineColor();
    glm::vec4 const outer = _map_view->brushCursorOuterOutlineColor();
  {
    QSignalBlocker const inner_block(_inner_color);
    QSignalBlocker const outer_block(_outer_color);
    _inner_color->setColor(QColor::fromRgbF(inner.r, inner.g, inner.b, inner.a));
    _outer_color->setColor(QColor::fromRgbF(outer.r, outer.g, outer.b, outer.a));
  }

    bool const dotted = _map_view->brushCursorStyle() == BrushCursorStyle::DottedOutline;
    _outline_colors_label->setEnabled(dotted);
    _inner_color->setEnabled(dotted);
    _outer_color->setEnabled(dotted);

    _syncing = false;
  }

  void BrushCursorTool::applyStyle(BrushCursorStyle style)
  {
    if (!_map_view)
    {
      return;
    }

    _map_view->setBrushCursorStyle(style, true, true);

    bool const dotted = style == BrushCursorStyle::DottedOutline;
    _outline_colors_label->setEnabled(dotted);
    _inner_color->setEnabled(dotted);
    _outer_color->setEnabled(dotted);
  }

  void BrushCursorTool::applyOutlineColors()
  {
    if (_syncing || !_map_view)
    {
      return;
    }

    QColor const inner_q = _inner_color->color();
    QColor const outer_q = _outer_color->color();
    _map_view->setBrushCursorOutlineColors(
      glm::vec4(inner_q.redF(), inner_q.greenF(), inner_q.blueF(), inner_q.alphaF()),
      glm::vec4(outer_q.redF(), outer_q.greenF(), outer_q.blueF(), outer_q.alphaF()),
      true
    );
  }
}
