#include "achievement_sound.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(achievement_sound, true, "UI", "Play a sound when an achievement unlocks");

REXCVAR_DEFINE_INT32(achievement_sound_volume, 80, "UI", "Achievement sound volume (0-100)");

REXCVAR_DEFINE_STRING(achievement_sound_file, "", "UI",
                      "WAV file played when an achievement unlocks (empty = built-in chime)");

namespace overlay {

namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;

SDL_AudioStream* g_stream = nullptr;
bool g_audio_initialized = false;
bool g_failed = false;
std::vector<float> g_samples;  // interleaved stereo at kRate, unscaled
std::string g_samples_source;

// Two bell strikes a fifth apart over a soft low note.
std::vector<float> SynthesizeChime() {
  struct Note {
    float start, frequency, gain, pan;
  };
  static constexpr Note kNotes[] = {
      {0.000f, 659.26f, 0.32f, 0.0f},     // E5, body
      {0.000f, 1318.51f, 0.55f, -0.25f},  // E6
      {0.095f, 1975.53f, 0.50f, 0.25f},   // B6
  };
  // Bell partials: frequency ratio, amplitude, decay per second.
  static constexpr float kPartials[][3] = {
      {1.0f, 1.0f, 3.0f}, {2.0f, 0.32f, 5.5f}, {2.76f, 0.16f, 8.0f}, {5.4f, 0.05f, 14.0f}};
  const float duration = 1.9f;
  const size_t frames = size_t(duration * kRate);
  std::vector<float> out(frames * kChannels, 0.0f);
  for (const Note& note : kNotes) {
    const size_t first = size_t(note.start * kRate);
    const float left = std::sqrt(0.5f * (1.0f - note.pan));
    const float right = std::sqrt(0.5f * (1.0f + note.pan));
    for (size_t i = first; i < frames; ++i) {
      const float t = float(i - first) / kRate;
      const float attack = std::min(t / 0.004f, 1.0f);
      float value = 0.0f;
      for (const auto& partial : kPartials) {
        value += partial[1] * std::exp(-partial[2] * t) *
                 std::sin(2.0f * std::numbers::pi_v<float> * note.frequency * partial[0] * t);
      }
      value *= note.gain * attack;
      out[i * 2] += value * left;
      out[i * 2 + 1] += value * right;
    }
  }
  // Fade the tail out and normalize.
  float peak = 0.0f;
  for (size_t i = 0; i < frames; ++i) {
    const float t = float(i) / kRate;
    const float fade = t > duration - 0.3f ? (duration - t) / 0.3f : 1.0f;
    out[i * 2] *= fade;
    out[i * 2 + 1] *= fade;
    peak = std::max({peak, std::abs(out[i * 2]), std::abs(out[i * 2 + 1])});
  }
  if (peak > 0.0f) {
    for (float& v : out) v *= 0.85f / peak;
  }
  return out;
}

std::vector<float> LoadWav(const std::string& path) {
  SDL_AudioSpec spec;
  Uint8* data = nullptr;
  Uint32 length = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length)) {
    REXLOG_WARN("Achievement sound: cannot load {}: {}", path, SDL_GetError());
    return {};
  }
  const SDL_AudioSpec target{SDL_AUDIO_F32, kChannels, kRate};
  Uint8* converted = nullptr;
  int converted_length = 0;
  std::vector<float> out;
  if (SDL_ConvertAudioSamples(&spec, data, int(length), &target, &converted, &converted_length)) {
    out.assign(reinterpret_cast<float*>(converted),
               reinterpret_cast<float*>(converted) + converted_length / sizeof(float));
    SDL_free(converted);
  } else {
    REXLOG_WARN("Achievement sound: cannot convert {}: {}", path, SDL_GetError());
  }
  SDL_free(data);
  return out;
}

bool OpenStream() {
  if (g_stream) return true;
  if (g_failed) return false;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXLOG_WARN("Achievement sound: audio unavailable: {}", SDL_GetError());
    g_failed = true;
    return false;
  }
  g_audio_initialized = true;
  const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kRate};
  g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!g_stream) {
    REXLOG_WARN("Achievement sound: cannot open the audio device: {}", SDL_GetError());
    g_failed = true;
    return false;
  }
  SDL_ResumeAudioStreamDevice(g_stream);
  return true;
}

}  // namespace

void PlayAchievementSound() {
  if (!REXCVAR_GET(achievement_sound)) return;
  const float volume = std::clamp(REXCVAR_GET(achievement_sound_volume), 0, 100) / 100.0f;
  if (volume <= 0.0f || !OpenStream()) return;

  const std::string source = REXCVAR_GET(achievement_sound_file);
  if (g_samples.empty() || source != g_samples_source) {
    g_samples = source.empty() ? std::vector<float>() : LoadWav(source);
    if (g_samples.empty()) g_samples = SynthesizeChime();
    g_samples_source = source;
  }
  std::vector<float> scaled(g_samples);
  // Perceived loudness: square the slider.
  const float gain = volume * volume;
  for (float& v : scaled) v *= gain;
  SDL_ClearAudioStream(g_stream);
  SDL_PutAudioStreamData(g_stream, scaled.data(), int(scaled.size() * sizeof(float)));
}

void ShutdownAchievementSound() {
  if (g_stream) {
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
  }
  if (g_audio_initialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_audio_initialized = false;
  }
}

}  // namespace overlay
