#define SDL_MAIN_HANDLED
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
constexpr int parameterCount = 15, axisCount = 4, triggerCount = 4;
constexpr int leftTrigger = SDL_CONTROLLER_BUTTON_MAX;
constexpr int rightTrigger = SDL_CONTROLLER_BUTTON_MAX + 1;
const char* waves[] = {"SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN", "PULSE", "LOFI TRI"};
const char* lfoWaves[] = {"SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN"};
const char* axisNames[] = {"LX", "LY", "RX", "RY"};
const char* assignmentNames[] = {"OFF", "PITCH", "LFO RATE", "LFO DEPTH", "VOLUME", "ECHO TIME", "FEEDBACK", "ECHO MIX"};

struct App {
  Siren siren{48000};
  SirenPatch patch;
  SDL_AudioDeviceID audio = 0;
  SDL_GameController* controller = nullptr;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  std::string folder;
  std::string status = "READY";
  std::array<int, axisCount> assignments{{1, 2, 3, 5}};
  std::array<unsigned, SDL_CONTROLLER_BUTTON_MAX> heldButtons{};
  std::array<unsigned, triggerCount> triggers{};
  std::array<uint64_t, triggerCount> triggerOrder{};
  uint64_t nextTriggerOrder = 0;
  int activeTrigger = -1;
  std::atomic<int> peak{0};
  int selection = 0;
  int preset = 0;
  unsigned gateSources = 0;
  unsigned triggerHeld = 0;
  bool triggerBlocked = false;
  bool running = true;
  bool assignMode = false;
  bool editUsed = false;
  bool editWithSelect = false;
  unsigned selectSources = 0;
  bool powerHeld = false;
  Uint32 powerPressed = 0;
  int selectedAxis = 0;
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
  SirenPatch sounding = app.patch;
  if (app.activeTrigger >= 0) sounding.wave = app.patch.triggerWaves[app.activeTrigger];
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
  if (!wasDown && isDown) app.triggerOrder[index] = ++app.nextTriggerOrder;
  app.triggerHeld = 0;
  for (unsigned held : app.triggers) app.triggerHeld |= held;
  if (app.triggerBlocked) {
    if (!app.triggerHeld) app.triggerBlocked = false;
    return;
  }
  app.activeTrigger = -1;
  for (int i = 0; i < triggerCount; ++i)
    if (app.triggers[i] && (app.activeTrigger < 0 ||
        app.triggerOrder[i] > app.triggerOrder[app.activeTrigger])) app.activeTrigger = i;
  app.gateSources = app.activeTrigger >= 0 ? 1 : 0;
  SirenPatch sounding = app.patch;
  if (app.activeTrigger >= 0) sounding.wave = app.patch.triggerWaves[app.activeTrigger];
  SDL_LockAudioDevice(app.audio);
  app.siren.setPatch(sounding);
  app.siren.setGate(app.gateSources != 0);
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
  s << "DUBSIREN2\n" << p.pitch << ' ' << p.rate << ' ' << p.depth << ' '
    << p.volume << ' ' << p.echoTime << ' ' << p.feedback << ' ' << p.echoMix << ' '
    << p.wave << ' ' << p.lfoWave << ' ' << p.retrig << ' ' << p.echo;
  for (int wave : p.triggerWaves) s << ' ' << wave;
  s << '\n';
  return writeFile(presetPath(app, slot), s.str());
}

bool loadPreset(const App& app, int slot, SirenPatch& result) {
  std::ifstream in(std::filesystem::u8path(presetPath(app, slot)), std::ios::binary);
  std::string header, extra;
  int retrig, echo;
  SirenPatch p;
  if (!(in >> header >> p.pitch >> p.rate >> p.depth >> p.volume >> p.echoTime >>
        p.feedback >> p.echoMix >> p.wave >> p.lfoWave >> retrig >> echo) ||
      (header != "DUBSIREN1" && header != "DUBSIREN2") || (retrig != 0 && retrig != 1) ||
      (echo != 0 && echo != 1)) return false;
  if (header == "DUBSIREN1") p.triggerWaves[2] = p.wave;
  else for (int& wave : p.triggerWaves) if (!(in >> wave)) return false;
  if (in >> extra) return false;
  p.retrig = retrig != 0;
  p.echo = echo != 0;
  if (!Siren::valid(p)) return false;
  result = p;
  return true;
}

