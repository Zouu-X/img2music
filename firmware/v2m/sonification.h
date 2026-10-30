#pragma once

#include <stddef.h>
#include <stdint.h>

namespace v2m {

constexpr size_t kNoteCount = 16;

struct Note {
  uint8_t midi;
  float velocity;
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

  // Sixteen half-beat notes at 60..96 BPM, then 100 ms of silence.
  // Copies the song; input may be released as soon as start returns.
  void start(const Song& song);
  // Writes up to maxFrames stereo pairs (L = R). Returns frames, not samples.
  // A short final block is not padded. No allocation occurs during playback.
  size_t render(int16_t* interleavedStereo, size_t maxFrames);
  bool done() const { return frame_ >= totalFrames_; }
  uint32_t renderedFrames() const { return frame_; }
  uint32_t totalFrames() const { return totalFrames_; }

 private:
  void beginNote(size_t index);

  Song song_{};
  uint32_t frame_ = 0;
  uint32_t totalFrames_ = 0;
  uint32_t framesPerNote_ = 0;
  float sine_ = 0.0f;
  float cosine_ = 1.0f;
  float rotationSine_ = 0.0f;
  float rotationCosine_ = 1.0f;
};

}  // namespace v2m
