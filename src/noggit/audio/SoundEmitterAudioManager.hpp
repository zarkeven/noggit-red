// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <external/glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class QMediaPlayer;
class QTemporaryFile;
class World;

namespace Noggit::Audio
{
  class SoundEmitterAudioManager
  {
  public:
    SoundEmitterAudioManager();
    ~SoundEmitterAudioManager();

    SoundEmitterAudioManager(SoundEmitterAudioManager const&) = delete;
    SoundEmitterAudioManager& operator=(SoundEmitterAudioManager const&) = delete;

    void update(World* world, glm::vec3 const& camera_pos, bool enabled);

    void stopAll();

  private:
    struct Voice
    {
      std::unique_ptr<QMediaPlayer> player;
      std::unique_ptr<QTemporaryFile> temp_file;
      std::uint64_t emitter_key = 0;
      std::uint32_t sound_entry_id = 0;
      glm::vec3 position{};
      float base_volume = 1.f;
      float min_distance = 0.f;
      float cutoff_distance = 0.f;
      bool looping = false;
    };

    struct Candidate
    {
      std::uint64_t key;
      glm::vec3 position;
      std::uint32_t sound_entry_id;
      float distance;
      float min_distance;
      float cutoff_distance;
      float base_volume;
      bool looping;
    };

    static float distanceAttenuation(float distance, float min_distance, float cutoff_distance);
    static bool shouldLoopSoundType(int sound_type);
    static std::uint64_t makeEmitterKey(int tile_x, int tile_z, int chunk_x, int chunk_z, std::size_t emitter_index);

    void assignVoice(Voice& voice, Candidate const& candidate);
    void releaseVoice(Voice& voice);

    static constexpr std::size_t kMaxVoices = 12;

    std::vector<Voice> _voices;
  };
}
