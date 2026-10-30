#include "sonification.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> image(uint16_t width, uint16_t height, uint8_t fill = 0) {
  return std::vector<uint8_t>(static_cast<size_t>(width) * height * 3, fill);
}

bool inScale(const v2m::Song& song, uint8_t midi) {
  const int major[] = {0, 2, 4, 7, 9};
  const int minor[] = {0, 3, 5, 7, 10};
  const int* scale = song.minor ? minor : major;
  const int interval = (midi + 12 - song.root) % 12;
  return std::find(scale, scale + 5, interval) != scale + 5;
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
    assert(static_cast<uint8_t>(note.timbre) <= static_cast<uint8_t>(v2m::Timbre::Bell));
  }
}

std::vector<int16_t> stream(const v2m::Song& song, size_t blockSize, bool drums = false) {
  v2m::Synth synth;
  assert(synth.done());
  assert(!synth.drumsEnabled());
  synth.setDrumsEnabled(drums);
  synth.start(song);
  assert(!synth.done());
  assert(synth.drumsEnabled() == drums);
  assert(synth.renderedFrames() == 0);
  assert(synth.musicalFrames() == synth.framesPerNote() * v2m::kNoteCount);
  assert(synth.totalFrames() == synth.musicalFrames() + v2m::Synth::kSampleRate / 10);
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
    assert(synth.renderedFrames() == result.size() / 2);
    assert(result.size() <= v2m::Synth::kSampleRate * 17U);
  }
  assert(synth.render(block.data(), blockSize) == 0);
  return result;
}

struct Toggle { uint32_t frame; bool enabled; };

std::vector<int16_t> switchedStream(const v2m::Song& song, size_t blockSize,
                                   const std::vector<Toggle>& toggles) {
  v2m::Synth synth;
  synth.start(song);
  const uint32_t total = synth.totalFrames();
  std::vector<int16_t> output(total * 2, 0);
  size_t nextToggle = 0;
  while (!synth.done()) {
    const uint32_t frame = synth.renderedFrames();
    if (nextToggle < toggles.size() && frame == toggles[nextToggle].frame) {
      synth.setDrumsEnabled(toggles[nextToggle++].enabled);
      assert(synth.renderedFrames() == frame && synth.totalFrames() == total);
    }
    size_t count = std::min(blockSize, static_cast<size_t>(total - frame));
    if (nextToggle < toggles.size()) count = std::min(count, static_cast<size_t>(toggles[nextToggle].frame - frame));
    assert(count > 0);
    assert(synth.render(output.data() + frame * 2, count) == count);
  }
  return output;
}

double energy(const std::vector<int16_t>& pcm, size_t startFrame, size_t frames) {
  double total = 0.0;
  for (size_t frame = startFrame; frame < startFrame + frames; ++frame) {
    const double sample = pcm[frame * 2];
    total += sample * sample;
  }
  return total / frames;
}

void assertQuietTail(const std::vector<int16_t>& pcm, size_t musicFrames) {
  assert(std::all_of(pcm.begin() + musicFrames * 2, pcm.end(),
                    [](int16_t sample) { return sample == 0; }));
}

