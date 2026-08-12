// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/TerrainUnifiedToolWidget.hpp>

#include <noggit/MapView.h>
#include <noggit/ui/FlattenTool.hpp>
#include <noggit/ui/TerrainTool.hpp>
#include <noggit/ui/tools/UiCommon/ExtendedSlider.hpp>

#include <glm/vec2.hpp>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLineF>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace
{
  constexpr char const* k_preset_format = "noggit_radial_falloff_preset";
  constexpr int k_preset_version = 1;

  [[nodiscard]] QString radialFalloffPresetRoot()
  {
    QString const p = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/Noggit3/radial_falloff_presets");
    QDir().mkpath(p);
    return p;
  }

  [[nodiscard]] QString sanitizePresetBaseName(QString s)
  {
    static QRegularExpression const strip(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    s.replace(strip, QStringLiteral("_"));
    s.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    s = s.trimmed();
    if (s.isEmpty())
      s = QStringLiteral("curve");
    return s;
  }

  [[nodiscard]] QPixmap curvePreviewPixmap(std::vector<glm::vec2> const& pts, QSize const sz)
  {
    QPixmap pm(sz);
    pm.fill(QColor(32, 32, 36));
    if (pts.size() < 2)
      return pm;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRect const plot = QRect(2, 2, sz.width() - 4, sz.height() - 4);
    p.setPen(QPen(QColor(70, 70, 78), 1));
    p.drawRect(plot);

    auto to_px = [&](glm::vec2 const& pt) -> QPointF {
      return { plot.left() + pt.x * plot.width(), plot.bottom() - pt.y * plot.height() };
    };

    QPainterPath path;
    path.moveTo(to_px(pts.front()));
    for (std::size_t i = 1; i < pts.size(); ++i)
      path.lineTo(to_px(pts[i]));
    p.setPen(QPen(QColor(120, 190, 255), 2));
    p.drawPath(path);
    return pm;
  }

  [[nodiscard]] bool readPresetFile(QString const& path, QJsonObject* out_root = nullptr)
  {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
      return false;
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
      return false;
    QJsonObject const o = doc.object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(k_preset_format))
      return false;
    if (o.value(QStringLiteral("version")).toInt(0) != k_preset_version)
      return false;
    if (!o.contains(QStringLiteral("curve")) || !o.value(QStringLiteral("curve")).isObject())
      return false;
    Noggit::BrushFalloffCurve test;
    test.fromJson(o.value(QStringLiteral("curve")).toObject());
    if (test.controlPoints().size() < 2)
      return false;
    if (out_root)
      *out_root = o;
    return true;
  }

  [[nodiscard]] QString presetDisplayNameFromFile(QString const& path)
  {
    QJsonObject o;
    if (!readPresetFile(path, &o))
      return QFileInfo(path).completeBaseName();
    QString const n = o.value(QStringLiteral("name")).toString().trimmed();
    return n.isEmpty() ? QFileInfo(path).completeBaseName() : n;
  }

  [[nodiscard]] QJsonObject buildPresetDocument(QString const& display_name, Noggit::BrushFalloffCurve const& curve)
  {
    QJsonObject o;
    o.insert(QStringLiteral("format"), QLatin1String(k_preset_format));
    o.insert(QStringLiteral("version"), k_preset_version);
    o.insert(QStringLiteral("name"), display_name);
    o.insert(QStringLiteral("curve"), curve.toJson());
    return o;
  }

  [[nodiscard]] QString uniquePresetPath(QString const& dir, QString const& base_name)
  {
    QString stem = sanitizePresetBaseName(base_name);
    QString path = dir + QLatin1Char('/') + stem + QStringLiteral(".json");
    for (int i = 0; QFile::exists(path); ++i)
      path = dir + QLatin1Char('/') + stem + QStringLiteral("_") + QString::number(i + 1) + QStringLiteral(".json");
    return path;
  }
}

namespace Noggit::Ui
{
  /// Editable falloff plot: horizontal = radius t in [0,1], vertical = strength [0,1].
  class RadialFalloffCurveEditorWidget final : public QWidget
  {
  public:
    explicit RadialFalloffCurveEditorWidget(BrushFalloffCurve& curve, std::function<void()> onChanged, QWidget* parent = nullptr)
      : QWidget(parent)
      , _curve(curve)
      , _on_changed(std::move(onChanged))
    {
      setMinimumHeight(108);
      setMinimumWidth(200);
      setMaximumHeight(140);
      setMouseTracking(true);
      setToolTip(tr("Drag control points. Horizontal: distance from brush center to edge. Vertical: strength multiplier."));
      reloadFromCurve();
    }

