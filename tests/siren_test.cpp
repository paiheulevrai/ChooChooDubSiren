#include "../src/siren.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

static long long energy(const std::vector<int16_t>& data) {
  long long sum = 0;
  for (int16_t sample : data) sum += std::abs(static_cast<int>(sample));
  return sum;
}

int main() {
  Siren siren(48000);
  SirenPatch patch;
  std::vector<int16_t> audio(48000 * 2);
  siren.render(audio.data(), 48000);
  assert(energy(audio) == 0);
  siren.setGate(true);
  siren.render(audio.data(), 48000);
  assert(energy(audio) > 0);
  assert(*std::max_element(audio.begin(), audio.end()) <= 26214);
  siren.setGate(false);
  siren.render(audio.data(), 48000);
  assert(energy(audio) > 0);
  siren.render(audio.data(), 48000);
  assert(energy(audio) == 0);
  patch.echo = true;
  patch.echoTime = 0.05f;
  siren.setPatch(patch);
  siren.setGate(true);
  siren.render(audio.data(), 4800);
  siren.setGate(false);
  patch.echoMix = 0.0f;
  siren.setPatch(patch);
  siren.render(audio.data(), 4800);
  assert(std::any_of(audio.begin() + 1000 * 2, audio.begin() + 4800 * 2,
                     [](int16_t sample) { return sample != 0; }));
  patch.echo = false;
  siren.setPatch(patch);
  siren.render(audio.data(), 4800);
  assert(std::any_of(audio.begin(), audio.begin() + 4800 * 2,
                     [](int16_t sample) { return sample != 0; }));
  siren.panic();
  siren.render(audio.data(), 48000);
  siren.render(audio.data(), 48000);
  assert(energy(audio) == 0);
  patch.pitch = NAN;
  assert(!Siren::valid(patch));
  assert(!siren.setPatch(patch));

  Siren reset(48000), free(48000);
  SirenPatch resetPatch;
  resetPatch.rate = 1.0f;
  resetPatch.depth = 2.0f;
  resetPatch.wave = 0;
  reset.setPatch(resetPatch);
  resetPatch.retrig = false;
  free.setPatch(resetPatch);
  std::vector<int16_t> first(6000 * 2), second(6000 * 2);
  reset.render(first.data(), 6000);
  free.render(second.data(), 6000);
  reset.setGate(true);
  free.setGate(true);
  reset.render(first.data(), 6000);
  free.render(second.data(), 6000);
  assert(first != second);

  for (bool retrig : {false, true}) {
    Siren overlap(48000), continuing(48000);
    SirenPatch overlapPatch;
    overlapPatch.rate = 3.0f;
    overlapPatch.depth = 2.0f;
    overlapPatch.retrig = retrig;
    assert(overlap.setPatch(overlapPatch) && continuing.setPatch(overlapPatch));
    overlap.setGate(true);
    continuing.setGate(true);
    overlap.render(first.data(), 6000);
    continuing.render(second.data(), 6000);
    assert(first == second);
    overlap.setGate(true, true);
    continuing.setGate(true);
    overlap.render(first.data(), 6000);
    continuing.render(second.data(), 6000);
    assert((first != second) == retrig);
  }

  Siren safe(48000);
  SirenPatch loud;
  loud.echo = true;
  loud.feedback = 0.85f;
  loud.echoMix = 0.75f;
  loud.volume = 0.5f;
  loud.echoTime = 0.05f;
  safe.setPatch(loud);
  safe.setGate(true);
  safe.render(audio.data(), 48000);
  for (int16_t sample : audio) assert(std::abs(static_cast<int>(sample)) <= 26214);
  loud.echoTime = 1.5f;
  safe.setPatch(loud);
  for (int i = 0; i < 10; ++i) safe.render(audio.data(), 48000);
  safe.panic();
  safe.render(audio.data(), 4800);
  safe.setGate(true);
  safe.render(audio.data(), 48000);
  assert(energy(audio) > 0);
}
