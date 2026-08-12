// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/theme/ThemeGenerator.hpp>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <array>

namespace Noggit::Ui::Theme
{
  namespace
  {
    QString readTemplateFile()
    {
      QFile file(ThemeGenerator::templatePath());
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
      return QString::fromUtf8(file.readAll());
    }

    std::array<int, 3> rgb(QColor const& c)
    {
      return { c.red(), c.green(), c.blue() };
    }

    QJsonArray rgbArray(QColor const& c)
    {
      QJsonArray arr;
      arr.append(c.red());
      arr.append(c.green());
      arr.append(c.blue());
      return arr;
    }
  }

  QString ThemeGenerator::templatePath()
  {
    return QStringLiteral("./themes/_template/theme_template.qss");
  }

  QString ThemeGenerator::sanitizeThemeName(QString const& name)
  {
    QString sanitized = name.trimmed();
    sanitized.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    while (sanitized.startsWith('.'))
      sanitized.remove(0, 1);
    if (sanitized.isEmpty())
      sanitized = QStringLiteral("Custom");
    return sanitized;
  }

  bool ThemeGenerator::isBuiltInTheme(QString const& name)
  {
    return name.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("McNet"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("_template"), Qt::CaseInsensitive) == 0;
  }

  QString ThemeGenerator::generateQss(ThemePalette const& palette, QString const& theme_folder_name)
  {
    QString qss = readTemplateFile();
    if (qss.isEmpty())
    {
      // Fallback: bake from the shipped Dark theme if the authoring template is missing.
      QFile dark(QStringLiteral("./themes/Dark/theme.qss"));
      if (dark.open(QIODevice::ReadOnly | QIODevice::Text))
        qss = QString::fromUtf8(dark.readAll());
    }

    if (qss.isEmpty())
      return {};

    // Drop `$name = #hex;` authoring lines. After substitution they become
    // `#rrggbb = #hex;`, which Qt parses as ID selectors and can invalidate the
    // entire stylesheet (falling back to the light Fusion look).
    static QRegularExpression const decl_line(
      QStringLiteral(R"(^[ \t]*\$[A-Za-z_][A-Za-z0-9_]*[ \t]*=[^\n]*\n?)"),
      QRegularExpression::MultilineOption);
    qss.remove(decl_line);

    // Longest names first so `$half_light` is not corrupted by replacing `$light`.
    struct Replacement
    {
      QString var;
      QString hex;
    };

    std::array<Replacement, ThemePalette::k_color_count> replacements;
    int i = 0;
    for (auto const& slot : palette.colorSlotViews())
    {
      replacements[i++] = {
        QString::fromUtf8(slot.qss_var.data(), static_cast<int>(slot.qss_var.size())),
        ThemePalette::colorToHex(*slot.color)
      };
    }

    std::sort(replacements.begin(), replacements.end(),
              [](Replacement const& a, Replacement const& b) {
                return a.var.size() > b.var.size();
              });

    for (auto const& r : replacements)
      qss.replace(r.var, r.hex);

    QString const image_base = QStringLiteral("themes/Dark/images/");
    qss.replace(QStringLiteral("@theme_images/"), image_base);

    // If we fell back to Dark/theme.qss, keep its image paths as-is (already themes/Dark/...).

    Q_UNUSED(theme_folder_name);
    return qss;
  }

  QByteArray ThemeGenerator::generateNodesThemeJson(ThemePalette const& palette)
  {
    QJsonObject root;

    QJsonObject flow;
    auto const base = rgb(palette.base);
    auto const half_dark = rgb(palette.half_dark);
    auto const light = rgb(palette.light);
    flow[QStringLiteral("BackgroundColor")] = rgbArray(palette.base);
    flow[QStringLiteral("FineGridColor")] = rgbArray(palette.half_light);
    flow[QStringLiteral("CoarseGridColor")] = rgbArray(palette.half_dark);
    root[QStringLiteral("FlowViewStyle")] = flow;

    QJsonObject node;
    node[QStringLiteral("NormalBoundaryColor")] = rgbArray(palette.text);
    node[QStringLiteral("SelectedBoundaryColor")] = rgbArray(palette.highlight);
    node[QStringLiteral("GradientColor0")] = rgbArray(palette.light);
    node[QStringLiteral("GradientColor1")] = rgbArray(palette.light);
    node[QStringLiteral("GradientColor2")] = rgbArray(palette.light);
    node[QStringLiteral("GradientColor3")] = rgbArray(palette.light);
    node[QStringLiteral("ShadowColor")] = rgbArray(palette.dark);
    node[QStringLiteral("FontColor")] = ThemePalette::colorToHex(palette.text);
    node[QStringLiteral("FontColorFaded")] = ThemePalette::colorToHex(palette.disabled);
    node[QStringLiteral("ConnectionPointColor")] = rgbArray(palette.disabled);
    node[QStringLiteral("FilledConnectionPointColor")] = QStringLiteral("cyan");
    node[QStringLiteral("ErrorColor")] = QStringLiteral("red");
    node[QStringLiteral("WarningColor")] = rgbArray(QColor(128, 128, 0));
    node[QStringLiteral("PenWidth")] = 1.0;
    node[QStringLiteral("HoveredPenWidth")] = 1.7;
    node[QStringLiteral("ConnectionPointDiameter")] = 10.0;
    node[QStringLiteral("Opacity")] = 0.8;
    root[QStringLiteral("NodeStyle")] = node;

    QJsonObject connection;
    connection[QStringLiteral("ConstructionColor")] = QStringLiteral("gray");
    connection[QStringLiteral("NormalColor")] = ThemePalette::colorToHex(palette.highlight);
    connection[QStringLiteral("SelectedColor")] = rgbArray(palette.half_light);
    connection[QStringLiteral("SelectedHaloColor")] = ThemePalette::colorToHex(palette.hover);
    connection[QStringLiteral("HoveredColor")] = ThemePalette::colorToHex(palette.text);
    connection[QStringLiteral("LineWidth")] = 3.0;
    connection[QStringLiteral("ConstructionLineWidth")] = 2.0;
    connection[QStringLiteral("PointDiameter")] = 10.0;
    connection[QStringLiteral("UseDataDefinedColors")] = true;
    root[QStringLiteral("ConnectionStyle")] = connection;

    Q_UNUSED(base);
    Q_UNUSED(half_dark);
    Q_UNUSED(light);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
  }