    void reloadFromCurve()
    {
      _pts = _curve.controlPoints();
      if (_pts.size() < 2)
        _pts = { glm::vec2(0.f, 1.f), glm::vec2(1.f, 0.f) };
      _drag_index = -1;
      update();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.fillRect(rect(), QColor(32, 32, 36));

      QRect const plot = plotRect();
      p.setPen(QPen(QColor(90, 90, 98), 1));
      p.drawRect(plot);

      auto to_px = [&](glm::vec2 const& pt) -> QPointF {
        return { plot.left() + pt.x * plot.width(), plot.bottom() - pt.y * plot.height() };
      };

      QPainterPath path;
      path.moveTo(to_px(_pts.front()));
      for (std::size_t i = 1; i < _pts.size(); ++i)
        path.lineTo(to_px(_pts[i]));
      p.setPen(QPen(QColor(120, 190, 255), 2));
      p.drawPath(path);

      p.setPen(QPen(Qt::white, 1));
      p.setBrush(QColor(200, 200, 210));
      for (std::size_t i = 0; i < _pts.size(); ++i)
      {
        QPointF const c = to_px(_pts[i]);
        float const r = (static_cast<int>(i) == _drag_index) ? 6.f : 4.f;
        p.drawEllipse(c, r, r);
      }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
      if (!isEnabled() || event->button() != Qt::LeftButton)
      {
        QWidget::mousePressEvent(event);
        return;
      }
      QRect const plot = plotRect();
      QPointF const m(event->pos());
      _drag_index = hitTest(m, plot);
      if (_drag_index < 0)
      {
        glm::vec2 const t = from_px(m, plot);
        _pts.push_back(t);
        std::sort(_pts.begin(), _pts.end(), [](glm::vec2 const& a, glm::vec2 const& b) { return a.x < b.x; });
        _drag_index = nearestIndex(t);
        commit();
      }
      update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
      if (!isEnabled() || !(event->buttons() & Qt::LeftButton) || _drag_index < 0)
      {
        QWidget::mouseMoveEvent(event);
        return;
      }
      QRect const plot = plotRect();
      glm::vec2 t = from_px(event->pos(), plot);
      t.x = std::clamp(t.x, 0.f, 1.f);
      t.y = std::clamp(t.y, 0.f, 1.f);
      if (_drag_index == 0)
        t.x = 0.f;
      if (_drag_index == static_cast<int>(_pts.size() - 1))
        t.x = 1.f;
      _pts[static_cast<std::size_t>(_drag_index)] = t;
      std::sort(_pts.begin(), _pts.end(), [](glm::vec2 const& a, glm::vec2 const& b) { return a.x < b.x; });
      _drag_index = nearestIndex(t);
      commit();
      update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
      (void)event;
      _drag_index = -1;
      update();
    }

    QSize sizeHint() const override
    {
      return { 240, 118 };
    }

  private:
    [[nodiscard]] QRect plotRect() const
    {
      return rect().adjusted(8, 6, -8, -10);
    }

    void commit()
    {
      _curve.setControlPoints(_pts);
      if (_on_changed)
        _on_changed();
    }

    [[nodiscard]] int hitTest(QPointF const& m, QRect const& plot) const
    {
      auto to_px = [&](glm::vec2 const& pt) -> QPointF {
        return { plot.left() + pt.x * plot.width(), plot.bottom() - pt.y * plot.height() };
      };
      for (int i = static_cast<int>(_pts.size()) - 1; i >= 0; --i)
      {
        QPointF const c = to_px(_pts[static_cast<std::size_t>(i)]);
        if (QLineF(m, c).length() <= 9.0)
          return i;
      }
      return -1;
    }

    [[nodiscard]] int nearestIndex(glm::vec2 const& t) const
    {
      int best = 0;
      float best_d = 1e9f;
      for (int i = 0; i < static_cast<int>(_pts.size()); ++i)
      {
        glm::vec2 const d = _pts[static_cast<std::size_t>(i)] - t;
        float const dd = d.x * d.x + d.y * d.y;
        if (dd < best_d)
        {
          best_d = dd;
          best = i;
        }
      }
      return best;
    }

    [[nodiscard]] static glm::vec2 from_px(QPointF const& m, QRect const& plot)
    {
      float const x = static_cast<float>((m.x() - plot.left()) / std::max(1, plot.width()));
      float const y = static_cast<float>((plot.bottom() - m.y()) / std::max(1, plot.height()));
      return { std::clamp(x, 0.f, 1.f), std::clamp(y, 0.f, 1.f) };
    }

