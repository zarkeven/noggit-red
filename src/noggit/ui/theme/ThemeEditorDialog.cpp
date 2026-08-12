// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/theme/ThemeEditorDialog.hpp>
#include <noggit/ui/theme/ThemeGenerator.hpp>
#include <noggit/ui/theme/ThemeShareCodec.hpp>

#include <external/qt-color-widgets/qt-color-widgets/color_selector.hpp>

#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

namespace Noggit::Ui::Theme
{
  ThemeEditorDialog::ThemeEditorDialog(QWidget* parent, QString const& initial_theme_name)
    : QDialog(parent)
  {
    setWindowTitle(tr("Theme Editor"));
    setMinimumSize(780, 680);
    resize(860, 760);

    _previous_theme = QSettings().value(QStringLiteral("theme"), QStringLiteral("Dark")).toString();
    _palette = ThemePalette::darkDefaults();

    if (!initial_theme_name.isEmpty()
        && initial_theme_name != QStringLiteral("System"))
    {
      if (auto loaded = ThemeGenerator::loadPaletteFromThemeFolder(initial_theme_name))
      {
        _palette = *loaded;
        _palette.name = initial_theme_name;
      }
      else
      {
        _palette.name = initial_theme_name;
      }
    }

    buildUi();
    setPalette(_palette);
    updateShareText();
  }

  void ThemeEditorDialog::buildUi()
  {
    auto* root = new QVBoxLayout(this);

    auto* name_row = new QHBoxLayout();
    name_row->addWidget(new QLabel(tr("Theme name")));
    _name_edit = new QLineEdit();
    _name_edit->setPlaceholderText(tr("My Theme"));
    name_row->addWidget(_name_edit, 1);
    root->addLayout(name_row);

    auto* middle = new QHBoxLayout();

    auto* colors_group = new QGroupBox(tr("Color palette"));
    auto* colors_layout = new QGridLayout(colors_group);

    int row = 0;
    for (auto const& slot : _palette.colorSlots())
    {
      auto* label = new QLabel(QString::fromUtf8(slot.label.data()));
      auto* selector = new color_widgets::ColorSelector();
      selector->setMinimumWidth(120);
      selector->setMinimumHeight(24);
      selector->setUpdateMode(color_widgets::ColorSelector::Continuous);

      _color_selectors[row] = selector;
      colors_layout->addWidget(label, row, 0);
      colors_layout->addWidget(selector, row, 1);
      ++row;

      connect(selector, &color_widgets::ColorPreview::colorChanged, this, &ThemeEditorDialog::onColorChanged);
    }

    auto* colors_scroll = new QScrollArea();
    colors_scroll->setWidget(colors_group);
    colors_scroll->setWidgetResizable(true);
    colors_scroll->setFrameShape(QFrame::NoFrame);
    colors_scroll->setMinimumWidth(260);
    middle->addWidget(colors_scroll, 1);

    middle->addWidget(buildPreviewPanel(), 1);
    root->addLayout(middle, 1);

    auto* share_group = new QGroupBox(tr("Share"));
    auto* share_layout = new QVBoxLayout(share_group);

    auto* share_format_row = new QHBoxLayout();
    share_format_row->addWidget(new QLabel(tr("Format")));
    _share_format = new QComboBox();
    _share_format->addItem(tr("Compact code"));
    _share_format->addItem(tr("JSON"));
    share_format_row->addWidget(_share_format, 1);
    share_layout->addLayout(share_format_row);

    _share_text = new QPlainTextEdit();
    _share_text->setPlaceholderText(tr("Paste a theme code here to import"));
    _share_text->setMinimumHeight(80);
    _share_text->setMaximumHeight(120);
    share_layout->addWidget(_share_text);

    auto* share_buttons = new QHBoxLayout();
    auto* copy_button = new QPushButton(tr("Copy"));
    auto* import_button = new QPushButton(tr("Import"));
    share_buttons->addWidget(copy_button);
    share_buttons->addWidget(import_button);
    share_buttons->addStretch();
    share_layout->addLayout(share_buttons);

    root->addWidget(share_group);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch();
    auto* apply_button = new QPushButton(tr("Apply preview"));
    _save_button = new QPushButton(tr("Save theme"));
    auto* cancel_button = new QPushButton(tr("Close"));
    bottom->addWidget(apply_button);
    bottom->addWidget(_save_button);
    bottom->addWidget(cancel_button);
    root->addLayout(bottom);

    connect(_name_edit, &QLineEdit::textChanged, this, &ThemeEditorDialog::onNameChanged);
    connect(_share_format, qOverload<int>(&QComboBox::currentIndexChanged), this, &ThemeEditorDialog::onShareFormatChanged);
    connect(copy_button, &QPushButton::clicked, this, &ThemeEditorDialog::copyShareCode);
    connect(import_button, &QPushButton::clicked, this, &ThemeEditorDialog::importShareCode);
    connect(apply_button, &QPushButton::clicked, this, &ThemeEditorDialog::applyPreview);
    connect(_save_button, &QPushButton::clicked, this, &ThemeEditorDialog::saveTheme);
    connect(cancel_button, &QPushButton::clicked, this, &ThemeEditorDialog::reject);
  }

