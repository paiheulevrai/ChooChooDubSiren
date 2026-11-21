#ifndef __ANDROID__
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>

#include "siren.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" const uint8_t font12x16[];

namespace {
constexpr int width = 640, height = 480;
constexpr int parameterCount = 17, axisCount = 4, triggerCount = 4;
constexpr int leftTrigger = SDL_CONTROLLER_BUTTON_MAX;
constexpr int rightTrigger = SDL_CONTROLLER_BUTTON_MAX + 1;
const char* waves[] = {"SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN", "PULSE", "LOFI TRI"};
const char* lfoWaves[] = {"SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN"};
const char* axisNames[] = {"LX", "LY", "RX", "RY"};
const char* assignmentNames[] = {"OFF", "PITCH", "LFO RATE", "LFO DEPTH"};
const char* triggerNames[] = {"L1", "L2", "R1", "R2"};
const char* factoryNames[] = {"DUB", "REGGAE", "STEPPER", "DANCEHALL"};

struct Instrument {
  float pitch = 600.0f, rate = 4.0f, depth = 1.0f;
  int wave = 0, lfoWave = 0;
  bool retrig = true;
};

struct Sound {
  std::array<Instrument, triggerCount> instruments{{
      {600, 4, 1, 2, 0, true}, {600, 4, 1, 1, 0, true},
      {600, 4, 1, 0, 0, true}, {600, 4, 1, 5, 0, true}}};
  float volume = 0.25f, echoTime = 0.35f, feedback = 0.45f, echoMix = 0.35f;
  bool echo = false;
};

SirenPatch voicePatch(const Sound& sound, int instrument) {
  const Instrument& voice = sound.instruments[instrument];
  SirenPatch patch;
  patch.pitch = voice.pitch;
  patch.rate = voice.rate;
  patch.depth = voice.depth;
  patch.wave = voice.wave;
  patch.lfoWave = voice.lfoWave;
  patch.retrig = voice.retrig;
  patch.volume = sound.volume;
  patch.echoTime = sound.echoTime;
  patch.feedback = sound.feedback;
  patch.echoMix = sound.echoMix;
  patch.echo = sound.echo;
  return patch;
}

bool validSound(const Sound& sound) {
  for (int i = 0; i < triggerCount; ++i)
    if (!Siren::valid(voicePatch(sound, i))) return false;
  return true;
}

Sound factoryPreset(int index);

struct Pointer {
  bool active = false, mouse = false;
  SDL_TouchID device = 0;
  SDL_FingerID finger = 0;
  int trigger = -1, control = -1, axis = -1, field = -1;
  float lastY = 0.0f, remainder = 0.0f;
};

struct App {
  Siren siren{48000};
  Sound patch = factoryPreset(0);
  SDL_AudioDeviceID audio = 0;
  SDL_GameController* controller = nullptr;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  std::string folder;
  std::string status = "READY";
  std::array<int, axisCount> assignments{{1, 2, 3, 0}};
  std::array<int, axisCount> axisRate{{100, 100, 100, 100}};
  std::array<bool, axisCount> axisInvert{{false, true, false, false}};
  std::array<unsigned, SDL_CONTROLLER_BUTTON_MAX> heldButtons{};
  std::array<unsigned, triggerCount> triggers{};
  std::array<uint64_t, triggerCount> triggerOrder{};
  std::array<Pointer, 8> pointers{};
  uint64_t nextTriggerOrder = 0;
  int activeTrigger = -1;
  int selectedInstrument = 0;
  std::atomic<int> peak{0};
  int selection = 0;
  int page = 0;
  int theme = 0;
  int preset = 0;
  unsigned gateSources = 0;
  unsigned triggerHeld = 0;
  bool triggerBlocked = false;
  bool running = true;
  bool assignMode = false;
#ifdef __ANDROID__
  bool touchUi = true;
#else
  bool touchUi = false;
#endif
  bool editUsed = false;
  bool editWithSelect = false;
  bool selectUsed = false;
  unsigned selectSources = 0;
  bool powerHeld = false;
  Uint32 powerPressed = 0;
  int selectedAxis = 0;
  int selectedAxisField = 0;
};

void audioCallback(void* user, Uint8* stream, int length) {
  App& app = *static_cast<App*>(user);
  app.siren.render(reinterpret_cast<int16_t*>(stream), static_cast<std::size_t>(length) / 4);
  const auto* samples = reinterpret_cast<const int16_t*>(stream);
  int peak = 0;
  for (int i = 0; i < length / 2; ++i)
    peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
  app.peak.store(peak, std::memory_order_relaxed);
}

void updateAudio(App& app) {
  const SirenPatch sounding = voicePatch(app.patch,
      app.activeTrigger >= 0 ? app.activeTrigger : app.selectedInstrument);
  SDL_LockAudioDevice(app.audio);
  app.siren.setPatch(sounding);
  SDL_UnlockAudioDevice(app.audio);
}

void panic(App& app);

void trigger(App& app, int index, unsigned source, bool down) {
  const bool wasDown = app.triggers[index] != 0;
  if (down) app.triggers[index] |= source;
  else app.triggers[index] &= ~source;
  const bool isDown = app.triggers[index] != 0;
  const bool pressed = !wasDown && isDown;
  if (pressed) app.triggerOrder[index] = ++app.nextTriggerOrder;
  app.triggerHeld = 0;
  for (unsigned held : app.triggers) app.triggerHeld |= held;
  if (app.triggerBlocked) {
    if (!app.triggerHeld) app.triggerBlocked = false;
    return;
  }
  const int previousTrigger = app.activeTrigger;
  app.activeTrigger = -1;
  for (int i = 0; i < triggerCount; ++i)
    if (app.triggers[i] && (app.activeTrigger < 0 ||
        app.triggerOrder[i] > app.triggerOrder[app.activeTrigger])) app.activeTrigger = i;
  app.gateSources = app.activeTrigger >= 0 ? 1 : 0;
  if (pressed) app.selectedInstrument = index;
  else if (app.activeTrigger >= 0 && app.activeTrigger != previousTrigger)
    app.selectedInstrument = app.activeTrigger;
  if (app.page == 0 && app.selection >= 3 && app.selection <= 6 &&
      (pressed || app.activeTrigger != previousTrigger) && app.activeTrigger >= 0)
    app.selection = 3 + app.selectedInstrument;
  const SirenPatch sounding = voicePatch(app.patch,
      app.activeTrigger >= 0 ? app.activeTrigger : app.selectedInstrument);
  SDL_LockAudioDevice(app.audio);
  app.siren.setPatch(sounding);
  app.siren.setGate(app.gateSources != 0, pressed);
  SDL_UnlockAudioDevice(app.audio);
}

void triggerAxis(App& app, int index, int value) {
  const bool held = (app.triggers[index] & 2u) != 0;
  const bool down = value > (held ? 12000 : 16000);
  if (held != down) trigger(app, index, 2, down);
}

void panic(App& app) {
  app.gateSources = 0;
  app.activeTrigger = -1;
  app.triggerBlocked = app.triggerHeld != 0;
  SDL_LockAudioDevice(app.audio);
  app.siren.panic();
  SDL_UnlockAudioDevice(app.audio);
  app.status = "PANIC: AUDIO STOPPED";
}

#ifndef _WIN32
void suspendNextUI(App& app) {
  const char* system = std::getenv("SYSTEM_PATH");
  if (!system) { app.status = "SUSPEND UNAVAILABLE"; return; }
  const std::string executable = std::string(system) + "/bin/suspend";
  if (access(executable.c_str(), X_OK) != 0) { app.status = "SUSPEND UNAVAILABLE"; return; }
  panic(app);
  SDL_PauseAudioDevice(app.audio, 1);
  sync();
  const pid_t child = fork();
  int status = -1;
  if (child == 0) {
    execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (child > 0) {
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) status = -1;
  }
  SDL_FlushEvents(SDL_KEYDOWN, SDL_KEYUP);
  SDL_FlushEvents(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP);
  SDL_FlushEvent(SDL_CONTROLLERAXISMOTION);
  app.selectSources = 0;
  app.triggerHeld = 0;
  app.triggerBlocked = false;
  app.triggers.fill(0);
  app.heldButtons.fill(0);
  app.pointers.fill(Pointer{});
  app.powerHeld = false;
  SDL_PauseAudioDevice(app.audio, 0);
  app.status = status == 0 ? "RESUMED" : "SUSPEND FAILED";
}
#endif

bool replaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
  return MoveFileExW(std::filesystem::u8path(from).c_str(), std::filesystem::u8path(to).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool writeFile(const std::string& path, const std::string& data) {
  const std::string temporary = path + ".tmp";
  std::ofstream out(std::filesystem::u8path(temporary), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << data;
  out.flush();
  if (!out) return false;
  out.close();
  if (!out) return false;
  return replaceFile(temporary, path);
}

std::string presetPath(const App& app, int slot) {
  return app.folder + "preset" + std::to_string(slot + 1) + ".txt";
}

bool savePreset(const App& app, int slot) {
  std::ostringstream s;
  const auto& p = app.patch;
  s.precision(9);
  s << "DUBSIREN3\n" << p.volume << ' ' << p.echoTime << ' '
    << p.feedback << ' ' << p.echoMix << ' ' << p.echo << '\n';
  for (const Instrument& voice : p.instruments)
    s << voice.pitch << ' ' << voice.rate << ' ' << voice.depth << ' '
      << voice.wave << ' ' << voice.lfoWave << ' ' << voice.retrig << '\n';
  return writeFile(presetPath(app, slot), s.str());
}

bool loadPreset(const App& app, int slot, Sound& result) {
  std::ifstream in(std::filesystem::u8path(presetPath(app, slot)), std::ios::binary);
  std::string header, extra;
  int retrig, echo;
  Sound p;
  if (!(in >> header)) return false;
  if (header == "DUBSIREN3") {
    if (!(in >> p.volume >> p.echoTime >> p.feedback >> p.echoMix >> echo) ||
        (echo != 0 && echo != 1)) return false;
    for (Instrument& voice : p.instruments) {
      if (!(in >> voice.pitch >> voice.rate >> voice.depth >> voice.wave >> voice.lfoWave >> retrig) ||
          (retrig != 0 && retrig != 1)) return false;
      voice.retrig = retrig != 0;
    }
  } else if (header == "DUBSIREN1" || header == "DUBSIREN2") {
    Instrument legacy;
    if (!(in >> legacy.pitch >> legacy.rate >> legacy.depth >> p.volume >> p.echoTime >>
          p.feedback >> p.echoMix >> legacy.wave >> legacy.lfoWave >> retrig >> echo) ||
        (retrig != 0 && retrig != 1) || (echo != 0 && echo != 1) ||
        legacy.wave < 0 || legacy.wave > 6) return false;
    legacy.retrig = retrig != 0;
    const std::array<int, triggerCount> defaultWaves{{2, 1, legacy.wave, 5}};
    for (int i = 0; i < triggerCount; ++i) {
      p.instruments[i] = legacy;
      if (header == "DUBSIREN1") p.instruments[i].wave = defaultWaves[i];
      else if (!(in >> p.instruments[i].wave)) return false;
    }
  } else return false;
  if (in >> extra) return false;
  p.echo = echo != 0;
  if (!validSound(p)) return false;
  result = p;
  return true;
}

bool saveAssignments(const App& app) {
  std::ostringstream s;
  s << "DUBAXIS2";
  for (int i = 0; i < axisCount; ++i)
    s << ' ' << app.assignments[i] << ' ' << app.axisRate[i] << ' ' << app.axisInvert[i];
  s << '\n';
  return writeFile(app.folder + "axes.txt", s.str());
}

void loadAssignments(App& app) {
  const std::string path = app.folder + "axes.txt";
  std::ifstream in(std::filesystem::u8path(path));
  if (!in) {
    std::error_code error;
    if (std::filesystem::exists(std::filesystem::u8path(path), error) || error)
      app.status = "CANNOT READ AXIS FILE";
    return;
  }
  std::string header, extra;
  std::array<int, axisCount> assignments{}, rates{{100, 100, 100, 100}};
  std::array<bool, axisCount> inverted{};
  bool invalid = !(in >> header) || (header != "DUBAXIS1" && header != "DUBAXIS2");
  bool removed = false;
  for (int i = 0; i < axisCount && !invalid; ++i) {
    int invert = 0;
    if (!(in >> assignments[i]) || assignments[i] < 0 ||
        assignments[i] > (header == "DUBAXIS1" ? 7 : 3)) {
      invalid = true;
    } else if (header == "DUBAXIS1") {
      inverted[i] = assignments[i] == 2;
      if (assignments[i] > 3) { assignments[i] = 0; removed = true; }
    } else if (!(in >> rates[i] >> invert) || rates[i] < 10 || rates[i] > 300 ||
               (invert != 0 && invert != 1)) {
      invalid = true;
    } else {
      inverted[i] = invert != 0;
    }
  }
  if (invalid || (in >> extra)) {
    app.status = "INVALID AXIS FILE";
    return;
  }
  app.assignments = assignments;
  app.axisRate = rates;
  app.axisInvert = inverted;
  if (removed) app.status = "COMMON AXES DISABLED";
}

bool saveTheme(const App& app) {
  return writeFile(app.folder + "theme.txt", std::string("DUBTHEME1 ") +
                   std::to_string(app.theme) + "\n");
}

void loadTheme(App& app) {
  const std::string path = app.folder + "theme.txt";
  std::ifstream in(std::filesystem::u8path(path));
  if (!in) {
    std::error_code error;
    if (std::filesystem::exists(std::filesystem::u8path(path), error) || error)
      app.status = "CANNOT READ THEME FILE";
    return;
  }
  std::string header, extra;
  int theme;
  if (!(in >> header >> theme) || header != "DUBTHEME1" ||
      (theme != 0 && theme != 1) || (in >> extra)) {
    app.status = "INVALID THEME FILE";
    return;
  }
  app.theme = theme;
}

Sound factoryPreset(int index) {
  Sound p;
  switch (index) {
    case 0:
      p.instruments = {{{420, 2.2f, 1.2f, 0, 0, true}, {650, 5, 1.8f, 2, 1, true},
                        {800, 0.7f, 0.7f, 1, 0, false}, {360, 9, 1.1f, 5, 4, true}}};
      p.echo = true; p.echoTime = 0.38f; p.feedback = 0.55f; p.echoMix = 0.38f; break;
    case 1:
      p.instruments = {{{560, 3.2f, 0.8f, 1, 0, true}, {740, 6, 1.1f, 0, 1, true},
                        {480, 1.2f, 1.4f, 6, 3, false}, {950, 4, 0.6f, 5, 0, true}}};
      p.echo = true; p.echoTime = 0.3f; p.feedback = 0.4f; p.echoMix = 0.3f; break;
    case 2:
      p.instruments = {{{330, 7, 1.5f, 2, 2, true}, {480, 12, 0.8f, 5, 1, true},
                        {720, 4, 2, 3, 4, true}, {270, 2, 1.2f, 6, 0, false}}};
      p.echo = true; p.echoTime = 0.24f; p.feedback = 0.5f; p.echoMix = 0.32f; break;
    case 3:
      p.instruments = {{{900, 8, 1.2f, 5, 3, true}, {1200, 14, 0.6f, 2, 2, true},
                        {620, 5, 1.7f, 4, 1, true}, {760, 0.8f, 0.9f, 1, 4, false}}};
      p.echo = true; p.echoTime = 0.19f; p.feedback = 0.32f; p.echoMix = 0.25f; break;
  }
  return p;
}

void recall(App& app) {
  if (app.triggerHeld) {
    app.status = "RELEASE TRIGGER FIRST";
    return;
  }
  Sound loaded;
  if (app.preset < 4) loaded = factoryPreset(app.preset);
  else if (!loadPreset(app, app.preset - 4, loaded)) {
    app.status = "PRESET MISSING OR INVALID";
    return;
  }
  app.patch = loaded;
  updateAudio(app);
  app.status = "PRESET LOADED";
}

void adjust(App& app, int index, float direction) {
  Sound& p = app.patch;
  Instrument& voice = p.instruments[app.selectedInstrument];
  switch (index) {
    case 0: voice.pitch = std::clamp(voice.pitch + direction * 30.0f, 80.0f, 3000.0f); break;
    case 1: voice.rate = std::clamp(voice.rate + direction * 0.2f, 0.1f, 30.0f); break;
    case 2: voice.depth = std::clamp(voice.depth + direction * 0.05f, 0.0f, 3.0f); break;
    case 3: case 4: case 5: case 6:
      p.instruments[index - 3].wave = (p.instruments[index - 3].wave + (direction > 0 ? 1 : 6)) % 7; break;
    case 7: voice.lfoWave = (voice.lfoWave + (direction > 0 ? 1 : 4)) % 5; break;
    case 8: voice.retrig = !voice.retrig; break;
    case 9: p.volume = std::clamp(p.volume + direction * 0.01f, 0.0f, 0.5f); break;
    case 10: p.echo = !p.echo; break;
    case 11: p.echoTime = std::clamp(p.echoTime + direction * 0.01f, 0.05f, 1.5f); break;
    case 12: p.feedback = std::clamp(p.feedback + direction * 0.01f, 0.0f, 0.85f); break;
    case 13: p.echoMix = std::clamp(p.echoMix + direction * 0.01f, 0.0f, 0.75f); break;
    case 14: app.preset = (app.preset + (direction > 0 ? 1 : 7)) % 8; break;
    case 15:
      app.theme = 1 - app.theme;
      app.status = saveTheme(app) ? "THEME SAVED" : "ERROR SAVING THEME";
      break;
    case 16: voice.wave = (voice.wave + (direction > 0 ? 1 : 6)) % 7; break;
  }
  updateAudio(app);
}

void text(SDL_Renderer* r, int x, int y, const std::string& value, SDL_Color color) {
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
  for (unsigned char ch : value) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* glyph = font12x16 + (ch - 32) * 32;
    for (int row = 0; row < 16; ++row)
      for (int col = 0; col < 12; ++col)
        if (glyph[row * 2 + col / 8] & (0x80 >> (col % 8)))
          SDL_RenderDrawPoint(r, x + col, y + row);
    x += 12;
  }
}

void rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
  SDL_Rect box{x, y, w, h};
  SDL_RenderFillRect(r, &box);
}

struct Palette {
  SDL_Color background, chassis, inset, edge, face, label, muted, accent, focus;
};

constexpr SDL_Color rastaGreen{7, 137, 48, 255};
constexpr SDL_Color rastaGold{252, 221, 9, 255};
constexpr SDL_Color rastaRed{218, 18, 26, 255};

Palette palette(int theme) {
  if (theme == 0)
    return {{39, 42, 36, 255}, {218, 210, 184, 255}, {198, 190, 166, 255},
            {108, 104, 85, 255}, {49, 55, 48, 255}, {31, 35, 28, 255},
            {94, 98, 82, 255}, {20, 77, 30, 255}, rastaRed};
  return {{15, 19, 15, 255}, {43, 51, 42, 255}, {31, 39, 30, 255},
          {105, 113, 98, 255}, {20, 25, 20, 255}, {235, 230, 207, 255},
          {156, 165, 147, 255}, {113, 218, 101, 255}, rastaGold};
}

float dialAngle(float value) {
  constexpr float pi = 3.14159265358979323846f;
  return -5.0f * pi / 6.0f + std::clamp(value, 0.0f, 1.0f) * 5.0f * pi / 3.0f;
}

void disc(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color color) {
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
  for (int y = -radius; y <= radius; ++y) {
    const int x = static_cast<int>(std::sqrt(radius * radius - y * y));
    SDL_RenderDrawLine(r, cx - x, cy + y, cx + x, cy + y);
  }
}

void dial(SDL_Renderer* r, int cx, int cy, float value, const Palette& p) {
  value = std::clamp(value, 0.0f, 1.0f);
  const int current = static_cast<int>(std::round(value * 24.0f));
  for (int i = 0; i <= 24; ++i) {
    const float angle = dialAngle(i / 24.0f);
    const SDL_Color tick = i <= current ? p.focus : p.muted;
    SDL_SetRenderDrawColor(r, tick.r, tick.g, tick.b, 255);
    const int inner = 35;
    const int x1 = cx + static_cast<int>(std::sin(angle) * inner);
    const int y1 = cy - static_cast<int>(std::cos(angle) * inner);
    const int x2 = cx + static_cast<int>(std::sin(angle) * 41);
    const int y2 = cy - static_cast<int>(std::cos(angle) * 41);
    const int dx = static_cast<int>(std::round(std::cos(angle)));
    const int dy = static_cast<int>(std::round(std::sin(angle)));
    for (int offset = -1; offset <= 1; ++offset)
      SDL_RenderDrawLine(r, x1 + offset * dx, y1 + offset * dy,
                         x2 + offset * dx, y2 + offset * dy);
  }
  const SDL_Color shine{static_cast<Uint8>(std::min(255, p.face.r + 18)),
                        static_cast<Uint8>(std::min(255, p.face.g + 18)),
                        static_cast<Uint8>(std::min(255, p.face.b + 18)), 255};
  disc(r, cx, cy + 2, 33, p.face);
  disc(r, cx, cy, 33, p.edge);
  disc(r, cx, cy, 31, p.face);
  disc(r, cx, cy, 28, shine);
  disc(r, cx, cy, 26, p.face);
  disc(r, cx, cy, 23, shine);
  const float angle = dialAngle(value);
  const int dx = static_cast<int>(std::round(std::cos(angle)));
  const int dy = static_cast<int>(std::round(std::sin(angle)));
  SDL_SetRenderDrawColor(r, rastaGold.r, rastaGold.g, rastaGold.b, 255);
  for (int offset = -1; offset <= 1; ++offset)
    SDL_RenderDrawLine(r, cx + static_cast<int>(std::sin(angle) * 17) + offset * dx,
                       cy - static_cast<int>(std::cos(angle) * 17) + offset * dy,
                       cx + static_cast<int>(std::sin(angle) * 31) + offset * dx,
                       cy - static_cast<int>(std::cos(angle) * 31) + offset * dy);
}

void panel(SDL_Renderer* r, int x, int y, int w, int h, bool selected, const Palette& p) {
  rect(r, x, y, w, h, selected ? p.focus : p.edge);
  rect(r, x + 3, y + 3, w - 6, h - 6, p.inset);
}

void centeredText(SDL_Renderer* r, int cx, int y, const std::string& value, SDL_Color color) {
  text(r, cx - static_cast<int>(value.size()) * 6, y, value, color);
}

std::string value(const App& app, int index) {
  char buf[64];
  const auto& p = app.patch;
  const Instrument& voice = p.instruments[app.selectedInstrument];
  switch (index) {
    case 0: std::snprintf(buf, sizeof buf, "%.0f HZ", voice.pitch); break;
    case 1: std::snprintf(buf, sizeof buf, "%.1f HZ", voice.rate); break;
    case 2: std::snprintf(buf, sizeof buf, "%.2f OCT", voice.depth); break;
    case 3: case 4: case 5: case 6: return waves[p.instruments[index - 3].wave];
    case 7: return lfoWaves[voice.lfoWave];
    case 8: return voice.retrig ? "ON" : "OFF";
    case 9: std::snprintf(buf, sizeof buf, "%.0f %%", p.volume * 200); break;
    case 10: return p.echo ? "ON" : "OFF";
    case 11: std::snprintf(buf, sizeof buf, "%.0f MS", p.echoTime * 1000); break;
    case 12: std::snprintf(buf, sizeof buf, "%.0f %%", p.feedback * 100); break;
    case 13: std::snprintf(buf, sizeof buf, "%.0f %%", p.echoMix * 100); break;
    case 14:
      if (app.preset < 4) return factoryNames[app.preset];
      std::snprintf(buf, sizeof buf, "USER %d", app.preset - 3); break;
    case 15: return app.theme == 0 ? "DAY RASTAFARI" : "NIGHT RASTAFARI";
    default: return waves[voice.wave];
  }
  return buf;
}

void draw(App& app) {
  SDL_Renderer* r = app.renderer;
  const Palette p = palette(app.theme);
  SDL_SetRenderDrawColor(r, p.background.r, p.background.g, p.background.b, 255);
  SDL_RenderClear(r);
  rect(r, 8, 8, 624, 464, p.edge);
  rect(r, 12, 12, 616, 456, p.chassis);
  rect(r, 12, 12, 616, 48, p.face);
  text(r, 24, 19, "CHOOCHOO DUB SIREN", p.label.r > 128 ? p.label : SDL_Color{230, 230, 220, 255});
  const SDL_Color headerAccent = rastaGold;
  if (app.touchUi) {
    const char* names[] = {"INST", "COMMON", "STICKS"};
    const int active = app.assignMode ? 2 : app.page;
    for (int i = 0; i < 3; ++i) {
      const int x = 20 + i * 158;
      panel(r, x, 34, 150, 25, i == active, p);
      centeredText(r, x + 75, 38, names[i], p.label);
    }
    text(r, 510, 39, std::string("PART ") + triggerNames[app.selectedInstrument], headerAccent);
  } else {
    text(r, 24, 39, app.assignMode ? "03 STICKS" : (app.page == 0 ? "01 INSTRUMENT" : "02 COMMON"),
         headerAccent);
    text(r, 408, 39, std::string("PART ") + triggerNames[app.selectedInstrument], headerAccent);
  }
  const int peak = app.peak.load(std::memory_order_relaxed);
  for (int i = 0; i < 16; ++i)
    rect(r, 444 + i * 8, 24, 5, 6, peak * 16 / 32768 > i ? headerAccent : p.muted);
  rect(r, 584, 20, 20, 14, app.gateSources ? headerAccent : p.muted);
  rect(r, 12, 60, 205, 4, rastaRed);
  rect(r, 217, 60, 206, 4, rastaGold);
  rect(r, 423, 60, 205, 4, rastaGreen);

  if (app.assignMode) {
    text(r, 34, 79, "AXIS", p.label);
    text(r, 112, 79, "ASSIGN", p.label);
    text(r, 364, 79, "RATE", p.label);
    text(r, 492, 79, "INVERT", p.label);
    for (int i = 0; i < axisCount; ++i) {
      const int y = 105 + i * 58;
      text(r, 34, y + 15, axisNames[i], p.label);
      panel(r, 100, y, 248, 46, app.selectedAxis == i && app.selectedAxisField == 0, p);
      text(r, 112, y + 15, assignmentNames[app.assignments[i]], p.accent);
      panel(r, 352, y, 120, 46, app.selectedAxis == i && app.selectedAxisField == 1, p);
      text(r, 364, y + 15, std::to_string(app.axisRate[i]) + "%", p.accent);
      panel(r, 476, y, 144, 46, app.selectedAxis == i && app.selectedAxisField == 2, p);
      text(r, 492, y + 15, app.axisInvert[i] ? "ON" : "OFF", p.accent);
    }
    centeredText(r, 320, 346, app.touchUi ? "TAP OR DRAG TO EDIT" : "DPAD SELECT  A+DPAD EDIT", p.label);
  } else if (app.page == 0) {
    for (int i = 0; i < triggerCount; ++i) {
      const int x = 20 + i * 151;
      panel(r, x, 69, 145, 57, app.selection == 3 + i, p);
      text(r, x + 12, 79, triggerNames[i], p.label);
      rect(r, x + 116, 79, 14, 14, app.selectedInstrument == i ? p.accent : p.muted);
      centeredText(r, x + 72, 103, value(app, 3 + i), p.label);
    }
    const Instrument& voice = app.patch.instruments[app.selectedInstrument];
    const float values[] = {(voice.pitch - 80.0f) / 2920.0f,
                            (voice.rate - 0.1f) / 29.9f, voice.depth / 3.0f};
    const char* names[] = {"PITCH", "LFO RATE", "LFO DEPTH"};
    for (int i = 0; i < 3; ++i) {
      const int x = 20 + i * 201, cx = x + 97;
      panel(r, x, 137, 195, 153, app.selection == i, p);
      centeredText(r, cx, 151, names[i], p.label);
      dial(r, cx, 218, values[i], p);
      centeredText(r, cx, 265, value(app, i), p.label);
    }
    panel(r, 20, 301, 195, 58, app.selection == 16, p);
    text(r, 32, 311, "WAVEFORM", p.label);
    text(r, 32, 335, value(app, 16), p.accent);
    panel(r, 221, 301, 195, 58, app.selection == 7, p);
    text(r, 233, 311, "LFO SHAPE", p.label);
    text(r, 233, 335, value(app, 7), p.accent);
    panel(r, 422, 301, 195, 58, app.selection == 8, p);
    text(r, 434, 311, "RETRIG", p.label);
    text(r, 434, 335, value(app, 8), p.accent);
  } else {
    panel(r, 20, 80, 146, 202, app.selection == 9, p);
    centeredText(r, 93, 98, "MASTER", p.label);
    centeredText(r, 93, 119, "VOLUME", p.label);
    dial(r, 93, 185, app.patch.volume / 0.5f, p);
    centeredText(r, 93, 237, value(app, 9), p.label);
    rect(r, 176, 70, 444, 212, p.edge);
    rect(r, 179, 73, 438, 206, p.inset);
    centeredText(r, 398, 80, "COMMON DELAY", p.label);
    const int ids[] = {11, 12, 13};
    const char* names[] = {"TIME", "FEEDBACK", "MIX"};
    const float values[] = {(app.patch.echoTime - 0.05f) / 1.45f,
                            app.patch.feedback / 0.85f, app.patch.echoMix / 0.75f};
    for (int i = 0; i < 3; ++i) {
      const int x = 187 + i * 142, cx = x + 68;
      panel(r, x, 103, 136, 130, app.selection == ids[i], p);
      centeredText(r, cx, 115, names[i], p.label);
      dial(r, cx, 172, values[i], p);
      centeredText(r, cx, 213, value(app, ids[i]), p.label);
    }
    panel(r, 187, 238, 420, 32, app.selection == 10, p);
    text(r, 202, 246, "ECHO", p.label);
    text(r, 500, 246, value(app, 10), p.accent);
    panel(r, 20, 293, 368, 66, app.selection == 14, p);
    text(r, 34, 305, "PRESET", p.label);
    text(r, 34, 331, "<", p.accent);
    centeredText(r, 204, 331, value(app, 14), p.accent);
    text(r, 356, 331, ">", p.accent);
    panel(r, 398, 293, 222, 66, app.selection == 15, p);
    text(r, 412, 305, "THEME", p.label);
    text(r, 412, 331, value(app, 15), p.accent);
  }

  text(r, 24, 372, app.status, p.accent);
  if (app.touchUi) {
    for (int i = 0; i < triggerCount; ++i) {
      const int x = 20 + i * 70;
      panel(r, x, 396, 65, 64, app.triggers[i] != 0, p);
      centeredText(r, x + 32, 420, triggerNames[i], p.label);
    }
    const char* names[] = {"PANIC", "EXIT", "LOAD", "SAVE"};
    for (int i = 0; i < 4; ++i) {
      const int x = 305 + (i % 2) * 157, y = 396 + (i / 2) * 34;
      panel(r, x, y, 150, 30, false, p);
      centeredText(r, x + 75, y + 7, names[i], p.label);
    }
  } else if (app.assignMode) {
    text(r, 24, 402, "SELECT PAGE  B/START BACK  SELECT+B PANIC", p.label);
    text(r, 24, 424, "A TOGGLE INVERT", p.label);
    text(r, 24, 446, "L1 L2 R1 R2 PLAY", p.label);
  } else {
    text(r, 24, 396, "DPAD NAV  A+DPAD EDIT  SELECT PAGE", p.label);
    text(r, 24, 418, "L1 L2 R1 R2 PLAY   B PANIC   START STICKS", p.label);
    text(r, 24, 440, "X LOAD  Y SAVE   SELECT+START EXIT", p.label);
  }
  SDL_RenderPresent(r);
}

int nextSelection(int selection, int button) {
  static const int neighbors[parameterCount][4] = {
    {3, 16, 0, 1}, {4, 7, 0, 2}, {6, 8, 1, 2},
    {3, 0, 3, 4}, {4, 1, 3, 5}, {5, 1, 4, 6}, {6, 2, 5, 6},
    {1, 4, 16, 8}, {2, 6, 7, 8}, {9, 14, 9, 11}, {12, 15, 9, 10},
    {11, 10, 9, 12}, {12, 10, 11, 13}, {13, 10, 12, 13}, {9, 14, 14, 15},
    {10, 15, 14, 15}, {0, 3, 16, 7}
  };
  const int direction = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? 0 :
                        button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 :
                        button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? 2 : 3;
  return neighbors[selection][direction];
}

void adjustAxis(App& app, int axis, int field, int step) {
  if (field == 0) {
    int& assignment = app.assignments[axis];
    assignment = (assignment + (step > 0 ? 1 : 3)) % 4;
  } else if (field == 1) {
    int& rate = app.axisRate[axis];
    rate = std::clamp(rate + step * 10, 10, 300);
  } else {
    app.axisInvert[axis] = !app.axisInvert[axis];
  }
  app.status = saveAssignments(app) ? "AXES SAVED" : "ERROR SAVING AXES";
}

void selectScreen(App& app, int screen) {
  if (screen == 2) {
    app.assignMode = true;
  } else {
    app.assignMode = false;
    app.page = screen;
    app.selection = screen == 0 ? 3 + app.selectedInstrument : 9;
  }
}

void saveSelectedPreset(App& app) {
  if (app.preset < 4) app.status = "SELECT A USER SLOT";
  else app.status = savePreset(app, app.preset - 4) ? "PRESET SAVED" : "ERROR SAVING PRESET";
}

void action(App& app, int button, bool down, unsigned source) {
  if (down && button != SDL_CONTROLLER_BUTTON_A && button != SDL_CONTROLLER_BUTTON_BACK &&
      app.heldButtons[SDL_CONTROLLER_BUTTON_A]) app.editUsed = true;
  if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == leftTrigger ||
      button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER || button == rightTrigger) {
    const int index = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? 0 :
                      button == leftTrigger ? 1 : button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? 2 : 3;
    trigger(app, index, source, down);
    return;
  }
  if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return;
  const bool wasDown = app.heldButtons[button] != 0;
  if (down) app.heldButtons[button] |= source;
  else app.heldButtons[button] &= ~source;
  if (wasDown == (app.heldButtons[button] != 0)) return;
  down = app.heldButtons[button] != 0;
  if (button == SDL_CONTROLLER_BUTTON_BACK) {
    app.selectSources = app.heldButtons[button];
    if (down) {
      app.selectUsed = app.heldButtons[SDL_CONTROLLER_BUTTON_A] ||
                       app.heldButtons[SDL_CONTROLLER_BUTTON_B] ||
                       app.heldButtons[SDL_CONTROLLER_BUTTON_START];
      if (app.heldButtons[SDL_CONTROLLER_BUTTON_A]) app.editWithSelect = true;
    } else if (!app.selectUsed) {
      selectScreen(app, app.assignMode ? 0 : (app.page == 0 ? 1 : 2));
    }
    return;
  }
  if (down && app.selectSources) app.selectUsed = true;
  if (button == SDL_CONTROLLER_BUTTON_A) {
    if (down) {
      app.editUsed = app.heldButtons[SDL_CONTROLLER_BUTTON_DPAD_UP] ||
                     app.heldButtons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] ||
                     app.heldButtons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] ||
                     app.heldButtons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
      app.editWithSelect = app.selectSources != 0;
      return;
    }
    if (app.editUsed) return;
    if (app.assignMode) {
      if (app.selectedAxisField == 2) adjustAxis(app, app.selectedAxis, 2, 1);
    }
    else if (app.selection == 14) {
      if (!app.editWithSelect && !app.selectSources) recall(app);
      else saveSelectedPreset(app);
    } else if (app.selection == 8 || app.selection == 10 || app.selection == 15)
      adjust(app, app.selection, 1);
    return;
  }
  if (!down) return;
  if (button == SDL_CONTROLLER_BUTTON_B) {
    if (app.assignMode && !app.selectSources) app.assignMode = false;
    else panic(app);
    return;
  }
  if (button == SDL_CONTROLLER_BUTTON_START) {
    if (app.selectSources) app.running = false;
    else app.assignMode = !app.assignMode;
    return;
  }
  const bool direction = button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
                         button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                         button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
                         button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  if (app.assignMode) {
    if (direction && app.heldButtons[SDL_CONTROLLER_BUTTON_A]) {
      const int step = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
                       button == SDL_CONTROLLER_BUTTON_DPAD_UP ? 1 : -1;
      adjustAxis(app, app.selectedAxis, app.selectedAxisField, step);
    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP && app.selectedAxis > 0) --app.selectedAxis;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && app.selectedAxis < axisCount - 1) ++app.selectedAxis;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && app.selectedAxisField > 0) --app.selectedAxisField;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && app.selectedAxisField < 2) ++app.selectedAxisField;
    return;
  }
  if (direction) {
    if (app.heldButtons[SDL_CONTROLLER_BUTTON_A]) {
      const int magnitude = button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
                            button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 10 : 1;
      adjust(app, app.selection,
             (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button == SDL_CONTROLLER_BUTTON_DPAD_UP) ?
             magnitude : -magnitude);
    } else {
      app.selection = nextSelection(app.selection, button);
      if (app.page == 0 && app.selection >= 3 && app.selection <= 6)
        app.selectedInstrument = app.selection - 3;
    }
    return;
  }
  switch (button) {
    case SDL_CONTROLLER_BUTTON_X: recall(app); break;
    case SDL_CONTROLLER_BUTTON_Y: saveSelectedPreset(app); break;
  }
}

