// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/AsyncObject.h>
#include <noggit/Log.h>

#include <chrono>

 AsyncObject::AsyncObject(BlizzardArchive::Listfile::FileKey file_key) : _file_key(std::move(file_key)) {}

[[nodiscard]]
 BlizzardArchive::Listfile::FileKey const& AsyncObject::file_key() const
{
  return _file_key;
}

[[nodiscard]]
 bool AsyncObject::finishedLoading() const
{
  return finished.load();
}

[[nodiscard]]
 bool AsyncObject::loading_failed() const
{
  return _loading_failed.load(std::memory_order_acquire);
}

 void AsyncObject::wait_until_loaded()
{
  if (finished.load())
  {
    return;
  }

  auto const start_wait = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(_mutex);

  _state_changed.wait
  (lock
    , [&]
    {
      return finished.load();
    }
  );

  auto const end_wait = std::chrono::steady_clock::now();
  auto const wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_wait - start_wait).count();
  if (wait_ms >= 1000)
  {
    std::string const id = _file_key.hasFilepath()
      ? _file_key.filepath()
      : std::to_string(_file_key.fileDataID());

    LogError << "AsyncObject: wait_until_loaded '" << id << "' took " << wait_ms << " ms" << std::endl;
  }
}

 void AsyncObject::error_on_loading()
{
  // Make this idempotent so repeated failures (e.g. draw exceptions every frame) don't spam logs
  // or do repeated work.
  bool const already_failed = _loading_failed.exchange(true, std::memory_order_acq_rel);
  if (!already_failed)
  {
    LogError << "File " << (_file_key.hasFilepath() ? _file_key.filepath() : std::to_string(_file_key.fileDataID()))
      << " could not be loaded" << std::endl;
  }

  finished = true;
  _state_changed.notify_all();
}

[[nodiscard]]
 bool AsyncObject::is_required_when_saving() const
{
  return false;
}

[[nodiscard]]
 async_priority AsyncObject::loading_priority() const
{
  return async_priority::medium;
}
