#pragma once

#include <stddef.h>
#include <stdint.h>

namespace v2m {

constexpr size_t kNoteCount = 16;

enum class Timbre : uint8_t { Keys, Pluck, Flute, Bell };
const char* timbreName(Timbre timbre);

struct Note {
  uint8_t midi;
  float velocity;
  Timbre timbre = Timbre::Keys;
};

struct Song {
  Note notes[kNoteCount];
  float brightness;
  float saturation;
  float edge;
  uint16_t bpm;
  uint8_t root;  // Pitch class: C = 0, C# = 1, ... B = 11.
  bool minor;
};

// Tightly packed, row-major R, G, B bytes; no row padding or BGR conversion.
// Features are normalized to 0..1. Invalid input leaves out unchanged.
// These are deliberately simple composition rules, not semantic recognition.
bool composeRgb888(const uint8_t* rgb, size_t length, uint16_t width,
                   uint16_t height, Song& out);

class Synth {
 public:
  static constexpr uint32_t kSampleRate = 22050;

  // Two bars of 4/4: sixteen eighth notes at 60..96 BPM, then 100 ms silence.
  // Copies the song; input may be released as soon as start returns.
  // Preserves the requested drums setting, resets all voices and the noise
  // seed. Replaying the same song and switch schedule gives the same PCM.
  void start(const Song& song);
  // Off by default. Enabling joins at the next unrendered eighth-note
  // boundary (including frame 0); disabling fades existing hits over 8 ms.
  // Neither operation changes melody position. Call from the rendering task.
  void setDrumsEnabled(bool enabled);
  bool drumsEnabled() const { return drumsEnabled_; }
  // Writes up to maxFrames stereo pairs (L = R). Returns frames, not samples.
  // A short final block is not padded. No allocation occurs during playback.
  size_t render(int16_t* interleavedStereo, size_t maxFrames);
  bool done() const { return frame_ >= totalFrames_; }
  uint32_t renderedFrames() const { return frame_; }
  uint32_t totalFrames() const { return totalFrames_; }
  uint32_t musicalFrames() const { return framesPerNote_ * kNoteCount; }
  uint32_t framesPerNote() const { return framesPerNote_; }

 private:
  void beginNote(size_t index);
  void triggerDrums(size_t step);
  float renderDrums();

  Song song_{};
  uint32_t frame_ = 0;
  uint32_t totalFrames_ = 0;
  uint32_t framesPerNote_ = 0;
  float sine_ = 0.0f;
  float cosine_ = 1.0f;
  float rotationSine_ = 0.0f;
  float rotationCosine_ = 1.0f;
  float partialSine_ = 0.0f;
  float partialCosine_ = 1.0f;
  float partialRotationSine_ = 0.0f;
  float partialRotationCosine_ = 1.0f;
  float toneEnvelope_ = 1.0f;
  float toneDecay_ = 1.0f;
  float partialEnvelope_ = 1.0f;
  float partialDecay_ = 1.0f;
  float attackFrames_ = 1.0f;

  bool drumsEnabled_ = false;
  bool drumTriggersActive_ = false;
  float drumGain_ = 0.0f;
  float kickEnvelope_ = 0.0f;
  float snareEnvelope_ = 0.0f;
  float hatEnvelope_ = 0.0f;
  float kickPhase_ = 0.0f;
  float kickFrequency_ = 55.0f;
  float snarePhase_ = 0.0f;
  uint32_t kickAge_ = 0;
  uint32_t snareAge_ = 0;
  uint32_t hatAge_ = 0;
  uint32_t noiseState_ = 0x6d2b79f5U;
  float previousNoise_ = 0.0f;
  float noiseLowpass_ = 0.0f;
  float kickDecay_ = 0.0f;
  float snareDecay_ = 0.0f;
  float hatDecay_ = 0.0f;
  float kickPitchSlew_ = 0.0f;
};

}  // namespace v2m
