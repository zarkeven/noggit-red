// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtGui/QColor>

#include <array>
#include <optional>
#include <string_view>

namespace Noggit::Ui::Theme
{
  struct ThemePalette
  {
    static constexpr int k_color_count = 9;

    struct ColorSlot
    {
      std::string_view key;
      std::string_view label;
      std::string_view qss_var;
      QColor* color;
    };

    QColor light{0x37, 0x3b, 0x40};
    QColor half_light{0x2d, 0x2f, 0x34};
    QColor base{0x26, 0x28, 0x2d};
    QColor half_dark{0x1f, 0x20, 0x23};
    QColor dark{0x14, 0x15, 0x17};
    QColor highlight{0x52, 0x81, 0xb9};
    QColor hover{0xff, 0xff, 0xff};
    QColor text{0xd7, 0xd7, 0xd7};
    QColor disabled{0x7f, 0x7f, 0x7f};

    QString name;

    struct ColorSlotView
    {
      std::string_view key;
      std::string_view label;
      std::string_view qss_var;
      QColor const* color;
    };

    static ThemePalette darkDefaults();
    std::array<ColorSlot, k_color_count> colorSlots();
    std::array<ColorSlotView, k_color_count> colorSlotViews() const;

    QJsonObject toJson() const;
    static std::optional<ThemePalette> fromJson(QJsonObject const& obj, QString* error = nullptr);

    static QString colorToHex(QColor const& c);
    static QColor hexToColor(QString const& hex, QColor const& fallback = {});
  };
}
