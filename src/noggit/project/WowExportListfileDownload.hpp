// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <QString>
#include <filesystem>

namespace Noggit::Project
{
  //! Fetch `id;path` listfile CSV from wow.export (kruithne.net). @p url_template must contain `%s` for the build slug.
  bool wow_export_download_listfile_csv (QString const& url_template
                                        , QString const& build_slug
                                        , std::filesystem::path const& dest_csv
                                        , QString* error_message);
}
