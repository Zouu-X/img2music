#include "sonification.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> image(uint16_t width, uint16_t height, uint8_t fill = 0) {
  return std::vector<uint8_t>(static_cast<size_t>(width) * height * 3, fill);
}

bool inScale(const v2m::Song& song, uint8_t midi) {
  const int major[] = {0, 2, 4, 5, 7, 9, 11};
  const int minor[] = {0, 2, 3, 5, 7, 8, 10};
  const int* scale = song.minor ? minor : major;
  const int interval = (midi + 12 - song.root) % 12;
  return std::find(scale, scale + 7, interval) != scale + 7;
}

void assertSong(const v2m::Song& song) {
  for (float feature : {song.brightness, song.saturation, song.edge}) {
    assert(std::isfinite(feature) && feature >= 0.0f && feature <= 1.0f);
  }
  assert(song.bpm >= 60 && song.bpm <= 96);
  assert(song.root < 12);
  for (const auto& note : song.notes) {
    assert(note.midi >= 48 && note.midi <= 84 && inScale(song, note.midi));
    assert(std::isfinite(note.velocity) && note.velocity >= 0.0f && note.velocity <= 1.0f);
  }
}

std::vector<int16_t> stream(const v2m::Song& song, size_t blockSize) {
  v2m::Synth synth;
  assert(synth.done());
  synth.start(song);
  assert(!synth.done());
  std::vector<int16_t> block(blockSize * 2 + 2, -12345);
  std::vector<int16_t> result;
  assert(synth.render(nullptr, blockSize) == 0);
  assert(synth.render(block.data(), 0) == 0);
  while (!synth.done()) {
    std::fill(block.begin(), block.end(), -12345);
    const size_t frames = synth.render(block.data(), blockSize);
    assert(frames > 0 && frames <= blockSize);
    assert(block[frames * 2] == -12345 && block[frames * 2 + 1] == -12345);
    result.insert(result.end(), block.begin(), block.begin() + frames * 2);
    assert(result.size() <= v2m::Synth::kSampleRate * 17U);
  }
  assert(synth.render(block.data(), blockSize) == 0);
  return result;
}

}  // namespace

int main() {
  auto black = image(32, 24);
  v2m::Song song{};
  song.bpm = 777;
  assert(!v2m::composeRgb888(nullptr, black.size(), 32, 24, song));
  assert(!v2m::composeRgb888(black.data(), black.size() - 1, 32, 24, song));
  assert(!v2m::composeRgb888(black.data(), black.size(), 0, 24, song));
  assert(!v2m::composeRgb888(black.data(), black.size(), 32, 0, song));
  assert(!v2m::composeRgb888(black.data(), black.size(), 65535, 65535, song));
  assert(song.bpm == 777);
  assert(v2m::composeRgb888(black.data(), black.size(), 32, 24, song));
  assertSong(song);
  assert(song.brightness == 0.0f && song.saturation == 0.0f && song.edge == 0.0f);
  assert(song.minor && song.root == 0);

  auto top = image(32, 24);
  auto bottom = image(32, 24);
  std::fill(top.begin(), top.begin() + 32 * 6 * 3, 255);
  std::fill(bottom.end() - 32 * 6 * 3, bottom.end(), 255);
  v2m::Song topSong{}, bottomSong{};
  assert(v2m::composeRgb888(top.data(), top.size(), 32, 24, topSong));
  assert(v2m::composeRgb888(bottom.data(), bottom.size(), 32, 24, bottomSong));
  assertSong(topSong);
  assertSong(bottomSong);
  assert(std::fabs(topSong.brightness - bottomSong.brightness) < 0.00001f);
  for (size_t i = 0; i < v2m::kNoteCount; ++i) assert(topSong.notes[i].midi > bottomSong.notes[i].midi);

  for (uint16_t width : {1, 7, 16, 19, 160}) {
    auto pixels = image(width, 31);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>((i * 37 + i / 11) % 256);
    assert(v2m::composeRgb888(pixels.data(), pixels.size(), width, 31, song));
    assertSong(song);
  }
  auto white = image(1, 1, 255);
  assert(v2m::composeRgb888(white.data(), white.size(), 1, 1, song));
  assertSong(song);
  assert(std::fabs(song.brightness - 1.0f) < 0.00001f && !song.minor);
  const uint8_t red[] = {255, 0, 0};
  const uint8_t green[] = {0, 255, 0};
  const uint8_t blue[] = {0, 0, 255};
  for (const auto& test : {std::make_pair(red, 0), std::make_pair(green, 4), std::make_pair(blue, 8)}) {
    assert(v2m::composeRgb888(test.first, 3, 1, 1, song));
    assert(song.root == test.second);
    assertSong(song);
  }

  const auto pcm = stream(topSong, 257);
  assert(pcm == stream(topSong, 4096));
  const size_t framesPerNote = (v2m::Synth::kSampleRate * 30 + topSong.bpm / 2) / topSong.bpm;
  const size_t expectedFrames = framesPerNote * v2m::kNoteCount + v2m::Synth::kSampleRate / 10;
  assert(pcm.size() == expectedFrames * 2);
  bool audible = false;
  int peak = 0;
  for (size_t i = 0; i < pcm.size(); i += 2) {
    assert(pcm[i] == pcm[i + 1]);
    peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
    audible = audible || pcm[i] != 0;
  }
  assert(audible && peak <= 5898);  // 0.18 of signed 16-bit full scale.
  for (size_t note = 0; note < v2m::kNoteCount; ++note) {
    assert(pcm[note * framesPerNote * 2] == 0);
    assert(pcm[((note + 1) * framesPerNote - 1) * 2] == 0);
  }
  assert(std::all_of(pcm.begin() + framesPerNote * v2m::kNoteCount * 2, pcm.end(),
                     [](int16_t sample) { return sample == 0; }));

  v2m::Song hostile = topSong;
  hostile.bpm = 0;
  for (auto& note : hostile.notes) {
    note.midi = 255;
    note.velocity = std::numeric_limits<float>::quiet_NaN();
  }
  const auto silence = stream(hostile, 512);
  assert(std::all_of(silence.begin(), silence.end(), [](int16_t sample) { return sample == 0; }));
  assert(silence.size() == (v2m::Synth::kSampleRate * 8U + v2m::Synth::kSampleRate / 10U) * 2U);
  hostile.bpm = 65535;
  for (auto& note : hostile.notes) note.velocity = 10.0f;
  const auto loud = stream(hostile, 511);
  for (int16_t sample : loud) assert(std::abs(static_cast<int>(sample)) <= 5898);
  assert(loud.size() < silence.size());

  // Starting again during playback must discard the previous oscillator state.
  v2m::Synth repeat;
  int16_t first[512], restarted[512];
  repeat.start(topSong);
  assert(repeat.render(first, 256) == 256);
  repeat.start(topSong);
  assert(repeat.render(restarted, 256) == 256);
  assert(std::equal(first, first + 512, restarted));
  std::printf("PASS: input validation, feature bounds, scale/pitch mapping, streaming duration, stereo, envelope, amplitude; sample duration %.3f s, peak %d\n",
              expectedFrames / static_cast<double>(v2m::Synth::kSampleRate), peak);
}