bool inside(float x, float y, int left, int top, int w, int h) {
  return x >= left && x < left + w && y >= top && y < top + h;
}

int pointerIndex(const App& app, bool mouse, SDL_TouchID device, SDL_FingerID finger) {
  for (int i = 0; i < static_cast<int>(app.pointers.size()); ++i) {
    const Pointer& pointer = app.pointers[i];
    if (pointer.active && pointer.mouse == mouse &&
        (mouse || (pointer.device == device && pointer.finger == finger))) return i;
  }
  return -1;
}

void pointerDown(App& app, bool mouse, SDL_TouchID device, SDL_FingerID finger, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || pointerIndex(app, mouse, device, finger) >= 0) return;
  int slot = 0;
  while (slot < static_cast<int>(app.pointers.size()) && app.pointers[slot].active) ++slot;
  if (slot == static_cast<int>(app.pointers.size())) { app.status = "TOO MANY TOUCHES"; return; }
  Pointer& pointer = app.pointers[slot];
  pointer = Pointer{};
  pointer.active = true;
  pointer.mouse = mouse;
  pointer.device = device;
  pointer.finger = finger;
  pointer.lastY = y;
  const unsigned source = 1u << (slot + 2);

  if (app.touchUi && inside(x, y, 20, 34, 466, 25)) {
    const int screen = static_cast<int>((x - 20) / 158);
    if (screen < 3 && inside(x, y, 20 + screen * 158, 34, 150, 25) &&
        screen != (app.assignMode ? 2 : app.page)) selectScreen(app, screen);
    return;
  } else if (app.touchUi && inside(x, y, 20, 396, 275, 64)) {
    const int index = static_cast<int>((x - 20) / 70);
    if (index < triggerCount && inside(x, y, 20 + index * 70, 396, 65, 64)) {
      pointer.trigger = index;
      trigger(app, index, source, true);
    }
    return;
  } else if (app.touchUi && inside(x, y, 305, 396, 307, 64)) {
    const int col = static_cast<int>((x - 305) / 157);
    const int row = static_cast<int>((y - 396) / 34);
    if (col < 2 && row < 2 && inside(x, y, 305 + col * 157, 396 + row * 34, 150, 30)) {
      if (row == 0 && col == 0) panic(app);
      else if (row == 0) app.running = false;
      else if (col == 0) recall(app);
      else saveSelectedPreset(app);
    }
    return;
  } else if (!app.touchUi && inside(x, y, 24, 32, 220, 28)) {
    action(app, SDL_CONTROLLER_BUTTON_BACK, true, source);
    action(app, SDL_CONTROLLER_BUTTON_BACK, false, source);
  } else if (!app.touchUi && inside(x, y, 24, app.assignMode ? 442 : 414, 144, 24)) {
    pointer.trigger = static_cast<int>((x - 24) / 36);
    trigger(app, pointer.trigger, source, true);
    return;
  } else if (app.assignMode) {
    for (int i = 0; i < axisCount; ++i) {
      const int top = 105 + i * 58;
      const int field = inside(x, y, 100, top, 248, 46) ? 0 :
                        inside(x, y, 352, top, 120, 46) ? 1 :
                        inside(x, y, 476, top, 144, 46) ? 2 : -1;
      if (field >= 0) {
        app.selectedAxis = i;
        app.selectedAxisField = field;
        if (field == 1) { pointer.axis = i; pointer.field = field; }
        else adjustAxis(app, i, field, 1);
        return;
      }
    }
  } else if (app.page == 0) {
    for (int i = 0; i < triggerCount; ++i) {
      if (inside(x, y, 20 + i * 151, 69, 145, 57)) {
        pointer.trigger = i;
        trigger(app, i, source, true);
        return;
      }
    }
    for (int i = 0; i < 3; ++i) {
      if (inside(x, y, 20 + i * 201, 137, 195, 153)) {
        app.selection = i;
        pointer.control = i;
        return;
      }
    }
    const int control = inside(x, y, 20, 301, 195, 58) ? 16 :
                        inside(x, y, 221, 301, 195, 58) ? 7 :
                        inside(x, y, 422, 301, 195, 58) ? 8 : -1;
    if (control >= 0) { app.selection = control; adjust(app, control, 1); }
  } else {
    if (inside(x, y, 20, 80, 146, 202)) { app.selection = pointer.control = 9; return; }
    for (int i = 0; i < 3; ++i) {
      if (inside(x, y, 187 + i * 142, 103, 136, 130)) {
        app.selection = pointer.control = 11 + i;
        return;
      }
    }
    if (inside(x, y, 187, 238, 420, 32)) { app.selection = 10; adjust(app, 10, 1); }
    else if (inside(x, y, 20, 293, 368, 66)) {
      app.selection = 14;
      if (x < 100) adjust(app, 14, -1);
      else if (x >= 320) adjust(app, 14, 1);
      else recall(app);
    } else if (inside(x, y, 398, 293, 222, 66)) {
      app.selection = 15;
      adjust(app, 15, 1);
    }
  }
  if (!app.touchUi && !app.assignMode && inside(x, y, 240, 418, 120, 20)) panic(app);
  else if (!app.touchUi && !app.assignMode && inside(x, y, 24, 440, 84, 20)) recall(app);
  else if (!app.touchUi && !app.assignMode && inside(x, y, 120, 440, 90, 20))
    saveSelectedPreset(app);
}