  QByteArray ThemeGenerator::generatePaletteJson(ThemePalette const& palette)
  {
    return QJsonDocument(palette.toJson()).toJson(QJsonDocument::Indented);
  }

  QString ThemeGenerator::applyStylesheet(QString const& qss)
  {
    QString style_sheet_fixed = qss;
    style_sheet_fixed.replace(QStringLiteral("@rpath"), QCoreApplication::applicationDirPath());

    if (style_sheet_fixed.endsWith('/'))
      style_sheet_fixed.chop(1);
    else if (style_sheet_fixed.endsWith('\\'))
      style_sheet_fixed.chop(2);

    qApp->setStyleSheet(style_sheet_fixed);
    return style_sheet_fixed;
  }

  ThemeInstallResult ThemeGenerator::installTheme(ThemePalette const& palette, bool allow_overwrite_built_in)
  {
    ThemeInstallResult result;
    result.theme_name = sanitizeThemeName(palette.name);

    if (isBuiltInTheme(result.theme_name) && !allow_overwrite_built_in)
    {
      result.error = QStringLiteral("Cannot overwrite built-in theme \"%1\". Choose a different name.").arg(result.theme_name);
      return result;
    }

    QString const qss = generateQss(palette, result.theme_name);
    if (qss.isEmpty())
    {
      result.error = QStringLiteral("Theme template not found at %1").arg(templatePath());
      return result;
    }

    QDir const themes_dir(QStringLiteral("./themes/"));
    QString const theme_path = themes_dir.filePath(result.theme_name);
    QDir theme_folder(theme_path);

    if (theme_folder.exists() && isBuiltInTheme(result.theme_name) && !allow_overwrite_built_in)
    {
      result.error = QStringLiteral("Built-in theme folder already exists.");
      return result;
    }

    if (!themes_dir.exists() && !themes_dir.mkpath(QStringLiteral(".")))
    {
      result.error = QStringLiteral("Could not create themes directory.");
      return result;
    }

    if (!theme_folder.exists() && !theme_folder.mkpath(QStringLiteral(".")))
    {
      result.error = QStringLiteral("Could not create theme folder.");
      return result;
    }

    auto write_file = [&](QString const& file_name, QByteArray const& data) -> bool
    {
      QFile file(theme_folder.filePath(file_name));
      if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
      return file.write(data) == data.size();
    };

    if (!write_file(QStringLiteral("theme.qss"), qss.toUtf8()))
    {
      result.error = QStringLiteral("Failed to write theme.qss.");
      return result;
    }

    if (!write_file(QStringLiteral("nodes_theme.json"), generateNodesThemeJson(palette)))
    {
      result.error = QStringLiteral("Failed to write nodes_theme.json.");
      return result;
    }

    if (!write_file(QStringLiteral("palette.json"), generatePaletteJson(palette)))
    {
      result.error = QStringLiteral("Failed to write palette.json.");
      return result;
    }

    result.success = true;
    return result;
  }

  std::optional<ThemePalette> ThemeGenerator::loadPaletteFromThemeFolder(QString const& theme_name)
  {
    if (theme_name.isEmpty() || theme_name == QStringLiteral("System"))
      return std::nullopt;

    QFile file(QDir(QStringLiteral("./themes/")).filePath(theme_name + QStringLiteral("/palette.json")));
    if (!file.open(QIODevice::ReadOnly))
      return std::nullopt;

    QJsonDocument const doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
      return std::nullopt;

    return ThemePalette::fromJson(doc.object());
  }
}
