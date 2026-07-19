// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#ifndef NOGGIT_UTILS_HPP
#define NOGGIT_UTILS_HPP

#include <noggit/application/NoggitApplication.hpp>

#include <ClientData.hpp>
#include <ClientFile.hpp>
#include <Exception.hpp>
#include <Listfile.hpp>
#include <stream/StreamReader.h>

#include <memory>
#include <string>
#include <vector>


inline auto readFileAsIMemStream = [](std::string const& file_path) -> std::shared_ptr<BlizzardDatabaseLib::Stream::IMemStream>
{
  auto* client_data = Noggit::Application::NoggitApplication::instance()->clientData();

  auto try_load = [&](std::string const& path) -> std::shared_ptr<BlizzardDatabaseLib::Stream::IMemStream>
  {
    if (!client_data)
      return nullptr;

    if (client_data->version() == BlizzardArchive::ClientVersion::WOTLK)
    {
      try
      {
        BlizzardArchive::ClientFile file(path, client_data);
        if (!file.isEof())
          return std::make_shared<BlizzardDatabaseLib::Stream::IMemStream>(file.getBuffer(), file.getSize());
      }
      catch (...)
      {
      }
      return nullptr;
    }

    BlizzardArchive::Listfile::FileKey file_key(path);
    file_key.deduceOtherComponent(client_data->listfile());

    std::vector<char> buffer;
    if (!client_data->readFile(file_key, buffer) || buffer.size() < 4)
      return nullptr;

    return std::make_shared<BlizzardDatabaseLib::Stream::IMemStream>(buffer.data(), buffer.size());
  };

  std::string base = file_path;
  if (base.ends_with(".dbc") || base.ends_with(".db2"))
    base = base.substr(0, base.find_last_of('.'));

  std::vector<std::string> candidates;
  if (client_data && client_data->version() != BlizzardArchive::ClientVersion::WOTLK)
  {
    candidates = {base + ".db2", base, base + ".dbc"};
  }
  else
  {
    candidates = {base + ".dbc"};
    if (file_path != base + ".dbc")
      candidates.insert(candidates.begin(), file_path);
  }

  for (auto const& candidate : candidates)
  {
    if (auto stream = try_load(candidate))
      return stream;
  }

  throw BlizzardArchive::Exceptions::FileReadFailedError(
    "File '" + file_path + "' does not exist or some other error occured.");
};


#endif //NOGGIT_UTILS_HPP
