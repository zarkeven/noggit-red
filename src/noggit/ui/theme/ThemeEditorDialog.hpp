// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ui/theme/ThemePalette.hpp>

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace color_widgets
{
  class ColorSelector;
}

namespace Noggit::Ui::Theme
{
  class ThemeEditorDialog : public QDialog
  {
    Q_OBJECT

  public:
    explicit ThemeEditorDialog(QWidget* parent = nullptr, QString const& initial_theme_name = {});

    QString savedThemeName() const { return _saved_theme_name; }
    bool themeWasSaved() const { return _theme_was_saved; }

  signals:
    void themeInstalled(QString const& theme_name);

  private slots:
    void onColorChanged();
    void onNameChanged();
    void onShareFormatChanged(int index);
    void updateShareText();
    void copyShareCode();
    void importShareCode();
    void applyPreview();
    void saveTheme();
    void reject() override;

  private:
    void buildUi();
    QWidget* buildPreviewPanel();
    ThemePalette currentPalette() const;
    void setPalette(ThemePalette const& palette);
    void restorePreviousTheme();
    void applyLivePreview();

    ThemePalette _palette;
    QString _previous_theme;
    QString _saved_theme_name;
    bool _theme_was_saved = false;
    bool _preview_active = false;

    QLineEdit* _name_edit = nullptr;
    QComboBox* _share_format = nullptr;
    QPlainTextEdit* _share_text = nullptr;
    QPushButton* _save_button = nullptr;
    QTimer* _preview_debounce = nullptr;

    std::array<color_widgets::ColorSelector*, ThemePalette::k_color_count> _color_selectors{};
  };
}