bool saveAssignments(const App& app) {
  std::ostringstream s;
  s << "DUBAXIS1";
  for (int assignment : app.assignments) s << ' ' << assignment;
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
  std::array<int, axisCount> assignments;
  if (!(in >> header >> assignments[0] >> assignments[1] >> assignments[2] >> assignments[3]) ||
      header != "DUBAXIS1" || (in >> extra) ||
      std::any_of(assignments.begin(), assignments.end(), [](int n) { return n < 0 || n > 7; })) {
    app.status = "INVALID AXIS FILE";
    return;
  }
  app.assignments = assignments;
}

SirenPatch factoryPreset(int index) {
  SirenPatch p;
  switch (index) {
    case 1: p.pitch = 900; p.rate = 7; p.depth = 1.8f; p.lfoWave = 4; p.echo = true; break;
    case 2: p.pitch = 450; p.rate = 12; p.depth = 0.8f; p.lfoWave = 2; p.echo = true; break;
    case 3: p.pitch = 500; p.triggerWaves[2] = 6; p.rate = 3; p.depth = 1.2f; p.echo = true; break;
  }
  return p;
}

void recall(App& app) {
  if (app.triggerHeld) {
    app.status = "RELEASE TRIGGER FIRST";
    return;
  }
  SirenPatch loaded;
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
  SirenPatch& p = app.patch;
  switch (index) {
    case 0: p.pitch = std::clamp(p.pitch + direction * 30.0f, 80.0f, 3000.0f); break;
    case 1: p.rate = std::clamp(p.rate + direction * 0.2f, 0.1f, 30.0f); break;
    case 2: p.depth = std::clamp(p.depth + direction * 0.05f, 0.0f, 3.0f); break;
    case 3: case 4: case 5: case 6:
      p.triggerWaves[index - 3] = (p.triggerWaves[index - 3] + (direction > 0 ? 1 : 6)) % 7; break;
    case 7: p.lfoWave = (p.lfoWave + (direction > 0 ? 1 : 4)) % 5; break;
    case 8: p.retrig = !p.retrig; break;
    case 9: p.volume = std::clamp(p.volume + direction * 0.01f, 0.0f, 0.5f); break;
    case 10: p.echo = !p.echo; break;
    case 11: p.echoTime = std::clamp(p.echoTime + direction * 0.01f, 0.05f, 1.5f); break;
    case 12: p.feedback = std::clamp(p.feedback + direction * 0.01f, 0.0f, 0.85f); break;
    case 13: p.echoMix = std::clamp(p.echoMix + direction * 0.01f, 0.0f, 0.75f); break;
    case 14: app.preset = (app.preset + (direction > 0 ? 1 : 7)) % 8; break;
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

void dial(SDL_Renderer* r, int cx, int cy, float value, SDL_Color light) {
  const SDL_Color dim{static_cast<Uint8>(light.r / 3), static_cast<Uint8>(light.g / 3),
                      static_cast<Uint8>(light.b / 3), 255};
  const SDL_Color edge{100, 91, 67, 255}, face{63, 64, 51, 255};
  value = std::clamp(value, 0.0f, 1.0f);
  for (int i = 0; i < 9; ++i) {
    const float angle = 2.35f + i * (4.72f / 8.0f);
    const int x = cx + static_cast<int>(std::sin(angle) * 27.0f);
    const int y = cy - static_cast<int>(std::cos(angle) * 27.0f);
    rect(r, x - 2, y - 2, 4, 4, i <= value * 8 ? light : dim);
  }
  for (int y = -20; y <= 20; y += 4) {
    for (int x = -20; x <= 20; x += 4) {
      const int distance = x * x + y * y;
      if (distance <= 20 * 20)
        rect(r, cx + x, cy + y, 4, 4, distance > 15 * 15 ? edge : face);
    }
  }
  const float angle = 2.35f + value * 4.72f;
  for (int distance = 6; distance < 16; distance += 4)
    rect(r, cx + static_cast<int>(std::sin(angle) * distance) - 2,
         cy - static_cast<int>(std::cos(angle) * distance) - 2, 4, 4, light);
}

void panel(SDL_Renderer* r, int x, int y, int w, int h, bool selected) {
  rect(r, x, y, w, h, selected ? SDL_Color{213, 171, 43, 255} : SDL_Color{81, 77, 60, 255});
  rect(r, x + 3, y + 3, w - 6, h - 6, {32, 38, 34, 255});
  rect(r, x + 7, y + 7, 3, 3, {143, 125, 68, 255});
  rect(r, x + w - 10, y + 7, 3, 3, {143, 125, 68, 255});
  for (int i = 0; i < 4 && 15 + i * 6 < h - 5; ++i)
    rect(r, x + w - 9, y + 15 + i * 6, 3, 3,
         i == 0 && selected ? SDL_Color{213, 171, 43, 255} : SDL_Color{50, 83, 55, 255});
}

void centeredText(SDL_Renderer* r, int cx, int y, const std::string& value, SDL_Color color) {
  text(r, cx - static_cast<int>(value.size()) * 6, y, value, color);
}

std::string value(const App& app, int index) {
  char buf[64];
  const auto& p = app.patch;
  switch (index) {
    case 0: std::snprintf(buf, sizeof buf, "%.0f HZ", p.pitch); break;
    case 1: std::snprintf(buf, sizeof buf, "%.1f HZ", p.rate); break;
    case 2: std::snprintf(buf, sizeof buf, "%.2f OCT", p.depth); break;
    case 3: case 4: case 5: case 6: return waves[p.triggerWaves[index - 3]];
    case 7: return lfoWaves[p.lfoWave];
    case 8: return p.retrig ? "ON" : "OFF";
    case 9: std::snprintf(buf, sizeof buf, "%.0f %%", p.volume * 200); break;
    case 10: return p.echo ? "ON" : "OFF";
    case 11: std::snprintf(buf, sizeof buf, "%.0f MS", p.echoTime * 1000); break;
    case 12: std::snprintf(buf, sizeof buf, "%.0f %%", p.feedback * 100); break;
    case 13: std::snprintf(buf, sizeof buf, "%.0f %%", p.echoMix * 100); break;
    default: std::snprintf(buf, sizeof buf, "%s %d", app.preset < 4 ? "FACTORY" : "USER", app.preset % 4 + 1); break;
  }
  return buf;
}

void draw(App& app) {
  SDL_Renderer* r = app.renderer;
  const SDL_Color cream{230, 219, 171, 255}, green{54, 138, 82, 255},
      gold{213, 171, 43, 255}, red{168, 60, 53, 255};
  SDL_SetRenderDrawColor(r, 18, 24, 21, 255);
  SDL_RenderClear(r);
  rect(r, 0, 0, width, 8, red);
  rect(r, 0, 8, width, 5, gold);
  rect(r, 0, 13, width, 5, green);
  text(r, 18, 24, "CHOOCHOO DUB SIREN", gold);
  text(r, 18, 44, app.gateSources ? "SIGNAL ON" : "SIGNAL READY", app.gateSources ? red : green);
  const SDL_Color indicatorColors[] = {green, green, gold, green, red, gold};
  const unsigned blink = SDL_GetTicks() / 250;
  for (int i = 0; i < 24; ++i) {
    const SDL_Color c = indicatorColors[(i + blink / 4) % 6];
    rect(r, 284 + i * 8, 48, 4, 4, (i + blink) % 5 == 0 ?
         SDL_Color{static_cast<Uint8>(c.r / 3), static_cast<Uint8>(c.g / 3), static_cast<Uint8>(c.b / 3), 255} : c);
  }
  if (app.assignMode) {
    centeredText(r, 320, 76, "STICK CONTROL MATRIX", gold);
    for (int i = 0; i < axisCount; ++i) {
      const int x = 24 + (i % 2) * 302, y = 110 + (i / 2) * 120;
      panel(r, x, y, 290, 104, app.selectedAxis == i);
      text(r, x + 20, y + 20, axisNames[i], cream);
      text(r, x + 20, y + 53, assignmentNames[app.assignments[i]], gold);
      for (int n = 0; n < 8; ++n)
        rect(r, x + 206 + n * 8, y + 74, 4, 5, n == app.assignments[i] ? green : SDL_Color{53, 65, 52, 255});
    }
    centeredText(r, 320, 363, "DPAD SELECT  A+DPAD ASSIGN", cream);
    text(r, 24, 396, app.status, gold);
    text(r, 24, 420, "A/B/START BACK   SELECT+B PANIC", cream);
    text(r, 24, 444, "L1 L2 R1 R2 PLAY", cream);
  } else {
    const int topParameters[] = {0, 1, 2, 9};
    const char* topNames[] = {"PITCH", "LFO RATE", "DEPTH", "VOLUME"};
    const float topValues[] = {(app.patch.pitch - 80.0f) / 2920.0f,
                               (app.patch.rate - 0.1f) / 29.9f,
                               app.patch.depth / 3.0f, app.patch.volume / 0.5f};
    const SDL_Color colors[] = {red, gold, green, gold};
    for (int i = 0; i < 4; ++i) {
      const int x = 14 + i * 154;
      panel(r, x, 65, 150, 104, app.selection == topParameters[i]);
      centeredText(r, x + 75, 71, topNames[i], cream);
      dial(r, x + 75, 117, topValues[i], colors[i]);
      centeredText(r, x + 75, 150, value(app, topParameters[i]), gold);
    }
    const int echoParameters[] = {11, 12, 13};
    const char* echoNames[] = {"ECHO TIME", "FEEDBACK", "ECHO MIX"};
    const float echoValues[] = {(app.patch.echoTime - 0.05f) / 1.45f,
                                app.patch.feedback / 0.85f, app.patch.echoMix / 0.75f};
    for (int i = 0; i < 3; ++i) {
      const int x = 14 + i * 154;
      panel(r, x, 178, 150, 104, app.selection == echoParameters[i]);
      centeredText(r, x + 75, 184, echoNames[i], cream);
      dial(r, x + 75, 230, echoValues[i], colors[i]);
      centeredText(r, x + 75, 263, value(app, echoParameters[i]), gold);
    }
    const int switchParameters[] = {7, 8, 10};
    const char* switchNames[] = {"LFO", "RETRIG", "ECHO"};
    for (int i = 0; i < 3; ++i) {
      const int y = 178 + i * 35;
      panel(r, 476, y, 150, 34, app.selection == switchParameters[i]);
      text(r, 482, y + 10, switchNames[i], cream);
      text(r, i == 0 ? 526 : 572, y + 10, value(app, switchParameters[i]), gold);
    }
    const char* triggerNames[] = {"L1", "L2", "R1", "R2"};
    const SDL_Color triggerColors[] = {red, gold, green, red};
    for (int i = 0; i < triggerCount; ++i) {
      const int x = 14 + i * 154;
      panel(r, x, 291, 150, 62, app.selection == 3 + i);
      text(r, x + 14, 299, triggerNames[i], cream);
      rect(r, x + 101, 295, 34, 24, {17, 20, 18, 255});
      rect(r, x + 104, 298, 28, 18, app.gateSources && app.activeTrigger == i ?
           triggerColors[i] : SDL_Color{76, 69, 48, 255});
      centeredText(r, x + 75, 328, value(app, 3 + i), gold);
    }
    panel(r, 14, 362, 380, 30, app.selection == 14);
    text(r, 27, 370, "PRESET", cream);
    text(r, 135, 370, value(app, 14), gold);
    text(r, 414, 370, "OUT", cream);
    const int peak = app.peak.load(std::memory_order_relaxed);
    for (int i = 0; i < 20; ++i)
      rect(r, 465 + i * 7, 375, 5, 7, peak * 20 / 32768 > i ?
           (i >= 16 ? red : i >= 11 ? gold : green) : SDL_Color{45, 50, 42, 255});
    text(r, 24, 396, app.status, gold);
    text(r, 24, 416, "L1 L2 R1 R2 PLAY   B PANIC", cream);
    text(r, 24, 436, "A/X LOAD  Y SAVE   START STICKS", cream);
    text(r, 24, 456, "DPAD SELECT  A+DPAD EDIT  SEL+START EXIT", cream);
  }
  SDL_RenderPresent(r);
}

int nextSelection(int selection, int button) {
  // Up, down, left, right follow the visible panels, including the tall switches.
  static const int neighbors[parameterCount][4] = {
    {0, 11, 0, 1}, {1, 12, 0, 2}, {2, 13, 1, 9},
    {11, 14, 3, 4}, {12, 14, 3, 5}, {13, 14, 4, 6}, {10, 14, 5, 6},
    {9, 8, 13, 7}, {7, 10, 13, 8}, {9, 7, 2, 9}, {8, 6, 13, 10},
    {0, 3, 11, 12}, {1, 4, 11, 13}, {2, 5, 12, 8}, {4, 14, 14, 14}
  };
  const int direction = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? 0 :
                        button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 :
                        button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? 2 : 3;
  return neighbors[selection][direction];
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
    return;
  }
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
    if (app.assignMode) app.assignMode = false;
    else if (app.selection == 14) {
      if (!app.editWithSelect && !app.selectSources) recall(app);
      else if (app.preset < 4) app.status = "SELECT A USER SLOT";
      else app.status = savePreset(app, app.preset - 4) ? "PRESET SAVED" : "ERROR SAVING PRESET";
    } else if (app.selection == 8 || app.selection == 10) adjust(app, app.selection, 1);
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
      int& n = app.assignments[app.selectedAxis];
      n = (n + ((button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
                 button == SDL_CONTROLLER_BUTTON_DPAD_UP) ? 1 : 7)) % 8;
      app.status = saveAssignments(app) ? "AXES SAVED" : "ERROR SAVING AXES";
    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP && app.selectedAxis >= 2) app.selectedAxis -= 2;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && app.selectedAxis < 2) app.selectedAxis += 2;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && app.selectedAxis % 2) --app.selectedAxis;
    else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && app.selectedAxis % 2 == 0) ++app.selectedAxis;
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
    }
    return;
  }
  switch (button) {
    case SDL_CONTROLLER_BUTTON_X: recall(app); break;
    case SDL_CONTROLLER_BUTTON_Y:
      if (app.preset < 4) app.status = "SELECT A USER SLOT";
      else app.status = savePreset(app, app.preset - 4) ? "PRESET SAVED" : "ERROR SAVING PRESET";
      break;
  }
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

void axes(App& app, float dt) {
  if (!app.controller || app.assignMode) return;
  const int targets[] = {-1, 0, 1, 2, 9, 11, 12, 13};
  for (int i = 0; i < axisCount; ++i) {
    const int raw = SDL_GameControllerGetAxis(app.controller, static_cast<SDL_GameControllerAxis>(i));
    const float v = raw / 32768.0f;
    if (std::fabs(v) > 0.2f && app.assignments[i])
      adjust(app, targets[app.assignments[i]], v * dt * 30.0f);
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
  app.window = SDL_CreateWindow("ChooChoo Dub Siren", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, std::getenv("DUBSIREN_HANDHELD") ?
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
  openController(app);
  SDL_PauseAudioDevice(app.audio, 0);
  Uint32 last = SDL_GetTicks();
  int smokeFrame = 0;
  while (app.running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) app.running = false;
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
