#include <noggit/ui/FontAwesome.hpp>
#include <noggit/ui/windows/projectSelection/widgets/ProjectListItem.hpp>

#include <QGridLayout>
#include <QGraphicsColorizeEffect>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QFontMetrics>

#include <algorithm>


namespace Noggit::Ui::Widget
{
  namespace
  {
    void setMouseTransparentRecursive (QWidget* widget)
    {
      if (!widget)
      {
        return;
      }

      widget->setAttribute (Qt::WA_TransparentForMouseEvents, true);

      for (QWidget* child : widget->findChildren<QWidget*>())
      {
        child->setAttribute (Qt::WA_TransparentForMouseEvents, true);
      }
    }
  }

  ProjectListItem::ProjectListItem (ProjectListItemData const& data, QWidget* parent)
    : QWidget (parent)
  {
    auto* layout = new QGridLayout (this);
    layout->setContentsMargins (4, 4, 4, 4);
    layout->setHorizontalSpacing (8);
    layout->setVerticalSpacing (2);

    QIcon icon;
    if (data.project_version == Project::ProjectVersion::WOTLK)
    {
      icon = QIcon (":/icon-wrath");
    }
    if (data.project_version == Project::ProjectVersion::SL)
    {
      icon = QIcon (":/icon-shadow");
    }

    _project_version_icon = new QLabel (this);
    _project_version_icon->setPixmap (icon.pixmap (QSize (48, 48)));
    _project_version_icon->setFixedSize (48, 48);

    auto const project_name = toCamelCase (QString (data.project_name));
    _project_name_label = new QLabel (project_name, this);
    _project_name_label->setObjectName ("project-title-label");
    QFont title_font = _project_name_label->font();
    title_font.setPixelSize (15);
    _project_name_label->setFont (title_font);
    _project_name_label->setMinimumHeight (QFontMetrics (title_font).height() + 2);

    _project_directory_label = new QLabel (data.project_directory, this);
    _project_directory_label->setObjectName ("project-information");
    QFont info_font = _project_directory_label->font();
    info_font.setPixelSize (10);
    _project_directory_label->setFont (info_font);
    int const info_line_h = QFontMetrics (info_font).height() + 1;
    _project_directory_label->setMinimumHeight (info_line_h);
    _project_directory_label->setToolTip (data.project_directory);

    auto* directory_effect = new QGraphicsOpacityEffect (_project_directory_label);
    directory_effect->setOpacity (0.5);
    _project_directory_label->setGraphicsEffect (directory_effect);

    QString version;
    if (data.project_version == Project::ProjectVersion::WOTLK)
    {
      version = "Wrath Of The Lich King";
    }
    if (data.project_version == Project::ProjectVersion::SL)
    {
      version = "Shadowlands";
    }

    _project_version_label = new QLabel (version, this);
    _project_version_label->setObjectName ("project-information");
    _project_version_label->setFont (info_font);
    _project_version_label->setMinimumHeight (info_line_h);

    auto* version_effect = new QGraphicsOpacityEffect (_project_version_label);
    version_effect->setOpacity (0.5);
    _project_version_label->setGraphicsEffect (version_effect);

    _project_last_edited_label = new QLabel (data.project_last_edited, this);
    _project_last_edited_label->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
    _project_last_edited_label->setObjectName ("project-information");
    _project_last_edited_label->setFont (info_font);
    _project_last_edited_label->setMinimumHeight (info_line_h);

    auto* last_edited_effect = new QGraphicsOpacityEffect (_project_last_edited_label);
    last_edited_effect->setOpacity (0.5);
    _project_last_edited_label->setGraphicsEffect (last_edited_effect);

    layout->addWidget (_project_version_icon, 0, 0, 3, 1);
    layout->addWidget (_project_name_label, 0, 1);
    layout->addWidget (_project_directory_label, 1, 1);
    layout->addWidget (_project_version_label, 2, 1);
    layout->addWidget (_project_last_edited_label, 2, 2);

    if (data.is_favorite)
    {
      _project_favorite_icon = new QLabel (this);
      _project_favorite_icon->setPixmap (FontAwesomeIcon (FontAwesome::star).pixmap (QSize (16, 16)));
      _project_favorite_icon->setAlignment (Qt::AlignRight | Qt::AlignTop);

      auto* colour = new QGraphicsColorizeEffect (_project_favorite_icon);
      colour->setColor (QColor (255, 204, 0));
      colour->setStrength (1.0f);
      _project_favorite_icon->setGraphicsEffect (colour);

      layout->addWidget (_project_favorite_icon, 0, 2, 1, 1);
    }
    else
    {
      _project_favorite_icon = nullptr;
    }

    // Let clicks reach the QListWidget so items can be selected and double-clicked.
    setMouseTransparentRecursive (this);

    int const text_block_h = _project_name_label->minimumHeight()
                           + _project_directory_label->minimumHeight()
                           + _project_version_label->minimumHeight()
                           + layout->verticalSpacing() * 2;
    int const row_h = std::max (48, text_block_h) + layout->contentsMargins().top()
                    + layout->contentsMargins().bottom();
    _preferred_size = QSize (380, row_h);
  }

  QSize ProjectListItem::sizeHint() const
  {
    return _preferred_size;
  }

  QSize ProjectListItem::minimumSizeHint() const
  {
    return _preferred_size;
  }

  QString ProjectListItem::toCamelCase (QString const& s)
  {
    QStringList parts = s.split (' ', Qt::SplitBehaviorFlags::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
    {
      parts[i].replace (0, 1, parts[i][0].toUpper());
    }

    return parts.join (" ");
  }
}
