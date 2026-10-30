#include "sonification.h"

#include <cmath>
#include <limits>

namespace v2m {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaxAmplitude = 0.175f;
constexpr uint8_t kMajor[] = {0, 2, 4, 5, 7, 9, 11};
constexpr uint8_t kMinor[] = {0, 2, 3, 5, 7, 8, 10};

float clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

float luminance(const uint8_t* pixel) {
  return (0.2126f * pixel[0] + 0.7152f * pixel[1] +
          0.0722f * pixel[2]) / 255.0f;
}

uint8_t nearestScaleNote(float target, uint8_t root, bool minor) {
  const uint8_t* scale = minor ? kMinor : kMajor;
  uint8_t best = 60;
  float bestDistance = 100.0f;
  for (uint8_t midi = 48; midi <= 84; ++midi) {
    const uint8_t interval = (midi + 12 - root) % 12;
    for (size_t i = 0; i < 7; ++i) {
      if (interval != scale[i]) continue;
      const float distance = std::fabs(midi - target);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = midi;
      }
      break;
    }
  }
  return best;
}

}  // namespace

bool composeRgb888(const uint8_t* rgb, size_t length, uint16_t width,
                   uint16_t height, Song& out) {
  if (!rgb || !width || !height ||
      static_cast<size_t>(width) >
          std::numeric_limits<size_t>::max() / height / 3) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  if (length < pixelCount * 3) return false;

  Song result{};
  float red = 0.0f, green = 0.0f, blue = 0.0f;
  float edgeSum = 0.0f;
  size_t edgeCount = 0;
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t index = (y * width + x) * 3;
      const uint8_t* p = rgb + index;
      const float luma = luminance(p);
      result.brightness += luma;
      red += p[0];
      green += p[1];
      blue += p[2];
      const float maximum = std::fmax(p[0], std::fmax(p[1], p[2]));
      const float minimum = std::fmin(p[0], std::fmin(p[1], p[2]));
      result.saturation += maximum > 0.0f ? (maximum - minimum) / maximum : 0.0f;
      if (x) {
        edgeSum += std::fabs(luma - luminance(p - 3));
        ++edgeCount;
      }
      if (y) {
        edgeSum += std::fabs(luma - luminance(p - static_cast<size_t>(width) * 3));
        ++edgeCount;
      }
    }
  }
  result.brightness = clamp(result.brightness / pixelCount, 0.0f, 1.0f);
  result.saturation = clamp(result.saturation / pixelCount, 0.0f, 1.0f);
  result.edge = edgeCount ? clamp(edgeSum / edgeCount, 0.0f, 1.0f) : 0.0f;
  red /= pixelCount;
  green /= pixelCount;
  blue /= pixelCount;

  // Hue of the mean RGB color. Neutral/cancelled colors intentionally use C.
  // This is not the reference project's circular mean of per-pixel hues.
  const float maximum = std::fmax(red, std::fmax(green, blue));
  const float minimum = std::fmin(red, std::fmin(green, blue));
  const float delta = maximum - minimum;
  float hue = 0.0f;
  if (delta > 1.0f) {
    if (maximum == red) hue = (green - blue) / delta;
    else if (maximum == green) hue = 2.0f + (blue - red) / delta;
    else hue = 4.0f + (red - green) / delta;
    if (hue < 0.0f) hue += 6.0f;
  }
  result.root = static_cast<uint8_t>(std::lround(hue * 2.0f)) % 12;
  result.minor = result.brightness < 0.42f;
  result.bpm = static_cast<uint16_t>(std::lround(
      60.0f + 18.0f * result.brightness + 18.0f * result.edge));

  for (size_t stripe = 0; stripe < kNoteCount; ++stripe) {
    const size_t x0 = stripe * width / kNoteCount;
    size_t x1 = (stripe + 1) * width / kNoteCount;
    // Small test images still have sixteen notes, reusing columns as needed.
    if (x1 <= x0) x1 = x0 + 1;
    float brightnessSum = 0.0f, weightSum = 0.0f, weightedY = 0.0f;
    for (size_t y = 0; y < height; ++y) {
      for (size_t x = x0; x < x1; ++x) {
        const float luma = luminance(rgb + (y * width + x) * 3);
        const float weight = luma * luma;
        brightnessSum += luma;
        weightSum += weight;
        weightedY += weight * (height > 1 ? static_cast<float>(y) / (height - 1) : 0.5f);
      }
    }
    const float localBrightness = brightnessSum / ((x1 - x0) * height);
    const float centroid = weightSum > 0.000001f ? weightedY / weightSum : 0.5f;
    const float position = clamp(1.0f - centroid, 0.0f, 1.0f);
    const float target = clamp(48.0f + 36.0f * position +
                               4.0f * (localBrightness - result.brightness),
                               48.0f, 84.0f);
    result.notes[stripe].midi = nearestScaleNote(target, result.root, result.minor);
    result.notes[stripe].velocity = 0.35f + 0.55f * clamp(localBrightness, 0.0f, 1.0f);
  }
  out = result;
  return true;
}