    BrushFalloffCurve& _curve;
    std::function<void()> _on_changed;
    std::vector<glm::vec2> _pts;
    int _drag_index = -1;
  };

  [[nodiscard]] RadialFalloffCurveEditorWidget* curveEditorWidget(QWidget* widget)
  {
    return dynamic_cast<RadialFalloffCurveEditorWidget*>(widget);
  }
}

namespace Noggit::Ui
{
  QString TerrainUnifiedToolWidget::radialPresetDirectory()
  {
    return radialFalloffPresetRoot();
  }

  TerrainUnifiedToolWidget::TerrainUnifiedToolWidget(MapView* map_view, QWidget* parent)
    : QWidget(parent)
    , _map_view(map_view)
  {
    _radial_curve = BrushFalloffCurve::makeDefault();
    _radial_curve.setEnabled(true);
    buildUi();
    updateCurveInteractionState();
    refreshRadialPresetList();
  }

  bool TerrainUnifiedToolWidget::customRadialFalloffEnabled() const
  {
    return _custom_falloff_chk && _custom_falloff_chk->isChecked();
  }

  Noggit::BrushFalloffCurve const* TerrainUnifiedToolWidget::activeRadialFalloff() const
  {
    return customRadialFalloffEnabled() ? &_radial_curve : nullptr;
  }

  void TerrainUnifiedToolWidget::setActiveFamily(Family f)
  {
    int const idx = (f == Family::Sculpt) ? 0 : 1;
    if (_family == f && _stack && _stack->currentIndex() == idx)
      return;

    _family = f;

    if (_stack)
      _stack->setCurrentIndex(idx);

    if (_family_buttons)
    {
      if (auto* btn = _family_buttons->button(static_cast<int>(f)))
      {
        QSignalBlocker const b(_family_buttons);
        btn->setChecked(true);
      }
    }

    emit activeFamilyChanged(static_cast<int>(f));
  }

  void TerrainUnifiedToolWidget::syncRadialFalloffToTools()
  {
    if (_flattenTool)
    {
      bool const on = customRadialFalloffEnabled();
      _radial_curve.setEnabled(on);
      _flattenTool->setRadialFalloffCurve(on ? &_radial_curve : nullptr);
    }
  }

  QJsonObject TerrainUnifiedToolWidget::toJSON() const
  {
    QJsonObject o;
    o.insert(QStringLiteral("family"), static_cast<int>(_family));
    o.insert(QStringLiteral("radial"), _radial_curve.toJson());
    if (_terrainTool)
      o.insert(QStringLiteral("sculpt"), _terrainTool->toJSON());
    if (_flattenTool)
      o.insert(QStringLiteral("surface"), _flattenTool->toJSON());
    if (_custom_falloff_chk)
      o.insert(QStringLiteral("custom_falloff"), _custom_falloff_chk->isChecked());
    return o;
  }

  void TerrainUnifiedToolWidget::fromJSON(QJsonObject const& json)
  {
    int const fam = json.value(QStringLiteral("family")).toInt(0);
    setActiveFamily(fam == 1 ? Family::Surface : Family::Sculpt);

    if (json.contains(QStringLiteral("radial")))
      _radial_curve.fromJson(json.value(QStringLiteral("radial")).toObject());

    if (_custom_falloff_chk)
    {
      QSignalBlocker b(_custom_falloff_chk);
      _custom_falloff_chk->setChecked(json.value(QStringLiteral("custom_falloff")).toBool(true));
    }
    _radial_curve.setEnabled(customRadialFalloffEnabled());

    if (_terrainTool && json.contains(QStringLiteral("sculpt")))
      _terrainTool->fromJSON(json.value(QStringLiteral("sculpt")).toObject());
    if (_flattenTool && json.contains(QStringLiteral("surface")))
      _flattenTool->fromJSON(json.value(QStringLiteral("surface")).toObject());

    if (_unified_radius && _terrainTool)
    {
      QSignalBlocker br(_unified_radius);
      _unified_radius->setValue(_terrainTool->brushRadius());
    }
    reloadCurveEditorFromCurve();
    refreshRadialPresetList();
    updateCurveInteractionState();
    syncRadialFalloffToTools();
  }