void pointerMove(App& app, bool mouse, SDL_TouchID device, SDL_FingerID finger, float y) {
  const int slot = pointerIndex(app, mouse, device, finger);
  if (slot < 0 || !std::isfinite(y)) return;
  Pointer& pointer = app.pointers[slot];
  const float delta = pointer.lastY - y;
  pointer.lastY = y;
  if (pointer.control >= 0) {
    const float scales[] = {2920.0f / 30.0f, 29.9f / 0.2f, 3.0f / 0.05f};
    float steps = 0.0f;
    if (pointer.control <= 2) steps = scales[pointer.control];
    else if (pointer.control == 9) steps = 0.5f / 0.01f;
    else if (pointer.control == 11) steps = 1.45f / 0.01f;
    else if (pointer.control == 12) steps = 0.85f / 0.01f;
    else if (pointer.control == 13) steps = 0.75f / 0.01f;
    if (delta != 0.0f) adjust(app, pointer.control, delta * steps / 200.0f);
  } else if (pointer.field == 1) {
    pointer.remainder += delta / 10.0f;
    const int steps = static_cast<int>(pointer.remainder);
    if (steps) {
      adjustAxis(app, pointer.axis, pointer.field, steps);
      pointer.remainder -= steps;
    }
  }
}

void pointerUp(App& app, bool mouse, SDL_TouchID device, SDL_FingerID finger) {
  const int slot = pointerIndex(app, mouse, device, finger);
  if (slot < 0) return;
  const int held = app.pointers[slot].trigger;
  app.pointers[slot] = Pointer{};
  if (held >= 0) trigger(app, held, 1u << (slot + 2), false);
}

