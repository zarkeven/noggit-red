// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/audio/SoundEmitterAudioManager.hpp>
#include <noggit/audio/SoundFileLoader.hpp>
#include <noggit/DBC.h>
#include <noggit/MapChunk.h>
#include <noggit/MapHeaders.h>
#include <noggit/MapTile.h>
#include <noggit/World.h>
#include <noggit/map_index.hpp>

#include <QMediaPlayer>
#include <QUrl>
#include <qtemporaryfile>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Noggit::Audio
{
  namespace
  {
    struct ResolvedSoundEntry
    {
      std::uint32_t id;
      std::string directory;
      std::string filename;
      float volume;
      float min_distance;
      float cutoff_distance;
      int sound_type;
    };

    std::optional<ResolvedSoundEntry> resolveSoundEntry(std::uint32_t mcse_sound_id)
    {
      std::optional<std::uint32_t> const sound_entry_id = resolveSoundEntryId(mcse_sound_id);
      if (!sound_entry_id)
      {
        return std::nullopt;
      }

      try
      {
        DBCFile::Record const record = gSoundEntriesDB.getByID(*sound_entry_id);

        ResolvedSoundEntry resolved{};
        resolved.id = *sound_entry_id;
        resolved.directory = record.getString(SoundEntriesDB::FilePath);
        resolved.volume = record.getFloat(SoundEntriesDB::Volume);
        resolved.min_distance = record.getFloat(SoundEntriesDB::minDistance);
        resolved.cutoff_distance = record.getFloat(SoundEntriesDB::distanceCutoff);
        resolved.sound_type = static_cast<int>(record.getUInt(SoundEntriesDB::SoundType));

        for (int i = 0; i < 10; ++i)
        {
          std::string const filename = record.getString(SoundEntriesDB::Filenames + i);
          if (!filename.empty())
          {
            resolved.filename = filename;
            break;
          }
        }

        if (resolved.filename.empty())
        {
          return std::nullopt;
        }

        if (resolved.min_distance <= 0.f || resolved.cutoff_distance <= 0.f)
        {
          try
          {
            DBCFile::Record const advanced = gSoundEntriesAdvancedDB.getByID(mcse_sound_id);
            if (resolved.min_distance <= 0.f)
            {
              resolved.min_distance = advanced.getFloat(SoundEntriesAdvancedDB::innerRadiusOfInfluence);
            }
            if (resolved.cutoff_distance <= 0.f)
            {
              resolved.cutoff_distance = advanced.getFloat(SoundEntriesAdvancedDB::outerRadiusOfInfluence);
            }
          }
          catch (SoundEntriesAdvancedDB::NotFound)
          {
          }
        }

        if (resolved.cutoff_distance <= 0.f)
        {
          resolved.cutoff_distance = 50.f;
        }
        if (resolved.min_distance < 0.f)
        {
          resolved.min_distance = 0.f;
        }

        return resolved;
      }
      catch (SoundEntriesDB::NotFound)
      {
        return std::nullopt;
      }
    }
  }

  SoundEmitterAudioManager::SoundEmitterAudioManager()
  {
    _voices.resize(kMaxVoices);
    for (auto& voice : _voices)
    {
      voice.player = std::make_unique<QMediaPlayer>();
    }
  }

  SoundEmitterAudioManager::~SoundEmitterAudioManager()
  {
    stopAll();
  }

  float SoundEmitterAudioManager::distanceAttenuation(float distance, float min_distance, float cutoff_distance)
  {
    if (cutoff_distance <= min_distance)
    {
      return distance <= cutoff_distance ? 1.f : 0.f;
    }

    if (distance <= min_distance)
    {
      return 1.f;
    }
    if (distance >= cutoff_distance)
    {
      return 0.f;
    }

    float const t = (distance - min_distance) / (cutoff_distance - min_distance);
    return 1.f - t;
  }

  bool SoundEmitterAudioManager::shouldLoopSoundType(int sound_type)
  {
    switch (sound_type)
    {
      case 50: // ZONE_AMBIENCE
      case 52: // SOUND_EMITTERS
      case 19: // TERRAIN_EMITER
      case 24: // WATERVOLUME_SOUNDS
      case 35: // CREATURE_LOOPS
        return true;
      default:
        return false;
    }
  }

  std::uint64_t SoundEmitterAudioManager::makeEmitterKey(int tile_x, int tile_z, int chunk_x, int chunk_z, std::size_t emitter_index)
  {
    return (static_cast<std::uint64_t>(tile_x & 0xFF) << 56)
         | (static_cast<std::uint64_t>(tile_z & 0xFF) << 48)
         | (static_cast<std::uint64_t>(chunk_x & 0xF) << 44)
         | (static_cast<std::uint64_t>(chunk_z & 0xF) << 40)
         | static_cast<std::uint64_t>(emitter_index);
  }

  void SoundEmitterAudioManager::releaseVoice(Voice& voice)
  {
    if (voice.player)
    {
      voice.player->stop();
      voice.player->setMedia(QMediaContent());
    }
    voice.temp_file.reset();
    voice.emitter_key = 0;
    voice.sound_entry_id = 0;
  }

  void SoundEmitterAudioManager::assignVoice(Voice& voice, Candidate const& candidate)
  {
    auto const resolved = resolveSoundEntry(candidate.sound_entry_id);
    if (!resolved)
    {
      releaseVoice(voice);
      return;
    }

    std::stringstream filepath;
    filepath << resolved->directory << "\\" << resolved->filename;

    auto temp_opt = extractSoundToTempFile(filepath.str());
    if (!temp_opt)
    {
      releaseVoice(voice);
      return;
    }

    voice.temp_file.reset(*temp_opt);
    voice.emitter_key = candidate.key;
    voice.sound_entry_id = resolved->id;
    voice.position = candidate.position;
    voice.base_volume = resolved->volume;
    voice.min_distance = candidate.min_distance;
    voice.cutoff_distance = candidate.cutoff_distance;
    voice.looping = candidate.looping;

    voice.player->setMedia(QUrl::fromLocalFile(voice.temp_file->fileName()));

    float const attenuation = distanceAttenuation(candidate.distance, candidate.min_distance, candidate.cutoff_distance);
    int const volume = static_cast<int>(std::clamp(resolved->volume * attenuation * 100.f, 0.f, 100.f));
    voice.player->setVolume(volume);

    if (voice.looping)
    {
      QObject::disconnect(voice.player.get(), &QMediaPlayer::mediaStatusChanged, nullptr, nullptr);
      QObject::connect(voice.player.get(), &QMediaPlayer::mediaStatusChanged, voice.player.get(),
        [player = voice.player.get()](QMediaPlayer::MediaStatus status)
        {
          if (status == QMediaPlayer::EndOfMedia)
          {
            player->setPosition(0);
            player->play();
          }
        });
    }

    voice.player->play();
  }

  void SoundEmitterAudioManager::stopAll()
  {
    for (auto& voice : _voices)
    {
      releaseVoice(voice);
    }
  }

  void SoundEmitterAudioManager::update(World* world, glm::vec3 const& camera_pos, bool enabled)
  {
    if (!enabled || !world)
    {
      stopAll();
      return;
    }

    std::vector<Candidate> candidates;
    candidates.reserve(256);

    for (MapTile* tile : world->mapIndex.loaded_tiles())
    {
      if (!tile)
      {
        continue;
      }

      for (unsigned z = 0; z < 16u; ++z)
      {
        for (unsigned x = 0; x < 16u; ++x)
        {
          MapChunk* chunk = tile->getChunk(x, z);
          if (!chunk || chunk->sound_emitters.empty())
          {
            continue;
          }

          for (std::size_t i = 0; i < chunk->sound_emitters.size(); ++i)
          {
            ENTRY_MCSE const& emitter = chunk->sound_emitters[i];
            if (emitter.soundId == 0)
            {
              continue;
            }

            glm::vec3 const position(emitter.pos[0], emitter.pos[1], emitter.pos[2]);
            float const distance = glm::distance(camera_pos, position);

            auto const resolved = resolveSoundEntry(emitter.soundId);
            if (!resolved)
            {
              continue;
            }

            if (distance > resolved->cutoff_distance)
            {
              continue;
            }

            Candidate candidate{};
            candidate.key = makeEmitterKey(tile->index.x, tile->index.z, static_cast<int>(x), static_cast<int>(z), i);
            candidate.position = position;
            candidate.sound_entry_id = emitter.soundId;
            candidate.distance = distance;
            candidate.min_distance = resolved->min_distance;
            candidate.cutoff_distance = resolved->cutoff_distance;
            candidate.base_volume = resolved->volume;
            candidate.looping = shouldLoopSoundType(resolved->sound_type);
            candidates.push_back(candidate);
          }
        }
      }
    }

    std::sort(candidates.begin(), candidates.end(),
      [](Candidate const& a, Candidate const& b)
      {
        return a.distance < b.distance;
      });

    if (candidates.size() > kMaxVoices)
    {
      candidates.resize(kMaxVoices);
    }

    for (auto& voice : _voices)
    {
      bool still_active = false;
      for (Candidate const& candidate : candidates)
      {
        if (voice.emitter_key == candidate.key)
        {
          still_active = true;
          float const attenuation = distanceAttenuation(candidate.distance, voice.min_distance, voice.cutoff_distance);
          int const volume = static_cast<int>(std::clamp(voice.base_volume * attenuation * 100.f, 0.f, 100.f));
          voice.player->setVolume(volume);
          break;
        }
      }

      if (!still_active)
      {
        releaseVoice(voice);
      }
    }

    for (Candidate const& candidate : candidates)
    {
      bool already_playing = false;
      for (Voice const& voice : _voices)
      {
        if (voice.emitter_key == candidate.key)
        {
          already_playing = true;
          break;
        }
      }

      if (already_playing)
      {
        continue;
      }

      for (auto& voice : _voices)
      {
        if (voice.emitter_key == 0)
        {
          assignVoice(voice, candidate);
          break;
        }
      }
    }
  }
}
