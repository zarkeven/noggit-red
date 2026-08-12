// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/integrations/DiscordRichPresence.hpp>

namespace Noggit::Integrations
{
  DiscordRichPresence& DiscordRichPresence::instance()
  {
    static DiscordRichPresence inst;
    return inst;
  }

  void DiscordRichPresence::configure(bool enabled, std::string const& app_id)
  {
    _enabled = enabled;
    _app_id = app_id;
  }

  void DiscordRichPresence::setAssets(DiscordAssets const& assets)
  {
    _assets = assets;
  }

  void DiscordRichPresence::setActivity(DiscordActivity activity)
  {
    (void)activity;
  }

  void DiscordRichPresence::shutdown()
  {
    _enabled = false;
  }
}