int keyButton(SDL_Keycode key) {
  switch (key) {
    case SDLK_x: return SDL_CONTROLLER_BUTTON_A;
    case SDLK_z: return SDL_CONTROLLER_BUTTON_B;
    case SDLK_s: return SDL_CONTROLLER_BUTTON_X;
    case SDLK_a: return SDL_CONTROLLER_BUTTON_Y;
    case SDLK_SPACE: return SDL_CONTROLLER_BUTTON_START;
    case SDLK_LSHIFT: return SDL_CONTROLLER_BUTTON_BACK;
    case SDLK_q: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case SDLK_w: return leftTrigger;
    case SDLK_r: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case SDLK_e: return rightTrigger;
    case SDLK_UP: return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case SDLK_DOWN: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case SDLK_LEFT: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case SDLK_RIGHT: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    default: return -1;
  }
}

void openController(App& app) {
  if (app.controller) return;
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i) && (app.controller = SDL_GameControllerOpen(i))) return;
}

void applyAxis(App& app, int axis, float value, float dt) {
  const int targets[] = {-1, 0, 1, 2};
  if (std::fabs(value) > 0.2f && app.assignments[axis])
    adjust(app, targets[app.assignments[axis]],
           value * dt * 30.0f * app.axisRate[axis] / 100.0f * (app.axisInvert[axis] ? -1.0f : 1.0f));
}

