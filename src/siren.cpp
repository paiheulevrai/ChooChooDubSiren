#include "siren.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float pi = 3.14159265358979323846f;

bool inRange(float value, float lo, float hi) {
  return std::isfinite(value) && value >= lo && value <= hi;
}

float smooth(float value, float target, float coefficient) {
  return value + (target - value) * coefficient;
}
}

Siren::Siren(int sampleRate)
    : rate_(static_cast<float>(std::max(8000, sampleRate))),
      dcCoefficient_(std::exp(-2.0f * pi * 15.0f / rate_)),
      delayPosition_(rate_ * 0.35f),
      delay_(static_cast<std::size_t>(rate_ * 4.6f) + 4, 0.0f) {}

bool Siren::valid(const SirenPatch& p) {
  return inRange(p.pitch, 80.0f, 3000.0f) && inRange(p.rate, 0.1f, 30.0f) &&
      inRange(p.depth, 0.0f, 3.0f) && inRange(p.volume, 0.0f, 0.5f) &&
      inRange(p.echoTime, 0.05f, 1.5f) && inRange(p.feedback, 0.0f, 0.85f) &&
      inRange(p.echoMix, 0.0f, 0.75f) && p.wave >= 0 && p.wave <= 6 &&
      std::all_of(p.triggerWaves.begin(), p.triggerWaves.end(),
                  [](int wave) { return wave >= 0 && wave <= 6; }) &&
      p.lfoWave >= 0 && p.lfoWave <= 4;
}

bool Siren::setPatch(const SirenPatch& patch) {
  if (!valid(patch)) return false;
  if (patch.wave != patch_.wave) {
    previousWave_ = patch_.wave;
    waveBlend_ = 0.0f;
  }
  patch_ = patch;
  return true;
}

void Siren::setGate(bool down) {
  if (down && !gate_ && patch_.retrig) lfoPhase_ = 0.0f;
  gate_ = down;
}

void Siren::panic() {
  gate_ = false;
  clearing_ = true;
  clearRemaining_ = delay_.size();
  clearIndex_ = 0;
}

float Siren::waveform(float phase, int shape) {
  switch (shape) {
    case 0: return std::sin(2.0f * pi * phase);
    case 1: return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    case 2: return phase < 0.5f ? 1.0f : -1.0f;
    case 3: return 2.0f * phase - 1.0f;
    case 4: return 1.0f - 2.0f * phase;
    case 5: return phase < 0.125f ? 1.0f : -1.0f;
    case 6: return std::floor((1.0f - 4.0f * std::fabs(phase - 0.5f)) * 4.0f) / 4.0f;
    default: return 0.0f;
  }
}

void Siren::render(int16_t* output, std::size_t frames) {
  const float smoothing = 1.0f - std::exp(-1.0f / (0.02f * rate_));
  const float gateStep = 1.0f / (0.005f * rate_);
  const float tone = 1.0f - std::exp(-2.0f * pi * 4000.0f / rate_);
  for (std::size_t i = 0; i < frames; ++i) {
    pitch_ = smooth(pitch_, patch_.pitch, smoothing);
    speed_ = smooth(speed_, patch_.rate, smoothing);
    depth_ = smooth(depth_, patch_.depth, smoothing);
    volume_ = smooth(volume_, patch_.volume, smoothing);
    const float lfo = waveform(lfoPhase_, patch_.lfoWave);
    const float frequency = std::min(rate_ * 0.4f, pitch_ * std::exp2(lfo * depth_));
    phase_ += frequency / rate_;
    phase_ -= std::floor(phase_);
    lfoPhase_ += speed_ / rate_;
    lfoPhase_ -= std::floor(lfoPhase_);
    envelope_ = std::clamp(envelope_ + (gate_ ? gateStep : -gateStep), 0.0f, 1.0f);
    waveBlend_ = std::min(1.0f, waveBlend_ + gateStep);
    float wave = waveform(phase_, patch_.wave);
    if (waveBlend_ < 1.0f)
      wave = wave * waveBlend_ + waveform(phase_, previousWave_) * (1.0f - waveBlend_);
    const float dry = wave * envelope_ * volume_;

    delayPosition_ = smooth(delayPosition_, patch_.echoTime * rate_, smoothing);
    // Three tape heads and normalized feedback follow ChooChooTracker's MIT-licensed Tape Echo.
    const float flutter = std::sin(flutterPhase_) * rate_ * 0.0004f;
    flutterPhase_ += 2.0f * pi * 0.25f / rate_;
    if (flutterPhase_ >= 2.0f * pi) flutterPhase_ -= 2.0f * pi;
    float delayed = 0.0f;
    for (int head = 1; head <= 3; ++head) {
      float read = static_cast<float>(write_) - delayPosition_ * head - flutter;
      while (read < 0.0f) read += static_cast<float>(delay_.size());
      if (read >= static_cast<float>(delay_.size())) read -= static_cast<float>(delay_.size());
      const std::size_t first = static_cast<std::size_t>(read);
      const std::size_t next = (first + 1) % delay_.size();
      const float fraction = read - static_cast<float>(first);
      delayed += delay_[first] * (1.0f - fraction) + delay_[next] * fraction;
    }
    filter_ += tone * (delayed - filter_);
    if (patch_.echo) {
      tailFeedback_ = patch_.feedback;
    }
    delay_[write_] = (patch_.echo && !clearing_ ? dry * patch_.echoMix : 0.0f) +
        (clearing_ ? 0.0f : filter_ * tailFeedback_ / 3.0f);
    if (++write_ == delay_.size()) write_ = 0;
    if (clearing_) {
      for (int n = 0; n < 64 && clearRemaining_; ++n) {
        delay_[clearIndex_++] = 0.0f;
        --clearRemaining_;
      }
      if (!clearRemaining_) {
        clearing_ = false;
        filter_ = 0.0f;
      }
    }
    float sample = dry + (clearing_ ? 0.0f : filter_);
    if (!std::isfinite(sample)) sample = 0.0f;
    const float filtered = sample - dcInput_ + dcCoefficient_ * dcOutput_;
    dcInput_ = sample;
    dcOutput_ = filtered;
    sample = std::clamp(filtered, -0.8f, 0.8f);
    const int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
    output[i * 2] = pcm;
    output[i * 2 + 1] = pcm;
  }
}
