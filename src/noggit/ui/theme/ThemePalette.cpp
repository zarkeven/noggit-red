// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/theme/ThemePalette.hpp>

#include <QtCore/QJsonObject>

namespace Noggit::Ui::Theme
{
  ThemePalette ThemePalette::darkDefaults()
  {
    ThemePalette p;
    p.name = QStringLiteral("Custom");
    return p;
  }

  std::array<ThemePalette::ColorSlot, ThemePalette::k_color_count> ThemePalette::colorSlots()
  {
    return {{
      { "light", "Light", "$light", &light },
      { "half_light", "Half light", "$half_light", &half_light },
      { "base", "Base", "$base", &base },
      { "half_dark", "Half dark", "$half_dark", &half_dark },
      { "dark", "Dark", "$dark", &dark },
      { "highlight", "Highlight", "$highlight", &highlight },
      { "hover", "Hover", "$hover", &hover },
      { "text", "Text", "$text", &text },
      { "disabled", "Disabled", "$disabled", &disabled },
    }};
  }

  std::array<ThemePalette::ColorSlotView, ThemePalette::k_color_count> ThemePalette::colorSlotViews() const
  {
    return {{
      { "light", "Light", "$light", &light },
      { "half_light", "Half light", "$half_light", &half_light },
      { "base", "Base", "$base", &base },
      { "half_dark", "Half dark", "$half_dark", &half_dark },
      { "dark", "Dark", "$dark", &dark },
      { "highlight", "Highlight", "$highlight", &highlight },
      { "hover", "Hover", "$hover", &hover },
      { "text", "Text", "$text", &text },
      { "disabled", "Disabled", "$disabled", &disabled },
    }};
  }

  QString ThemePalette::colorToHex(QColor const& c)
  {
    return QStringLiteral("#%1%2%3")
      .arg(c.red(), 2, 16, QChar('0'))
      .arg(c.green(), 2, 16, QChar('0'))
      .arg(c.blue(), 2, 16, QChar('0'));
  }

  QColor ThemePalette::hexToColor(QString const& hex, QColor const& fallback)
  {
    QColor c(hex);
    return c.isValid() ? c : fallback;
  }

  QJsonObject ThemePalette::toJson() const
  {
    QJsonObject colors;
    for (auto const& slot : colorSlotViews())
      colors[QString::fromUtf8(slot.key.data())] = colorToHex(*slot.color);

    QJsonObject root;
    root[QStringLiteral("v")] = 1;
    root[QStringLiteral("name")] = name;
    root[QStringLiteral("colors")] = colors;
    return root;
  }

  std::optional<ThemePalette> ThemePalette::fromJson(QJsonObject const& obj, QString* error)
  {
    auto set_error = [&](QString const& msg)
    {
      if (error)
        *error = msg;
    };

    if (!obj.contains(QStringLiteral("v")) || obj.value(QStringLiteral("v")).toInt() != 1)
    {
      set_error(QStringLiteral("Unsupported theme format version."));
      return std::nullopt;
    }

    QJsonObject const colors = obj.value(QStringLiteral("colors")).toObject();
    if (colors.isEmpty())
    {
      set_error(QStringLiteral("Theme is missing color data."));
      return std::nullopt;
    }

    ThemePalette palette = darkDefaults();
    palette.name = obj.value(QStringLiteral("name")).toString(QStringLiteral("Imported"));

    for (auto const& slot : palette.colorSlots())
    {
      QString const key = QString::fromUtf8(slot.key.data());
      if (!colors.contains(key))
      {
        set_error(QStringLiteral("Theme is missing required color: %1").arg(key));
        return std::nullopt;
      }
      QColor const parsed = hexToColor(colors.value(key).toString());
      if (!parsed.isValid())
      {
        set_error(QStringLiteral("Invalid color value for: %1").arg(key));
        return std::nullopt;
      }
      *slot.color = parsed;
    }

    return palette;
  }
}