void testColors() {
  using v2m::Timbre;
  struct ColorCase { uint8_t red, green, blue; Timbre timbre; };
  const ColorCase cases[] = {
    {255, 0, 0, Timbre::Pluck}, {255, 128, 0, Timbre::Pluck},
    {255, 255, 0, Timbre::Keys}, {0, 255, 0, Timbre::Keys},
    {0, 255, 255, Timbre::Flute}, {0, 0, 255, Timbre::Flute},
    {180, 0, 255, Timbre::Bell}, {255, 0, 255, Timbre::Bell},
    {0, 0, 0, Timbre::Keys}, {220, 220, 200, Timbre::Keys},
    {4, 0, 0, Timbre::Keys},
    // Neighboring 8-bit RGB values on both sides of each hue-sector edge.
    {255, 191, 0, Timbre::Pluck}, {255, 192, 0, Timbre::Keys},
    {0, 255, 191, Timbre::Keys}, {0, 255, 192, Timbre::Flute},
    {127, 0, 255, Timbre::Flute}, {128, 0, 255, Timbre::Bell},
    {255, 0, 128, Timbre::Bell}, {255, 0, 127, Timbre::Pluck},
  };
  for (const auto& test : cases) {
    const uint8_t pixel[] = {test.red, test.green, test.blue};
    v2m::Song song{};
    assert(v2m::composeRgb888(pixel, 3, 1, 1, song));
    for (const auto& note : song.notes) assert(note.timbre == test.timbre);
  }
  // Each stripe chooses independently, not from the whole photograph's color.
  auto striped = image(16, 8);
  const ColorCase palette[] = {cases[0], cases[3], cases[5], cases[7]};
  for (size_t y = 0; y < 8; ++y) {
    for (size_t x = 0; x < 16; ++x) {
      const auto& color = palette[x % 4];
      striped[(y * 16 + x) * 3] = color.red;
      striped[(y * 16 + x) * 3 + 1] = color.green;
      striped[(y * 16 + x) * 3 + 2] = color.blue;
    }
  }
  v2m::Song song{};
  assert(v2m::composeRgb888(striped.data(), striped.size(), 16, 8, song));
  for (size_t i = 0; i < v2m::kNoteCount; ++i) assert(song.notes[i].timbre == palette[i % 4].timbre);

  // Red + cyan averages to gray in RGB, but two saturated colors must retain
  // their separate votes. A deterministic tie selects Pluck here, not Keys.
  const uint8_t complementary[] = {255, 0, 0, 0, 255, 255};
  assert(v2m::composeRgb888(complementary, sizeof(complementary), 1, 2, song));
  for (const auto& note : song.notes) assert(note.timbre == Timbre::Pluck);
  assert(song.root == 0);

  // A tiny colored patch in a mostly neutral stripe does not dominate it.
  auto neutral = image(1, 16, 255);
  neutral[1] = neutral[2] = 0;
  assert(v2m::composeRgb888(neutral.data(), neutral.size(), 1, 16, song));
  for (const auto& note : song.notes) assert(note.timbre == Timbre::Keys);
  assert(std::strcmp(v2m::timbreName(Timbre::Keys), "Keys") == 0);
  assert(std::strcmp(v2m::timbreName(Timbre::Pluck), "Pluck") == 0);
  assert(std::strcmp(v2m::timbreName(Timbre::Flute), "Flute") == 0);
  assert(std::strcmp(v2m::timbreName(Timbre::Bell), "Bell") == 0);
}

}  // namespace

