// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <cstdint>
#include <string>

namespace Noggit::Integrations
{
  struct DiscordAssets
  {
    std::string large_image_key;
    std::string large_image_text;
  };

  struct DiscordActivity
  {
    std::string details;
    std::string state;
    std::string tool;
    std::int64_t start_timestamp_unix = 0;
  };

  class DiscordRichPresence
  {
  public:
    static DiscordRichPresence& instance();

    void configure(bool enabled, std::string const& app_id);
    void setAssets(DiscordAssets const& assets);
    void setActivity(DiscordActivity activity);
    void shutdown();

  private:
    DiscordRichPresence() = default;

    bool _enabled = false;
    std::string _app_id;
    DiscordAssets _assets;
  };
}