  void TerrainUnifiedToolWidget::updateCurveInteractionState()
  {
    bool const on = customRadialFalloffEnabled();
    if (auto* editor = curveEditorWidget(_curve_editor))
      editor->setEnabled(on);
    if (_radial_preset_list)
      _radial_preset_list->setEnabled(on);
    if (_save_radial_preset_btn)
      _save_radial_preset_btn->setEnabled(on);
    if (_import_radial_preset_btn)
      _import_radial_preset_btn->setEnabled(on);
    if (_open_radial_presets_folder_btn)
      _open_radial_presets_folder_btn->setEnabled(true);
    if (_delete_radial_preset_btn)
    {
      bool can_delete = on && _radial_preset_list && _radial_preset_list->currentItem()
          && !_radial_preset_list->currentItem()->data(Qt::UserRole).toString().isEmpty();
      _delete_radial_preset_btn->setEnabled(can_delete);
    }
  }

  void TerrainUnifiedToolWidget::reloadCurveEditorFromCurve()
  {
    if (auto* editor = curveEditorWidget(_curve_editor))
      editor->reloadFromCurve();
  }

  void TerrainUnifiedToolWidget::applyRadialCurveFromPresetItem(QListWidgetItem* item)
  {
    if (!item)
      return;
    QString const path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
    {
      _radial_curve = BrushFalloffCurve::makeDefault();
    }
    else
    {
      QJsonObject root;
      if (!readPresetFile(path, &root))
      {
        QMessageBox::warning(this, tr("Radial preset"), tr("Could not read preset file:\n%1").arg(path));
        return;
      }
      _radial_curve.fromJson(root.value(QStringLiteral("curve")).toObject());
    }
    _radial_curve.setEnabled(customRadialFalloffEnabled());
    reloadCurveEditorFromCurve();
    syncRadialFalloffToTools();
  }

  void TerrainUnifiedToolWidget::refreshRadialPresetList()
  {
    if (!_radial_preset_list)
      return;

    QSignalBlocker const block(_radial_preset_list);
    _radial_preset_list->clear();

    QSize const icon_sz(88, 40);
    _radial_preset_list->setIconSize(icon_sz);

    {
      Noggit::BrushFalloffCurve const def = Noggit::BrushFalloffCurve::makeDefault();
      std::vector<glm::vec2> const& pts = def.controlPoints();
      auto* it = new QListWidgetItem(QIcon(curvePreviewPixmap(pts, icon_sz)), tr("Factory default"));
      it->setData(Qt::UserRole, QString());
      it->setToolTip(tr("Built-in falloff curve."));
      _radial_preset_list->addItem(it);
    }

    QDir const dir(radialFalloffPresetRoot());
    QStringList const names = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    for (QString const& fn : names)
    {
      QString const path = dir.filePath(fn);
      if (!readPresetFile(path, nullptr))
        continue;
      QString const label = presetDisplayNameFromFile(path);
      QJsonObject root;
      readPresetFile(path, &root);
      Noggit::BrushFalloffCurve c;
      c.fromJson(root.value(QStringLiteral("curve")).toObject());
      std::vector<glm::vec2> const pts(c.controlPoints().begin(), c.controlPoints().end());
      auto* it = new QListWidgetItem(QIcon(curvePreviewPixmap(pts, icon_sz)), label);
      it->setData(Qt::UserRole, path);
      it->setToolTip(path);
      _radial_preset_list->addItem(it);
    }

    if (_radial_preset_list->count() > 0)
      _radial_preset_list->setCurrentRow(0);
  }

  void TerrainUnifiedToolWidget::onRadialPresetSelectionChanged()
  {
    applyRadialCurveFromPresetItem(_radial_preset_list ? _radial_preset_list->currentItem() : nullptr);
    updateCurveInteractionState();
  }