void Synth::start(const Song& song) {
  song_ = song;
  if (song_.bpm < 60) song_.bpm = 60;
  if (song_.bpm > 96) song_.bpm = 96;
  for (Note& note : song_.notes) {
    if (note.midi < 48) note.midi = 48;
    if (note.midi > 84) note.midi = 84;
    note.velocity = std::isfinite(note.velocity) ? clamp(note.velocity, 0.0f, 1.0f) : 0.0f;
  }
  framesPerNote_ = (kSampleRate * 30 + song_.bpm / 2) / song_.bpm;
  totalFrames_ = framesPerNote_ * kNoteCount + kSampleRate / 10;
  frame_ = 0;
  beginNote(0);
}

void Synth::beginNote(size_t index) {
  const float frequency = 440.0f * std::pow(2.0f, (song_.notes[index].midi - 69.0f) / 12.0f);
  const float increment = 2.0f * kPi * frequency / kSampleRate;
  rotationSine_ = std::sin(increment);
  rotationCosine_ = std::cos(increment);
  sine_ = 0.0f;
  cosine_ = 1.0f;
}

size_t Synth::render(int16_t* interleavedStereo, size_t maxFrames) {
  if (!interleavedStereo || !maxFrames || done()) return 0;
  const size_t available = totalFrames_ - frame_;
  const size_t count = maxFrames < available ? maxFrames : available;
  const uint32_t musicalFrames = framesPerNote_ * kNoteCount;
  const float attackFrames = kSampleRate * 0.006f;
  const float releaseFrames = kSampleRate * 0.045f;
  for (size_t i = 0; i < count; ++i, ++frame_) {
    int16_t sample = 0;
    if (frame_ < musicalFrames) {
      const size_t noteIndex = frame_ / framesPerNote_;
      const uint32_t noteFrame = frame_ % framesPerNote_;
      if (noteFrame == 0) beginNote(noteIndex);
      const float attack = clamp(noteFrame / attackFrames, 0.0f, 1.0f);
      const float release = clamp((framesPerNote_ - 1 - noteFrame) / releaseFrames, 0.0f, 1.0f);
      const float decay = 1.0f - 0.55f * noteFrame / framesPerNote_;
      const float envelope = attack * release * decay;
      // Rotation recurrence avoids per-sample trig. Normalize occasionally to
      // keep floating-point drift bounded across a sustained note.
      const float wave = (sine_ + 0.15f * (2.0f * sine_ * cosine_)) / 1.15f;
      const float value = kMaxAmplitude * song_.notes[noteIndex].velocity * envelope * wave;
      sample = static_cast<int16_t>(std::lround(clamp(value, -kMaxAmplitude, kMaxAmplitude) * 32767.0f));
      const float nextSine = sine_ * rotationCosine_ + cosine_ * rotationSine_;
      cosine_ = cosine_ * rotationCosine_ - sine_ * rotationSine_;
      sine_ = nextSine;
      if ((noteFrame & 255U) == 255U) {
        const float scale = 1.0f / std::sqrt(sine_ * sine_ + cosine_ * cosine_);
        sine_ *= scale;
        cosine_ *= scale;
      }
    }
    interleavedStereo[i * 2] = sample;
    interleavedStereo[i * 2 + 1] = sample;
  }
  return count;
}

}  // namespace v2m
