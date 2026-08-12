// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/audio/SoundFileLoader.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/Log.h>

#include <ClientFile.hpp>

#include <qtemporaryfile>

namespace Noggit::Audio
{
  std::optional<QTemporaryFile*> extractSoundToTempFile(std::string const& filepath)
  {
    auto const client_data = Noggit::Application::NoggitApplication::instance()->clientData();
    if (!client_data || !client_data->exists(filepath))
    {
      LogError << "Sound file not found in client: \"" << filepath << "\"" << std::endl;
      return std::nullopt;
    }

    BlizzardArchive::ClientFile file(filepath, client_data);

    auto* temp_file = new QTemporaryFile();
    if (!temp_file->open())
    {
      delete temp_file;
      return std::nullopt;
    }

    temp_file->write(file.getBuffer(), file.getSize());
    temp_file->close();

    std::string const ext = filepath.substr(filepath.find_last_of('.'));
    if (!temp_file->rename(temp_file->fileName() + QString::fromStdString(ext)))
    {
      delete temp_file;
      return std::nullopt;
    }

    return temp_file;
  }
}