  void TerrainUnifiedToolWidget::onSaveRadialPresetClicked()
  {
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Save radial preset"), tr("Preset name:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
      return;

    QString const dir = radialFalloffPresetRoot();
    QString const path = uniquePresetPath(dir, name.trimmed());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
      QMessageBox::warning(this, tr("Radial preset"), tr("Could not write:\n%1").arg(path));
      return;
    }
    QJsonDocument const doc(buildPresetDocument(name.trimmed(), _radial_curve));
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    refreshRadialPresetList();
    for (int i = 0; i < _radial_preset_list->count(); ++i)
    {
      auto* it = _radial_preset_list->item(i);
      if (it && it->data(Qt::UserRole).toString() == path)
      {
        _radial_preset_list->setCurrentItem(it);
        break;
      }
    }
    updateCurveInteractionState();
  }

  void TerrainUnifiedToolWidget::onImportRadialPresetClicked()
  {
    QString const src = QFileDialog::getOpenFileName(this, tr("Import radial preset"), QString(),
                                                     tr("JSON preset (*.json);;All files (*)"));
    if (src.isEmpty())
      return;
    if (!readPresetFile(src, nullptr))
    {
      QMessageBox::warning(this, tr("Radial preset"),
                           tr("Not a valid Noggit radial falloff preset (wrong format or version)."));
      return;
    }

    QJsonObject root;
    readPresetFile(src, &root);
    QString const display = root.value(QStringLiteral("name")).toString().trimmed();
    QString const dest = uniquePresetPath(radialFalloffPresetRoot(), display.isEmpty() ? QFileInfo(src).completeBaseName() : display);

    if (!QFile::copy(src, dest))
    {
      QFile in(src);
      if (!in.open(QIODevice::ReadOnly))
      {
        QMessageBox::warning(this, tr("Radial preset"), tr("Could not read source file."));
        return;
      }
      QFile out(dest);
      if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
      {
        QMessageBox::warning(this, tr("Radial preset"), tr("Could not write:\n%1").arg(dest));
        return;
      }
      out.write(in.readAll());
    }

    refreshRadialPresetList();
    for (int i = 0; i < _radial_preset_list->count(); ++i)
    {
      auto* it = _radial_preset_list->item(i);
      if (it && it->data(Qt::UserRole).toString() == dest)
      {
        _radial_preset_list->setCurrentItem(it);
        break;
      }
    }
    updateCurveInteractionState();
  }

  void TerrainUnifiedToolWidget::onOpenRadialPresetsFolderClicked()
  {
    QDesktopServices::openUrl(QUrl::fromLocalFile(radialFalloffPresetRoot()));
  }

  void TerrainUnifiedToolWidget::onDeleteRadialPresetClicked()
  {
    if (!_radial_preset_list)
      return;
    auto* cur = _radial_preset_list->currentItem();
    if (!cur)
      return;
    QString const path = cur->data(Qt::UserRole).toString();
    if (path.isEmpty())
      return;

    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete this preset file?\n%1").arg(path))
        != QMessageBox::Yes)
      return;

    if (!QFile::remove(path))
    {
      QMessageBox::warning(this, tr("Radial preset"), tr("Could not delete file."));
      return;
    }
    refreshRadialPresetList();
    updateCurveInteractionState();
  }

  void TerrainUnifiedToolWidget::buildUi()
  {
    auto* root = new QVBoxLayout(this);
    root->setAlignment(Qt::AlignTop);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* family_row = new QHBoxLayout();
    family_row->addWidget(new QLabel(tr("Mode:"), this));
    _family_buttons = new QButtonGroup(this);
    auto* sculpt_btn = new QPushButton(tr("Sculpt"), this);
    auto* surface_btn = new QPushButton(tr("Surface"), this);
    sculpt_btn->setCheckable(true);
    surface_btn->setCheckable(true);
    _family_buttons->addButton(sculpt_btn, static_cast<int>(Family::Sculpt));
    _family_buttons->addButton(surface_btn, static_cast<int>(Family::Surface));
    sculpt_btn->setChecked(true);
    family_row->addWidget(sculpt_btn);
    family_row->addWidget(surface_btn);
    root->addLayout(family_row);

    connect(_family_buttons, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked), this,
            [this](QAbstractButton* btn) {
              int const id = _family_buttons->id(btn);
              setActiveFamily(id == static_cast<int>(Family::Surface) ? Family::Surface : Family::Sculpt);
            });

    auto* radius_row = new QHBoxLayout();
    _unified_radius = new Noggit::Ui::Tools::UiCommon::ExtendedSlider(this);
    _unified_radius->setTabletSupportEnabled(false);
    _unified_radius->setRange(0, 1000);
    _unified_radius->setPrefix(tr("Radius:"));
    _unified_radius->setDecimals(2);
    _unified_radius->setValue(15);
    radius_row->addWidget(_unified_radius, 1);
    root->addLayout(radius_row);

    connect(_unified_radius, &Noggit::Ui::Tools::UiCommon::ExtendedSlider::valueChanged, this,
            &TerrainUnifiedToolWidget::onUnifiedRadiusChanged);

    _custom_falloff_chk = new QCheckBox(tr("Custom radial falloff"), this);
    _custom_falloff_chk->setChecked(true);
    root->addWidget(_custom_falloff_chk);
    connect(_custom_falloff_chk, &QCheckBox::stateChanged, this, &TerrainUnifiedToolWidget::onCustomFalloffToggled);

    _curve_editor_section = new QWidget(this);
    auto* curve_outer = new QVBoxLayout(_curve_editor_section);
    curve_outer->setContentsMargins(0, 2, 0, 0);
    curve_outer->setSpacing(4);

    auto* divider = new QFrame(_curve_editor_section);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    curve_outer->addWidget(divider);

    auto* cap = new QLabel(tr("<b>Radial falloff</b> — each file is one shareable <code>.json</code> preset."), _curve_editor_section);
    cap->setTextFormat(Qt::RichText);
    cap->setWordWrap(true);
    curve_outer->addWidget(cap);

    _radial_preset_list = new QListWidget(_curve_editor_section);
    _radial_preset_list->setMinimumHeight(96);
    _radial_preset_list->setMaximumHeight(160);
    _radial_preset_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    curve_outer->addWidget(_radial_preset_list);
    connect(_radial_preset_list, &QListWidget::currentItemChanged, this,
            &TerrainUnifiedToolWidget::onRadialPresetSelectionChanged);

    auto* preset_btn_row = new QHBoxLayout();
    _save_radial_preset_btn = new QPushButton(tr("Save…"), _curve_editor_section);
    _save_radial_preset_btn->setToolTip(tr("Save the current curve as a new named preset (.json)."));
    _import_radial_preset_btn = new QPushButton(tr("Import…"), _curve_editor_section);
    _import_radial_preset_btn->setToolTip(tr("Copy a preset .json into the shared presets folder."));
    _open_radial_presets_folder_btn = new QPushButton(tr("Folder"), _curve_editor_section);
    _open_radial_presets_folder_btn->setToolTip(tr("Open the presets folder (copy files to share)."));
    _delete_radial_preset_btn = new QPushButton(tr("Delete"), _curve_editor_section);
    _delete_radial_preset_btn->setToolTip(tr("Delete the selected preset file."));
    preset_btn_row->addWidget(_save_radial_preset_btn);
    preset_btn_row->addWidget(_import_radial_preset_btn);
    preset_btn_row->addWidget(_open_radial_presets_folder_btn);
    preset_btn_row->addWidget(_delete_radial_preset_btn);
    preset_btn_row->addStretch(1);
    curve_outer->addLayout(preset_btn_row);

    connect(_save_radial_preset_btn, &QPushButton::clicked, this, &TerrainUnifiedToolWidget::onSaveRadialPresetClicked);
    connect(_import_radial_preset_btn, &QPushButton::clicked, this, &TerrainUnifiedToolWidget::onImportRadialPresetClicked);
    connect(_open_radial_presets_folder_btn, &QPushButton::clicked, this,
            &TerrainUnifiedToolWidget::onOpenRadialPresetsFolderClicked);
    connect(_delete_radial_preset_btn, &QPushButton::clicked, this, &TerrainUnifiedToolWidget::onDeleteRadialPresetClicked);

    _curve_editor = new RadialFalloffCurveEditorWidget(
      _radial_curve,
      [this] { syncRadialFalloffToTools(); },
      _curve_editor_section);
    curve_outer->addWidget(_curve_editor);

    root->addWidget(_curve_editor_section);

    _stack = new QStackedWidget(this);
    _terrainTool = new TerrainTool(_map_view, this, false, true);
    _flattenTool = new flatten_blur_tool(_map_view, this);

    _terrainTool->getRadiusSlider()->hide();
    _flattenTool->getRadiusSlider()->hide();

    _stack->addWidget(_terrainTool);
    _stack->addWidget(_flattenTool);
    root->addWidget(_stack);

    applyUnifiedRadius(_unified_radius->value());
    syncRadialFalloffToTools();
  }

  void TerrainUnifiedToolWidget::onCustomFalloffToggled(int state)
  {
    (void)state;
    _radial_curve.setEnabled(customRadialFalloffEnabled());
    updateCurveInteractionState();
    if (customRadialFalloffEnabled())
      reloadCurveEditorFromCurve();
    syncRadialFalloffToTools();
  }

  void TerrainUnifiedToolWidget::onUnifiedRadiusChanged(double v)
  {
    applyUnifiedRadius(v);
  }

  void TerrainUnifiedToolWidget::applyUnifiedRadius(double v)
  {
    if (_terrainTool)
      _terrainTool->setRadius(static_cast<float>(v));
    if (_flattenTool)
      _flattenTool->setRadius(static_cast<float>(v));
  }
}
