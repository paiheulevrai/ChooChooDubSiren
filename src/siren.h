#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct SirenPatch {
  float pitch = 600.0f;
  float rate = 4.0f;
  float depth = 1.0f;
  float volume = 0.25f;
  float echoTime = 0.35f;
  float feedback = 0.45f;
  float echoMix = 0.35f;
  int wave = 0;
  std::array<int, 4> triggerWaves{{2, 1, 0, 5}};
  int lfoWave = 0;
  bool retrig = true;
  bool echo = false;
};

class Siren {
 public:
  explicit Siren(int sampleRate);
  bool setPatch(const SirenPatch& patch);
  void setGate(bool down);
  void panic();
  void render(int16_t* output, std::size_t frames);
  static bool valid(const SirenPatch& patch);

 private:
  static float waveform(float phase, int shape);
  float rate_;
  SirenPatch patch_;
  float phase_ = 0.0f, lfoPhase_ = 0.0f, envelope_ = 0.0f;
  float waveBlend_ = 1.0f;
  int previousWave_ = 0;
  float pitch_ = 600.0f, speed_ = 4.0f, depth_ = 1.0f, volume_ = 0.25f;
  float dcInput_ = 0.0f, dcOutput_ = 0.0f, dcCoefficient_;
  float filter_ = 0.0f, delayPosition_ = 0.0f;
  float flutterPhase_ = 0.0f;
  float tailFeedback_ = 0.45f;
  std::vector<float> delay_;
  std::size_t write_ = 0;
  bool gate_ = false, clearing_ = false;
  std::size_t clearRemaining_ = 0, clearIndex_ = 0;
};
