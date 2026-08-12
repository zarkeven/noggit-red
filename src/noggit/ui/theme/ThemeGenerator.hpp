// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ui/theme/ThemePalette.hpp>

#include <QtCore/QString>

#include <optional>

namespace Noggit::Ui::Theme
{
  struct ThemeInstallResult
  {
    bool success = false;
    QString theme_name;
    QString error;
  };

  struct ThemeGenerator
  {
    static QString templatePath();
    static QString sanitizeThemeName(QString const& name);
    static bool isBuiltInTheme(QString const& name);

    static QString generateQss(ThemePalette const& palette, QString const& theme_folder_name);
    static QByteArray generateNodesThemeJson(ThemePalette const& palette);
    static QByteArray generatePaletteJson(ThemePalette const& palette);

    static QString applyStylesheet(QString const& qss);
    static ThemeInstallResult installTheme(ThemePalette const& palette, bool allow_overwrite_built_in = false);
    static std::optional<ThemePalette> loadPaletteFromThemeFolder(QString const& theme_name);
  };
}
