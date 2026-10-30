#include "sonification.h"

#include <cmath>
#include <limits>

namespace v2m {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaxAmplitude = 0.175f;
// Reserve independent headroom for melody, kick, snare and hi-hat. The sum
// of their maximum gains is 0.171, below the final defensive 0.175 ceiling.
constexpr float kMelodyGain = 0.105f;
constexpr float kKickGain = 0.035f;
constexpr float kSnareGain = 0.022f;
constexpr float kHatGain = 0.009f;
constexpr uint8_t kMajor[] = {0, 2, 4, 7, 9};
constexpr uint8_t kMinor[] = {0, 3, 5, 7, 10};
constexpr uint32_t kDrumFadeFrames = (Synth::kSampleRate * 8U + 999U) / 1000U;

float clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

float luminance(const uint8_t* pixel) {
  return (0.2126f * pixel[0] + 0.7152f * pixel[1] +
          0.0722f * pixel[2]) / 255.0f;
}

struct PixelColor {
  float saturation;
  float value;
  float weight;
  uint8_t root;
  Timbre timbre;
};

PixelColor pixelColor(const uint8_t* pixel) {
  const float red = pixel[0], green = pixel[1], blue = pixel[2];
  const float maximum = std::fmax(red, std::fmax(green, blue));
  const float minimum = std::fmin(red, std::fmin(green, blue));
  const float delta = maximum - minimum;
  PixelColor color{maximum > 0.0f ? delta / maximum : 0.0f,
                   maximum / 255.0f, 0.0f, 0, Timbre::Keys};
  // Near-neutral and nearly black pixels do not cast unstable hue votes.
  if (color.saturation < 0.15f || color.value < 0.05f) return color;
  float hue;
  if (maximum == red) hue = (green - blue) / delta;
  else if (maximum == green) hue = 2.0f + (blue - red) / delta;
  else hue = 4.0f + (red - green) / delta;
  if (hue < 0.0f) hue += 6.0f;
  hue *= 60.0f;
  color.root = static_cast<uint8_t>(std::lround(hue / 30.0f)) % 12;
  color.weight = color.saturation * color.value;  // HSV value, not RGB mean.
  if (hue < 45.0f || hue >= 330.0f) color.timbre = Timbre::Pluck;
  else if (hue < 165.0f) color.timbre = Timbre::Keys;
  else if (hue < 270.0f) color.timbre = Timbre::Flute;
  else color.timbre = Timbre::Bell;
  return color;
}

// Bounded sine approximation for the two percussion oscillators. Avoids
// per-sample trigonometry while allowing the kick's frequency to fall.
float percussionSine(float phase) {
  float x = phase * 2.0f;
  if (x > 1.0f) x -= 2.0f;
  const float y = 4.0f * x * (1.0f - std::fabs(x));
  return y + 0.225f * (y * std::fabs(y) - y);
}

void advanceOscillator(float& sine, float& cosine, float rotationSine,
                       float rotationCosine, bool normalize) {
  const float nextSine = sine * rotationCosine + cosine * rotationSine;
  cosine = cosine * rotationCosine - sine * rotationSine;
  sine = nextSine;
  if (normalize) {
    const float scale = 1.0f / std::sqrt(sine * sine + cosine * cosine);
    sine *= scale;
    cosine *= scale;
  }
}

uint8_t nearestScaleNote(float target, uint8_t root, bool minor) {
  const uint8_t* scale = minor ? kMinor : kMajor;
  uint8_t best = 60;
  float bestDistance = 100.0f;
  for (uint8_t midi = 48; midi <= 84; ++midi) {
    const uint8_t interval = (midi + 12 - root) % 12;
    for (size_t i = 0; i < 5; ++i) {
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

const char* timbreName(Timbre timbre) {
  switch (timbre) {
    case Timbre::Keys: return "Keys";
    case Timbre::Pluck: return "Pluck";
    case Timbre::Flute: return "Flute";
    case Timbre::Bell: return "Bell";
  }
  return "Keys";
}

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
  float hueVotes[12] = {};
  float edgeSum = 0.0f;
  size_t edgeCount = 0;
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      const size_t index = (y * width + x) * 3;
      const uint8_t* p = rgb + index;
      const float luma = luminance(p);
      result.brightness += luma;
      const PixelColor color = pixelColor(p);
      result.saturation += color.saturation;
      hueVotes[color.root] += color.weight;
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
  // Weighted hue histogram preserves competing colors instead of averaging
  // complementary RGB values into gray. Ties choose the lower pitch class;
  // an image with no chromatic votes falls back to C.
  result.root = 0;
  for (uint8_t root = 1; root < 12; ++root) {
    if (hueVotes[root] > hueVotes[result.root]) result.root = root;
  }
  result.minor = result.brightness < 0.42f;
  result.bpm = static_cast<uint16_t>(std::lround(
      60.0f + 18.0f * result.brightness + 18.0f * result.edge));

  for (size_t stripe = 0; stripe < kNoteCount; ++stripe) {
    const size_t x0 = stripe * width / kNoteCount;
    size_t x1 = (stripe + 1) * width / kNoteCount;
    // Small test images still have sixteen notes, reusing columns as needed.
    if (x1 <= x0) x1 = x0 + 1;
    float brightnessSum = 0.0f, weightSum = 0.0f, weightedY = 0.0f;
    float timbreVotes[4] = {};
    float valueSum = 0.0f, chromaticWeight = 0.0f;
    for (size_t y = 0; y < height; ++y) {
      for (size_t x = x0; x < x1; ++x) {
        const uint8_t* pixel = rgb + (y * width + x) * 3;
        const float luma = luminance(pixel);
        const PixelColor color = pixelColor(pixel);
        valueSum += color.value;
        chromaticWeight += color.weight;
        timbreVotes[static_cast<uint8_t>(color.timbre)] += color.weight;
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
    uint8_t selected = 0;  // Neutral -> Keys; tied winning bins use enum order.
    if (chromaticWeight > 0.0f && chromaticWeight >= valueSum * 0.12f) {
      for (uint8_t timbre = 1; timbre < 4; ++timbre) {
        if (timbreVotes[timbre] > timbreVotes[selected]) selected = timbre;
      }
    }
    result.notes[stripe].timbre = static_cast<Timbre>(selected);
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
    if (static_cast<uint8_t>(note.timbre) > static_cast<uint8_t>(Timbre::Bell)) note.timbre = Timbre::Keys;
  }
  framesPerNote_ = (kSampleRate * 30 + song_.bpm / 2) / song_.bpm;
  totalFrames_ = framesPerNote_ * kNoteCount + kSampleRate / 10;
  frame_ = 0;
  drumTriggersActive_ = false;
  drumGain_ = 0.0f;
  kickEnvelope_ = snareEnvelope_ = hatEnvelope_ = 0.0f;
  kickPhase_ = snarePhase_ = 0.0f;
  kickFrequency_ = 55.0f;
  kickAge_ = snareAge_ = hatAge_ = 0;
  noiseState_ = 0x6d2b79f5U;
  previousNoise_ = noiseLowpass_ = 0.0f;
  kickDecay_ = std::exp(-1.0f / (0.075f * kSampleRate));
  snareDecay_ = std::exp(-1.0f / (0.045f * kSampleRate));
  hatDecay_ = std::exp(-1.0f / (0.015f * kSampleRate));
  kickPitchSlew_ = 1.0f - std::exp(-1.0f / (0.018f * kSampleRate));
  beginNote(0);
}

void Synth::setDrumsEnabled(bool enabled) {
  drumsEnabled_ = enabled;
  if (!enabled) drumTriggersActive_ = false;
  // Enabling waits for triggerDrums at a musical boundary. Existing tails
  // keep fading if on is requested again before the next boundary arrives.
}

void Synth::beginNote(size_t index) {
  const float frequency = 440.0f * std::pow(2.0f, (song_.notes[index].midi - 69.0f) / 12.0f);
  const float increment = 2.0f * kPi * frequency / kSampleRate;
  rotationSine_ = std::sin(increment);
  rotationCosine_ = std::cos(increment);
  sine_ = 0.0f;
  cosine_ = 1.0f;
  partialSine_ = 0.0f;
  partialCosine_ = 1.0f;
  toneEnvelope_ = partialEnvelope_ = 1.0f;
  toneDecay_ = 1.0f;
  partialDecay_ = 1.0f;
  attackFrames_ = kSampleRate * 0.006f;
  switch (song_.notes[index].timbre) {
    case Timbre::Keys:
      toneDecay_ = std::exp(-1.0f / (0.35f * kSampleRate));
      break;
    case Timbre::Pluck:
      attackFrames_ = kSampleRate * 0.003f;
      toneDecay_ = std::exp(-1.0f / (0.14f * kSampleRate));
      break;
    case Timbre::Flute:
      attackFrames_ = kSampleRate * 0.035f;
      break;
    case Timbre::Bell:
      toneDecay_ = std::exp(-1.0f / (0.48f * kSampleRate));
      partialDecay_ = std::exp(-1.0f / (0.16f * kSampleRate));
      partialRotationSine_ = std::sin(increment * 2.76f);
      partialRotationCosine_ = std::cos(increment * 2.76f);
      break;
  }
}

void Synth::triggerDrums(size_t step) {
  if (!drumsEnabled_) return;
  drumTriggersActive_ = true;
  // Eighth-note hats; kick on beats 1/3 and snare on beats 2/4 in each bar.
  hatEnvelope_ = 1.0f;
  hatAge_ = 0;
  if (step % 4 == 0) {
    kickEnvelope_ = 1.0f;
    kickAge_ = 0;
    kickPhase_ = 0.0f;
    kickFrequency_ = 145.0f;
  } else if (step % 4 == 2) {
    snareEnvelope_ = 1.0f;
    snareAge_ = 0;
    snarePhase_ = 0.0f;
  }
}

float Synth::renderDrums() {
  // Advance the deterministic noise stream on the shared sample clock even
  // when drums are muted; there are no independent timers or random seeds.
  noiseState_ ^= noiseState_ << 13;
  noiseState_ ^= noiseState_ >> 17;
  noiseState_ ^= noiseState_ << 5;
  const float noise = static_cast<float>(noiseState_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
  noiseLowpass_ += 0.12f * (noise - noiseLowpass_);
  const float snareNoise = (noise - noiseLowpass_) * 0.5f;
  const float hatNoise = (noise - previousNoise_) * 0.5f;
  previousNoise_ = noise;

  constexpr float fadeStep = 1.0f / kDrumFadeFrames;
  drumGain_ = clamp(drumGain_ + (drumTriggersActive_ ? fadeStep : -fadeStep), 0.0f, 1.0f);
  if (drumGain_ == 0.0f && !drumTriggersActive_) {
    kickEnvelope_ = snareEnvelope_ = hatEnvelope_ = 0.0f;
    return 0.0f;
  }

  constexpr float attackFrames = kSampleRate * 0.001f;
  float mixed = 0.0f;
  if (kickEnvelope_ > 0.00001f) {
    const float attack = clamp(kickAge_++ / attackFrames, 0.0f, 1.0f);
    mixed += kKickGain * percussionSine(kickPhase_) * kickEnvelope_ * attack;
    kickFrequency_ += (55.0f - kickFrequency_) * kickPitchSlew_;
    kickPhase_ += kickFrequency_ / kSampleRate;
    if (kickPhase_ >= 1.0f) kickPhase_ -= 1.0f;
    kickEnvelope_ *= kickDecay_;
  }
  if (snareEnvelope_ > 0.00001f) {
    const float attack = clamp(snareAge_++ / attackFrames, 0.0f, 1.0f);
    const float wave = 0.8f * snareNoise + 0.2f * percussionSine(snarePhase_);
    mixed += kSnareGain * wave * snareEnvelope_ * attack;
    snarePhase_ += 185.0f / kSampleRate;
    if (snarePhase_ >= 1.0f) snarePhase_ -= 1.0f;
    snareEnvelope_ *= snareDecay_;
  }
  if (hatEnvelope_ > 0.00001f) {
    const float attack = clamp(hatAge_++ / attackFrames, 0.0f, 1.0f);
    mixed += kHatGain * hatNoise * hatEnvelope_ * attack;
    hatEnvelope_ *= hatDecay_;
  }
  // All percussion fades before the musical endpoint; the following 100 ms
  // are exactly silent even if a switch is pressed during that tail.
  const float endFade = clamp((musicalFrames() - 1 - frame_) /
                              (kSampleRate * 0.012f), 0.0f, 1.0f);
  return mixed * drumGain_ * endFade;
}

size_t Synth::render(int16_t* interleavedStereo, size_t maxFrames) {
  if (!interleavedStereo || !maxFrames || done()) return 0;
  const size_t available = totalFrames_ - frame_;
  const size_t count = maxFrames < available ? maxFrames : available;
  const uint32_t musicFrames = musicalFrames();
  const float releaseFrames = kSampleRate * 0.045f;
  for (size_t i = 0; i < count; ++i, ++frame_) {
    int16_t sample = 0;
    if (frame_ < musicFrames) {
      const size_t noteIndex = frame_ / framesPerNote_;
      const uint32_t noteFrame = frame_ % framesPerNote_;
      if (noteFrame == 0) {
        beginNote(noteIndex);
        triggerDrums(noteIndex);
      }
      const float attack = clamp(noteFrame / attackFrames_, 0.0f, 1.0f);
      const float release = clamp((framesPerNote_ - 1 - noteFrame) / releaseFrames, 0.0f, 1.0f);
      const float envelope = attack * release;
      // Rotation recurrence avoids per-sample trig. Normalize occasionally to
      // keep floating-point drift bounded across a sustained note.
      const float second = 2.0f * sine_ * cosine_;
      const float third = 3.0f * sine_ - 4.0f * sine_ * sine_ * sine_;
      const Timbre timbre = song_.notes[noteIndex].timbre;
      float wave = 0.0f;
      switch (timbre) {
        case Timbre::Keys:
          wave = toneEnvelope_ * (sine_ + 0.26f * second + 0.11f * third) / 1.37f;
          break;
        case Timbre::Pluck:
          wave = toneEnvelope_ * (sine_ + 0.34f * second + 0.18f * third) / 1.52f;
          break;
        case Timbre::Flute:
          wave = (sine_ + 0.08f * second) / 1.08f *
                 (1.0f - 0.18f * noteFrame / framesPerNote_);
          break;
        case Timbre::Bell:
          wave = (sine_ * toneEnvelope_ + 0.35f * partialSine_ * partialEnvelope_) / 1.35f;
          break;
      }
      const float value = kMelodyGain * song_.notes[noteIndex].velocity * envelope * wave + renderDrums();
      sample = static_cast<int16_t>(std::lround(clamp(value, -kMaxAmplitude, kMaxAmplitude) * 32767.0f));
      const bool normalize = (noteFrame & 255U) == 255U;
      advanceOscillator(sine_, cosine_, rotationSine_, rotationCosine_, normalize);
      toneEnvelope_ *= toneDecay_;
      if (timbre == Timbre::Bell) {
        advanceOscillator(partialSine_, partialCosine_, partialRotationSine_,
                          partialRotationCosine_, normalize);
        partialEnvelope_ *= partialDecay_;
      }
    }
    interleavedStereo[i * 2] = sample;
    interleavedStereo[i * 2 + 1] = sample;
  }
  return count;
}

}  // namespace v2m