  QWidget* ThemeEditorDialog::buildPreviewPanel()
  {
    auto* preview_group = new QGroupBox(tr("Preview"));
    auto* layout = new QVBoxLayout(preview_group);

    auto* hint = new QLabel(tr("Sample controls update with the live theme."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* tabs = new QTabWidget();
    auto* tab_controls = new QWidget();
    auto* tab_layout = new QVBoxLayout(tab_controls);

    auto* buttons = new QHBoxLayout();
    auto* normal_btn = new QPushButton(tr("Button"));
    auto* checked_btn = new QPushButton(tr("Checked"));
    checked_btn->setCheckable(true);
    checked_btn->setChecked(true);
    auto* disabled_btn = new QPushButton(tr("Disabled"));
    disabled_btn->setEnabled(false);
    buttons->addWidget(normal_btn);
    buttons->addWidget(checked_btn);
    buttons->addWidget(disabled_btn);
    tab_layout->addLayout(buttons);

    auto* line = new QLineEdit(tr("Sample text field"));
    tab_layout->addWidget(line);

    auto* combo = new QComboBox();
    combo->addItems({tr("Option A"), tr("Option B"), tr("Option C")});
    tab_layout->addWidget(combo);

    auto* spin = new QSpinBox();
    spin->setRange(0, 100);
    spin->setValue(42);
    tab_layout->addWidget(spin);

    auto* slider_row = new QHBoxLayout();
    slider_row->addWidget(new QLabel(tr("Slider")));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(65);
    slider_row->addWidget(slider, 1);
    tab_layout->addLayout(slider_row);

    auto* progress = new QProgressBar();
    progress->setRange(0, 100);
    progress->setValue(40);
    progress->setTextVisible(true);
    tab_layout->addWidget(progress);

    auto* checks = new QHBoxLayout();
    auto* check = new QCheckBox(tr("Checkbox"));
    check->setChecked(true);
    auto* check_off = new QCheckBox(tr("Unchecked"));
    auto* radio_a = new QRadioButton(tr("Radio A"));
    radio_a->setChecked(true);
    auto* radio_b = new QRadioButton(tr("Radio B"));
    checks->addWidget(check);
    checks->addWidget(check_off);
    checks->addWidget(radio_a);
    checks->addWidget(radio_b);
    tab_layout->addLayout(checks);

    tab_layout->addStretch();
    tabs->addTab(tab_controls, tr("Controls"));

    auto* tab_groups = new QWidget();
    auto* groups_layout = new QVBoxLayout(tab_groups);
    auto* nested = new QGroupBox(tr("Nested group"));
    auto* nested_layout = new QVBoxLayout(nested);
    nested_layout->addWidget(new QLabel(tr("Labels, group titles, and borders")));
    auto* nested_slider = new QSlider(Qt::Horizontal);
    nested_slider->setValue(25);
    nested_layout->addWidget(nested_slider);
    groups_layout->addWidget(nested);
    groups_layout->addStretch();
    tabs->addTab(tab_groups, tr("Groups"));

    layout->addWidget(tabs, 1);
    preview_group->setMinimumWidth(320);
    return preview_group;
  }

  ThemePalette ThemeEditorDialog::currentPalette() const
  {
    ThemePalette palette = _palette;
    palette.name = _name_edit->text();

    int i = 0;
    for (auto const& slot : palette.colorSlots())
    {
      if (_color_selectors[i])
        *slot.color = _color_selectors[i]->color();
      ++i;
    }

    return palette;
  }

  void ThemeEditorDialog::setPalette(ThemePalette const& palette)
  {
    _palette = palette;

    QSignalBlocker name_blocker(_name_edit);
    _name_edit->setText(palette.name);

    int i = 0;
    for (auto const& slot : palette.colorSlotViews())
    {
      if (_color_selectors[i])
      {
        QSignalBlocker blocker(_color_selectors[i]);
        _color_selectors[i]->setColor(*slot.color);
      }
      ++i;
    }
  }

  void ThemeEditorDialog::onColorChanged()
  {
    _palette = currentPalette();
    updateShareText();

    if (!_preview_debounce)
    {
      _preview_debounce = new QTimer(this);
      _preview_debounce->setSingleShot(true);
      _preview_debounce->setInterval(150);
      connect(_preview_debounce, &QTimer::timeout, this, &ThemeEditorDialog::applyLivePreview);
    }
    _preview_debounce->start();
  }

  void ThemeEditorDialog::onNameChanged()
  {
    _palette.name = _name_edit->text();
    updateShareText();
  }

  void ThemeEditorDialog::onShareFormatChanged(int)
  {
    updateShareText();
  }

  void ThemeEditorDialog::updateShareText()
  {
    ThemeShareFormat const format = _share_format->currentIndex() == 0
                                      ? ThemeShareFormat::Compact
                                      : ThemeShareFormat::Json;

    QSignalBlocker blocker(_share_text);
    _share_text->setPlainText(ThemeShareCodec::encode(currentPalette(), format));
  }

  void ThemeEditorDialog::copyShareCode()
  {
    QApplication::clipboard()->setText(_share_text->toPlainText());
  }

  void ThemeEditorDialog::importShareCode()
  {
    QString error;
    auto imported = ThemeShareCodec::decode(_share_text->toPlainText(), &error);
    if (!imported)
    {
      QMessageBox::warning(this, tr("Import failed"), error);
      return;
    }

    setPalette(*imported);
    _palette = *imported;
    updateShareText();
    applyLivePreview();

    QMessageBox::information(this, tr("Theme imported"), tr("Palette loaded from share code."));
  }

  void ThemeEditorDialog::applyLivePreview()
  {
    ThemePalette const palette = currentPalette();
    QString const qss = ThemeGenerator::generateQss(palette, ThemeGenerator::sanitizeThemeName(palette.name));
    if (qss.isEmpty())
      return;

    ThemeGenerator::applyStylesheet(qss);
    _preview_active = true;
  }

  void ThemeEditorDialog::applyPreview()
  {
    applyLivePreview();
  }

  void ThemeEditorDialog::saveTheme()
  {
    ThemePalette palette = currentPalette();
    palette.name = ThemeGenerator::sanitizeThemeName(palette.name);
    _name_edit->setText(palette.name);

    if (ThemeGenerator::isBuiltInTheme(palette.name))
    {
      auto const answer = QMessageBox::question(
        this,
        tr("Built-in theme name"),
        tr("Built-in themes cannot be overwritten. Save as a different name?"),
        QMessageBox::Yes | QMessageBox::No);

      if (answer != QMessageBox::Yes)
        return;

      palette.name = palette.name + QStringLiteral("_Custom");
      _name_edit->setText(palette.name);
    }

    ThemeInstallResult const result = ThemeGenerator::installTheme(palette);
    if (!result.success)
    {
      QMessageBox::warning(this, tr("Save failed"), result.error);
      return;
    }

    ThemeGenerator::applyStylesheet(ThemeGenerator::generateQss(palette, result.theme_name));
    _preview_active = false;
    _theme_was_saved = true;
    _saved_theme_name = result.theme_name;

    emit themeInstalled(result.theme_name);
    accept();
  }

  void ThemeEditorDialog::restorePreviousTheme()
  {
    if (!_preview_active)
      return;

    if (_previous_theme == QStringLiteral("System"))
    {
      qApp->setStyleSheet(QString());
      return;
    }

    QFile file(QStringLiteral("./themes/") + _previous_theme + QStringLiteral("/theme.qss"));
    if (!file.open(QIODevice::ReadOnly))
      return;

    ThemeGenerator::applyStylesheet(QString::fromUtf8(file.readAll()));
  }

  void ThemeEditorDialog::reject()
  {
    restorePreviousTheme();
    QDialog::reject();
  }
}