void axes(App& app, float dt) {
  if (!app.controller || app.assignMode) return;
  for (int i = 0; i < axisCount; ++i) {
    const int raw = SDL_GameControllerGetAxis(app.controller, static_cast<SDL_GameControllerAxis>(i));
    applyAxis(app, i, raw / 32768.0f, dt);
  }
}
}

int main(int argc, char** argv) {
  const bool smoke = argc == 3 && std::strcmp(argv[1], "--smoke") == 0;
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return 1;
  }
  App app;
  char* pref = SDL_GetPrefPath("ChooChoo", "DubSiren");
  if (!pref) { std::fprintf(stderr, "Cannot open settings folder: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
  app.folder = pref;
  SDL_free(pref);
  loadAssignments(app);
  loadTheme(app);
#ifdef __ANDROID__
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
  app.window = SDL_CreateWindow("ChooChoo Dub Siren", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, (app.touchUi || std::getenv("DUBSIREN_HANDHELD")) ?
                                SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
  if (app.window) app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (app.window && !app.renderer) app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
  if (!app.renderer) { std::fprintf(stderr, "Video initialization failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
  SDL_RenderSetLogicalSize(app.renderer, width, height);
  SDL_AudioSpec desired{}, obtained{};
  desired.freq = 48000; desired.format = AUDIO_S16SYS; desired.channels = 2;
  desired.samples = 512; desired.callback = audioCallback; desired.userdata = &app;
  app.audio = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  const char* audioFallback = std::getenv("DUBSIREN_AUDIODEV_FALLBACK");
  if (!app.audio && audioFallback && *audioFallback && !std::getenv("AUDIODEV")) {
    std::fprintf(stderr, "Default audio unavailable (%s); retrying %s\n", SDL_GetError(), audioFallback);
    if (SDL_setenv("AUDIODEV", audioFallback, 1) == 0)
      app.audio = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  }
  if (!app.audio) { std::fprintf(stderr, "Audio initialization failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
  updateAudio(app);
  openController(app);
  SDL_PauseAudioDevice(app.audio, 0);
  Uint32 last = SDL_GetTicks();
  int smokeFrame = 0;
  while (app.running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) app.running = false;
      else if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
               event.type == SDL_FINGERUP) {
        const SDL_TouchFingerEvent& finger = event.tfinger;
        if (finger.touchId == SDL_MOUSE_TOUCHID) continue;
        if (event.type == SDL_FINGERDOWN)
          pointerDown(app, false, finger.touchId, finger.fingerId, finger.x * width, finger.y * height);
        else if (event.type == SDL_FINGERMOTION)
          pointerMove(app, false, finger.touchId, finger.fingerId, finger.y * height);
        else pointerUp(app, false, finger.touchId, finger.fingerId);
      } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT) continue;
        if (event.type == SDL_MOUSEBUTTONDOWN)
          pointerDown(app, true, 0, 0, static_cast<float>(event.button.x), static_cast<float>(event.button.y));
        else pointerUp(app, true, 0, 0);
      } else if (event.type == SDL_MOUSEMOTION) {
        if (event.motion.which != SDL_TOUCH_MOUSEID)
          pointerMove(app, true, 0, 0, static_cast<float>(event.motion.y));
      }
      else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.key.repeat) continue;
#ifndef _WIN32
        if (std::getenv("DUBSIREN_NEXTUI") && event.key.keysym.scancode == SDL_SCANCODE_POWER) {
          if (event.type == SDL_KEYDOWN) {
            app.powerHeld = true;
            app.powerPressed = SDL_GetTicks();
          } else {
            if (app.powerHeld && SDL_GetTicks() - app.powerPressed < 1000) suspendNextUI(app);
            app.powerHeld = false;
          }
          continue;
        }
#endif
        if (event.key.keysym.sym == SDLK_ESCAPE && event.type == SDL_KEYDOWN) app.running = false;
        const int button = keyButton(event.key.keysym.sym);
        if (button >= 0) action(app, button, event.type == SDL_KEYDOWN, 1);
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
        if (app.controller && event.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app.controller)))
          action(app, event.cbutton.button, event.type == SDL_CONTROLLERBUTTONDOWN, 2);
      } else if (event.type == SDL_CONTROLLERAXISMOTION && app.controller &&
                 event.caxis.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app.controller)) &&
                 (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        const int index = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 1 : 3;
        triggerAxis(app, index, event.caxis.value);
      } else if (event.type == SDL_CONTROLLERDEVICEADDED) openController(app);
      else if (event.type == SDL_CONTROLLERDEVICEREMOVED && app.controller &&
               event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app.controller))) {
        for (int i = 0; i < triggerCount; ++i) trigger(app, i, 2, false);
        for (auto& held : app.heldButtons) held &= ~2u;
        app.selectSources &= ~2u;
        SDL_GameControllerClose(app.controller);
        app.controller = nullptr;
        openController(app);
      } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        panic(app);
        app.triggerHeld = 0;
        app.triggerBlocked = false;
        app.triggers.fill(0);
        app.heldButtons.fill(0);
        app.pointers.fill(Pointer{});
        app.selectSources = 0;
      }
    }
    const Uint32 now = SDL_GetTicks();
    axes(app, std::min((now - last) / 1000.0f, 0.1f));
    last = now;
    if (smoke && smokeFrame == 2) trigger(app, 0, 1, true);
    if (smoke && smokeFrame == 15) trigger(app, 0, 1, false);
    draw(app);
    if (smoke && smokeFrame == 10) {
      SDL_Surface* image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
      if (!image || SDL_RenderReadPixels(app.renderer, nullptr, image->format->format, image->pixels, image->pitch) != 0 ||
          SDL_SaveBMP(image, argv[2]) != 0) {
        std::fprintf(stderr, "Smoke capture failed: %s\n", SDL_GetError());
        if (image) SDL_FreeSurface(image);
        SDL_CloseAudioDevice(app.audio);
        SDL_Quit();
        return 1;
      }
      SDL_FreeSurface(image);
    }
    if (smoke && ++smokeFrame == 25) app.running = false;
    SDL_Delay(8);
  }
  SDL_CloseAudioDevice(app.audio);
  if (app.controller) SDL_GameControllerClose(app.controller);
  SDL_DestroyRenderer(app.renderer);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
  return 0;
}