int main() {
  testColors();
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
  assert(audible && peak <= 5734);  // 0.175 of signed 16-bit full scale.
  for (size_t note = 0; note < v2m::kNoteCount; ++note) {
    assert(pcm[note * framesPerNote * 2] == 0);
    assert(pcm[((note + 1) * framesPerNote - 1) * 2] == 0);
  }
  assertQuietTail(pcm, framesPerNote * v2m::kNoteCount);

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
  for (int16_t sample : loud) assert(std::abs(static_cast<int>(sample)) <= 5734);
  assert(loud.size() < silence.size());

  // Starting again during playback must discard the previous oscillator state.
  v2m::Synth repeat;
  int16_t first[512], restarted[512];
  repeat.start(topSong);
  assert(repeat.render(first, 256) == 256);
  repeat.start(topSong);
  assert(repeat.render(restarted, 256) == 256);
  assert(std::equal(first, first + 512, restarted));

  // Same pitches and velocities with each timbre must produce distinct sound.
  std::vector<std::vector<int16_t>> timbres;
  v2m::Song testSong = topSong;
  testSong.bpm = 96;
  for (uint8_t timbre = 0; timbre < 4; ++timbre) {
    for (auto& note : testSong.notes) {
      note.midi = 72;
      note.velocity = 1.0f;
      note.timbre = static_cast<v2m::Timbre>(timbre);
    }
    const auto sound = stream(testSong, 257);
    assert(sound == stream(testSong, 4096));
    assert(energy(sound, 0, 2048) > 1.0);
    for (const auto& previous : timbres) assert(sound != previous);
    for (int16_t sample : sound) {
      peak = std::max(peak, std::abs(static_cast<int>(sample)));
      assert(std::abs(static_cast<int>(sample)) <= 5734);
    }
    const auto mixed = stream(testSong, 1024, true);
    for (int16_t sample : mixed) {
      peak = std::max(peak, std::abs(static_cast<int>(sample)));
      assert(std::abs(static_cast<int>(sample)) <= 5734);
    }
    timbres.push_back(sound);
  }

  const auto drums = stream(testSong, 257, true);
  assert(drums == stream(testSong, 4096, true));
  const auto clean = stream(testSong, 301);
  assert(drums != clean && drums.size() == clean.size());
  for (int16_t sample : drums) assert(std::abs(static_cast<int>(sample)) <= 5734);
  v2m::Synth clock;
  clock.start(testSong);
  const uint32_t stepFrames = clock.framesPerNote();
  const uint32_t musicFrames = clock.musicalFrames();
  assertQuietTail(drums, musicFrames);

  // Mid-step enable cannot alter any sample before the next subdivision;
  // disabling leaves an 8 ms fade, then the unchanged melody is exact again.
  const uint32_t enableAt = stepFrames / 2;
  const uint32_t disableAt = stepFrames * 2 + 50;
  const uint32_t fadeFrames = (v2m::Synth::kSampleRate * 8U + 999U) / 1000U;
  const std::vector<Toggle> toggles{{enableAt, true}, {disableAt, false}};
  const auto switched = switchedStream(testSong, 333, toggles);
  assert(switched == switchedStream(testSong, 701, toggles));
  assert(switched == switchedStream(testSong, 1, toggles));
  assert(std::equal(clean.begin(), clean.begin() + stepFrames * 2, switched.begin()));
  assert(!std::equal(clean.begin() + stepFrames * 2, clean.begin() + disableAt * 2,
                     switched.begin() + stepFrames * 2));
  assert(!std::equal(clean.begin() + disableAt * 2, clean.begin() + (disableAt + 20) * 2,
                     switched.begin() + disableAt * 2));  // Existing hit is not hard-cut.
  assert(std::equal(clean.begin() + (disableAt + fadeFrames + 1) * 2, clean.end(),
                    switched.begin() + (disableAt + fadeFrames + 1) * 2));
  assertQuietTail(switched, musicFrames);

  // A toggle during the silent tail must never start a new hit or extend it.
  const auto late = switchedStream(testSong, 256, {{musicFrames + 10, true}});
  assert(late == clean);
  const auto justAtBoundary = switchedStream(testSong, 509, {{stepFrames, true}});
  assert(std::equal(clean.begin(), clean.begin() + stepFrames * 2, justAtBoundary.begin()));
  assert(justAtBoundary != clean);

  // Isolate percussion. Every off-beat has a hat; alternating downbeats carry
  // the much stronger kick/snare. No extra bass or arpeggio voice is present.
  v2m::Song percussionOnly = testSong;
  for (auto& note : percussionOnly.notes) note.velocity = 0.0f;
  const auto hits = stream(percussionOnly, 127, true);
  for (size_t step = 0; step < v2m::kNoteCount; step += 2) {
    const double downbeat = energy(hits, step * stepFrames + 30, 1500);
    const double offbeat = energy(hits, (step + 1) * stepFrames + 30, 1500);
    assert(offbeat > 1.0 && downbeat > offbeat * 2.0);
  }
  assertQuietTail(hits, musicFrames);

  // start preserves the requested setting while resetting noise and all drum
  // phases. Replay must be deterministic on the same Synth instance too.
  repeat.setDrumsEnabled(true);
  repeat.start(testSong);
  assert(repeat.drumsEnabled());
  assert(repeat.render(first, 256) == 256);
  repeat.start(testSong);
  assert(repeat.render(restarted, 256) == 256);
  assert(std::equal(first, first + 512, restarted));
  repeat.setDrumsEnabled(false);
  repeat.start(testSong);
  assert(!repeat.drumsEnabled());
  assert(repeat.render(restarted, 256) == 256);
  assert(std::equal(restarted, restarted + 512, clean.begin()));

  // Invalid timbre is sanitized along with malformed note velocity and pitch.
  auto invalidTimbre = testSong;
  for (auto& note : invalidTimbre.notes) note.timbre = static_cast<v2m::Timbre>(255);
  auto keys = invalidTimbre;
  for (auto& note : keys.notes) note.timbre = v2m::Timbre::Keys;
  assert(stream(invalidTimbre, 512) == stream(keys, 512));

  std::printf("PASS: pentatonic/color rules, four timbres, deterministic blocks/replay, drum timing/fade, silent tail, amplitude, input bounds; sample duration %.3f s, tested peak %d\n",
              expectedFrames / static_cast<double>(v2m::Synth::kSampleRate), peak);
}
