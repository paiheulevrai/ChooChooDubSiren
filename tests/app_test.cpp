#define main dubsiren_main
#include "../src/main.cpp"
#undef main

#include <cassert>

static bool sameInstrument(const Instrument& a, const Instrument& b) {
  return a.pitch == b.pitch && a.rate == b.rate && a.depth == b.depth &&
         a.wave == b.wave && a.lfoWave == b.lfoWave && a.retrig == b.retrig;
}

static bool sameSound(const Sound& a, const Sound& b) {
  if (a.volume != b.volume || a.echoTime != b.echoTime || a.feedback != b.feedback ||
      a.echoMix != b.echoMix || a.echo != b.echo) return false;
  for (int i = 0; i < triggerCount; ++i)
    if (!sameInstrument(a.instruments[i], b.instruments[i])) return false;
  return true;
}

static void press(App& app, int button, unsigned source = 1) {
  action(app, button, true, source);
  action(app, button, false, source);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string folder = std::string(argv[1]) + "/";
  std::filesystem::create_directories(std::filesystem::u8path(folder));
  SDL_SetMainReady();
  assert(SDL_Init(SDL_INIT_AUDIO) == 0);
  App app;
  app.folder = folder;
  SDL_AudioSpec desired{};
  desired.freq = 48000;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 512;
  desired.callback = audioCallback;
  desired.userdata = &app;
  app.audio = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
  assert(app.audio);

  std::vector<std::array<int16_t, 1024>> factoryAudio;
  for (int i = 0; i < 4; ++i) {
    const Sound preset = factoryPreset(i);
    assert(validSound(preset));
    for (int j = 0; j < i; ++j) assert(!sameSound(preset, factoryPreset(j)));
    for (int voice = 0; voice < triggerCount; ++voice)
      for (int other = 0; other < voice; ++other)
        assert(!sameInstrument(preset.instruments[voice], preset.instruments[other]));
    for (int voice = 0; voice < triggerCount; ++voice) {
      Siren siren(48000);
      assert(siren.setPatch(voicePatch(preset, voice)));
      siren.setGate(true);
      std::array<int16_t, 1024> audio{};
      siren.render(audio.data(), 512);
      for (const auto& previous : factoryAudio) assert(audio != previous);
      factoryAudio.push_back(audio);
    }
    app.preset = i;
    assert(value(app, 14) == factoryNames[i]);
  }

  app.patch = factoryPreset(3);
  const Sound saved = app.patch;
  assert(savePreset(app, 0));
  app.patch = factoryPreset(1);
  assert(savePreset(app, 0));
  Sound loaded;
  assert(loadPreset(app, 0, loaded) && sameSound(loaded, app.patch));
  app.patch = saved;
  assert(savePreset(app, 0));
  assert(loadPreset(app, 0, loaded) && sameSound(loaded, saved));
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::app);
    out << "garbage";
  }
  assert(!loadPreset(app, 0, loaded));
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::trunc);
    out << "DUBSIREN1\n700 4 1 0.25 0.35 0.45 0.35 6 0 1 0\n";
  }
  assert(loadPreset(app, 0, loaded));
  const int oldWaves[] = {2, 1, 6, 5};
  for (int i = 0; i < triggerCount; ++i) {
    assert(loaded.instruments[i].wave == oldWaves[i]);
    assert(loaded.instruments[i].pitch == 700 && loaded.instruments[i].rate == 4 &&
           loaded.instruments[i].depth == 1 && loaded.instruments[i].retrig);
  }
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::trunc);
    out << "DUBSIREN2\n700 4 1 0.25 0.35 0.45 0.35 0 2 0 1 6 4 3 5\n";
  }
  assert(loadPreset(app, 0, loaded));
  const int waves2[] = {6, 4, 3, 5};
  for (int i = 0; i < triggerCount; ++i) {
    assert(loaded.instruments[i].wave == waves2[i]);
    assert(loaded.instruments[i].pitch == 700 && loaded.instruments[i].lfoWave == 2 &&
           !loaded.instruments[i].retrig);
  }
  assert(loaded.echo);
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::trunc);
    out << "DUBSIREN2\n700 4 1 0.25 0.35 0.45 0.35 0 0 1 0 0 1 2 7\n";
  }
  assert(!loadPreset(app, 0, loaded));
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::trunc);
    out << "DUBSIREN3\n0.25 0.35 0.45 0.35 1\n"
           "600 4 1 0 0 1\n600 4 1 0 0 1\n600 4 1 0 0 2\n600 4 1 0 0 1\n";
  }
  assert(!loadPreset(app, 0, loaded));

  app.assignments = {{3, 0, 2, 1}};
  app.axisRate = {{10, 100, 150, 300}};
  app.axisInvert = {{false, true, true, false}};
  assert(saveAssignments(app));
  app.assignments = {{0, 0, 0, 0}};
  app.axisRate = {{100, 100, 100, 100}};
  app.axisInvert = {{false, false, false, false}};
  loadAssignments(app);
  assert((app.assignments == std::array<int, 4>{{3, 0, 2, 1}}));
  assert((app.axisRate == std::array<int, 4>{{10, 100, 150, 300}}));
  assert((app.axisInvert == std::array<bool, 4>{{false, true, true, false}}));
  {
    std::ofstream out(std::filesystem::u8path(folder + "axes.txt"), std::ios::trunc);
    out << "DUBAXIS1 7 0 5 3\n";
  }
  loadAssignments(app);
  assert((app.assignments == std::array<int, 4>{{0, 0, 0, 3}}));
  assert(app.status == "COMMON AXES DISABLED");
  std::ifstream legacyAxes(std::filesystem::u8path(folder + "axes.txt"));
  std::string legacyLine;
  std::getline(legacyAxes, legacyLine);
  assert(legacyLine == "DUBAXIS1 7 0 5 3");
  legacyAxes.close();
  {
    std::ofstream out(std::filesystem::u8path(folder + "axes.txt"), std::ios::trunc);
    out << "DUBAXIS1 1 2 0 2\n";
  }
  loadAssignments(app);
  assert((app.axisInvert == std::array<bool, 4>{{false, true, false, true}}));
  assert((app.axisRate == std::array<int, 4>{{100, 100, 100, 100}}));
  {
    std::ofstream out(std::filesystem::u8path(folder + "axes.txt"), std::ios::trunc);
    out << "DUBAXIS2 1 100 0 2 301 1 3 100 0 0 100 0\n";
  }
  loadAssignments(app);
  assert(app.status == "INVALID AXIS FILE");
  assert((app.assignments == std::array<int, 4>{{1, 2, 0, 2}}));

  app.theme = 1;
  assert(saveTheme(app));
  app.theme = 0;
  loadTheme(app);
  assert(app.theme == 1);
  {
    std::ofstream out(std::filesystem::u8path(folder + "theme.txt"), std::ios::trunc);
    out << "DUBTHEME1 2\n";
  }
  loadTheme(app);
  assert(app.theme == 1 && app.status == "INVALID THEME FILE");
  App missing;
  missing.folder = folder + "missing/";
  assert(!savePreset(missing, 0) && !saveTheme(missing));

  app.patch = factoryPreset(0);
  app.selectedInstrument = 0;
  const Instrument original = app.patch.instruments[0];
  app.assignments[0] = 1;
  applyAxis(app, 0, 0.1f, 1.0f);
  assert(sameInstrument(original, app.patch.instruments[0]));
  app.selectedInstrument = 1;
  app.page = 1;
  const float volume = app.patch.volume, time = app.patch.echoTime;
  const float pitch1 = app.patch.instruments[1].pitch;
  applyAxis(app, 0, 0.5f, 0.1f);
  assert(app.patch.instruments[1].pitch > pitch1);
  assert(sameInstrument(original, app.patch.instruments[0]));
  assert(app.patch.volume == volume && app.patch.echoTime == time);
  app.assignments[1] = 2;
  const float initialRate = app.patch.instruments[1].rate;
  applyAxis(app, 1, -0.5f, 0.1f);
  assert(app.patch.instruments[1].rate > initialRate);
  const float higherRate = app.patch.instruments[1].rate;
  applyAxis(app, 1, 0.5f, 0.1f);
  assert(app.patch.instruments[1].rate < higherRate);
  app.assignments[3] = 2;
  applyAxis(app, 3, 0.5f, 0.1f);
  assert(app.patch.instruments[1].rate < initialRate);
  assert(sameInstrument(original, app.patch.instruments[0]));
  app.axisInvert[3] = false;
  app.axisRate[3] = 200;
  const float beforeFast = app.patch.instruments[1].rate;
  applyAxis(app, 3, 0.5f, 0.1f);
  assert(std::fabs(app.patch.instruments[1].rate - beforeFast - 0.6f) < 0.0001f);
  app.page = 0;

  const int wave = app.patch.instruments[1].wave;
  app.selection = 16;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.patch.instruments[1].wave == (wave + 1) % 7);
  assert(app.patch.instruments[0].wave == original.wave);
  assert(value(app, 16) == waves[app.patch.instruments[1].wave]);
  const Instrument beforeLfo = app.patch.instruments[1];
  adjust(app, 1, 1);
  adjust(app, 2, 1);
  adjust(app, 7, 1);
  app.selection = 8;
  press(app, SDL_CONTROLLER_BUTTON_A);
  assert(app.patch.instruments[1].rate != beforeLfo.rate);
  assert(app.patch.instruments[1].depth != beforeLfo.depth);
  assert(app.patch.instruments[1].lfoWave != beforeLfo.lfoWave);
  assert(app.patch.instruments[1].retrig != beforeLfo.retrig);
  assert(sameInstrument(original, app.patch.instruments[0]));

  App unchanged, edited;
  unchanged.audio = edited.audio = app.audio;
  action(unchanged, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  action(edited, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  std::array<int16_t, 1024> unchangedAudio{}, editedAudio{};
  unchanged.siren.render(unchangedAudio.data(), 512);
  edited.siren.render(editedAudio.data(), 512);
  assert(unchangedAudio == editedAudio);
  edited.selectedInstrument = 1;
  adjust(edited, 0, 10);
  unchanged.siren.render(unchangedAudio.data(), 512);
  edited.siren.render(editedAudio.data(), 512);
  assert(unchangedAudio == editedAudio);
  action(unchanged, leftTrigger, true, 1);
  action(edited, leftTrigger, true, 1);
  unchanged.siren.render(unchangedAudio.data(), 512);
  edited.siren.render(editedAudio.data(), 512);
  assert(unchangedAudio != editedAudio);

  app.selection = 3;
  app.selectedInstrument = 0;
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  assert(app.selection == 4 && app.selectedInstrument == 1);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  assert(app.activeTrigger == 0 && app.selectedInstrument == 0 && app.selection == 3);
  action(app, leftTrigger, true, 1);
  assert(app.activeTrigger == 1 && app.selectedInstrument == 1 && app.selection == 4);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true, 1);
  action(app, rightTrigger, true, 1);
  assert(app.activeTrigger == 3 && app.selectedInstrument == 3);
  action(app, rightTrigger, false, 1);
  assert(app.activeTrigger == 2 && app.selectedInstrument == 2);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false, 1);
  assert(app.activeTrigger == 1 && app.selectedInstrument == 1);
  action(app, leftTrigger, false, 1);
  assert(app.activeTrigger == 0 && app.selectedInstrument == 0);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 2);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false, 1);
  assert(app.activeTrigger == 0 && app.gateSources);
  app.preset = 1;
  recall(app);
  assert(app.status == "RELEASE TRIGGER FIRST");
  panic(app);
  assert(!app.gateSources && app.triggerBlocked);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false, 2);
  assert(!app.triggerBlocked);

  press(app, SDL_CONTROLLER_BUTTON_BACK);
  assert(app.page == 1 && app.selection == 9);
  app.selection = 15;
  app.theme = 0;
  press(app, SDL_CONTROLLER_BUTTON_A);
  assert(app.theme == 1 && app.status == "THEME SAVED");
  app.theme = 0;
  loadTheme(app);
  assert(app.theme == 1 && value(app, 15) == "NIGHT RASTAFARI");
  action(app, rightTrigger, true, 1);
  assert(app.page == 1 && app.selectedInstrument == 3 && app.activeTrigger == 3);
  action(app, rightTrigger, false, 1);
  press(app, SDL_CONTROLLER_BUTTON_BACK);
  assert(app.assignMode && app.page == 1);
  press(app, SDL_CONTROLLER_BUTTON_BACK);
  assert(app.page == 0 && app.selection == 6 && app.selectedInstrument == 3);

  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 2);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  assert(app.page == 0);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 2);
  assert(app.page == 1);
  app.selection = 14;
  app.preset = 4;
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.page == 1 && app.status == "PRESET SAVED");
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.page == 1 && app.status == "PRESET SAVED");
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_START);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  assert(app.page == 1 && !app.running);
  app.running = true;
  app.assignMode = true;
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_B);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  assert(app.assignMode && app.page == 1 && app.status == "PANIC: AUDIO STOPPED");
  app.assignMode = false;
  press(app, SDL_CONTROLLER_BUTTON_BACK);
  assert(app.assignMode && app.page == 1);
  press(app, SDL_CONTROLLER_BUTTON_BACK);
  assert(app.page == 0 && app.selection == 6);

  app.preset = 4;
  const Sound beforeSave = app.patch;
  press(app, SDL_CONTROLLER_BUTTON_Y);
  assert(app.status == "PRESET SAVED");
  app.patch = factoryPreset(2);
  press(app, SDL_CONTROLLER_BUTTON_X);
  assert(app.status == "PRESET LOADED" && sameSound(app.patch, beforeSave));

  app.assignMode = true;
  app.selectedAxis = 0;
  app.selectedAxisField = 0;
  app.assignments[0] = 3;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.assignMode && app.assignments[0] == 0 && app.status == "AXES SAVED");
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  assert(app.selectedAxisField == 1);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.axisRate[0] == 110);
  app.axisRate[0] = 300;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.axisRate[0] == 300);
  app.axisRate[0] = 10;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.axisRate[0] == 10);
  press(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  assert(app.selectedAxisField == 2);
  const bool wasInverted = app.axisInvert[0];
  press(app, SDL_CONTROLLER_BUTTON_A);
  assert(app.assignMode && app.axisInvert[0] != wasInverted);
  loadAssignments(app);
  assert(app.axisRate[0] == 10 && app.axisInvert[0] != wasInverted);
  press(app, SDL_CONTROLLER_BUTTON_B);
  assert(!app.assignMode);

  const int directions[] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                            SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
  for (int page = 0; page < 2; ++page) {
    std::array<bool, parameterCount> reachable{};
    reachable[page == 0 ? 0 : 9] = true;
    for (int iteration = 0; iteration < parameterCount; ++iteration)
      for (int from = 0; from < parameterCount; ++from)
        if (reachable[from])
          for (int direction : directions) reachable[nextSelection(from, direction)] = true;
    for (int index = 0; index < parameterCount; ++index)
      assert(reachable[index] == (page == 0 ? index <= 8 || index == 16 :
                                                   index >= 9 && index <= 15));
  }
  assert(std::fabs(dialAngle(0) + 5.0f * 3.14159265358979323846f / 6.0f) < 0.0001f);
  assert(std::fabs(dialAngle(0.5f)) < 0.0001f);
  assert(std::fabs(dialAngle(1) - 5.0f * 3.14159265358979323846f / 6.0f) < 0.0001f);
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 120, 120, 32, SDL_PIXELFORMAT_ARGB8888);
  assert(surface);
  SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
  assert(renderer);
  const Palette colors = palette(0);
  const float endAngle = dialAngle(1);
  const int endX = 60 + static_cast<int>(std::sin(endAngle) * 40);
  const int endY = 60 - static_cast<int>(std::cos(endAngle) * 40);
  const Uint32 activeTick = SDL_MapRGBA(surface->format, colors.focus.r, colors.focus.g,
                                        colors.focus.b, 255);
  const Uint32 inactiveTick = SDL_MapRGBA(surface->format, colors.muted.r, colors.muted.g,
                                          colors.muted.b, 255);
  const auto endPixel = [&]() {
    return static_cast<Uint32*>(surface->pixels)[endY * (surface->pitch / 4) + endX];
  };
  dial(renderer, 60, 60, 0, colors);
  SDL_RenderPresent(renderer);
  assert(endPixel() == inactiveTick);
  dial(renderer, 60, 60, 1, colors);
  SDL_RenderPresent(renderer);
  assert(endPixel() == activeTick);
  const Uint32 notch = SDL_MapRGBA(surface->format, rastaGold.r, rastaGold.g, rastaGold.b, 255);
  const int notchX = 60 + static_cast<int>(std::sin(endAngle) * 24);
  const int notchY = 60 - static_cast<int>(std::cos(endAngle) * 24);
  assert(static_cast<Uint32*>(surface->pixels)[notchY * (surface->pitch / 4) + notchX] == notch);
  panel(renderer, 10, 10, 90, 60, true, colors);
  SDL_RenderPresent(renderer);
  const Uint32 interior = SDL_MapRGBA(surface->format, colors.inset.r, colors.inset.g, colors.inset.b, 255);
  const Uint32 border = SDL_MapRGBA(surface->format, colors.focus.r, colors.focus.g, colors.focus.b, 255);
  assert(static_cast<Uint32*>(surface->pixels)[17 * (surface->pitch / 4) + 18] == interior);
  assert(static_cast<Uint32*>(surface->pixels)[10 * (surface->pitch / 4) + 10] == border);
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);

  App touch;
  touch.audio = app.audio;
  touch.folder = folder;
  pointerDown(touch, false, 1, 10, 50, 90);
  assert(touch.activeTrigger == 0 && touch.gateSources);
  pointerDown(touch, false, 1, 11, 200, 90);
  assert(touch.activeTrigger == 1 && touch.selectedInstrument == 1);
  pointerUp(touch, false, 1, 11);
  assert(touch.activeTrigger == 0 && touch.selectedInstrument == 0);
  pointerUp(touch, false, 1, 10);
  assert(!touch.gateSources);
  const float touchPitch = touch.patch.instruments[0].pitch;
  pointerDown(touch, true, 0, 0, 100, 210);
  pointerMove(touch, true, 0, 0, 190);
  pointerUp(touch, true, 0, 0);
  assert(touch.patch.instruments[0].pitch > touchPitch);
  const int touchWave = touch.patch.instruments[0].wave;
  pointerDown(touch, false, 1, 12, 100, 330);
  pointerUp(touch, false, 1, 12);
  assert(touch.patch.instruments[0].wave == (touchWave + 1) % 7);
  pointerDown(touch, false, 1, 13, 30, 45);
  pointerUp(touch, false, 1, 13);
  assert(touch.page == 1 && !touch.assignMode);
  pointerDown(touch, false, 1, 22, 65, 424);
  assert(touch.activeTrigger == 1);
  pointerUp(touch, false, 1, 22);
  assert(!touch.gateSources);
  const float echoTime = touch.patch.echoTime;
  pointerDown(touch, false, 1, 14, 240, 170);
  pointerMove(touch, false, 1, 14, 150);
  pointerUp(touch, false, 1, 14);
  assert(touch.patch.echoTime > echoTime);
  pointerDown(touch, false, 1, 15, 360, 330);
  pointerUp(touch, false, 1, 15);
  assert(touch.preset == 1);
  pointerDown(touch, false, 1, 16, 200, 330);
  pointerUp(touch, false, 1, 16);
  assert(touch.status == "PRESET LOADED");
  pointerDown(touch, false, 1, 17, 30, 45);
  pointerUp(touch, false, 1, 17);
  assert(touch.assignMode);
  pointerDown(touch, false, 1, 23, 100, 450);
  assert(touch.activeTrigger == 2);
  pointerUp(touch, false, 1, 23);
  assert(!touch.gateSources);
  pointerDown(touch, false, 1, 18, 380, 120);
  pointerMove(touch, false, 1, 18, 100);
  pointerUp(touch, false, 1, 18);
  assert(touch.axisRate[0] == 120);
  pointerDown(touch, false, 1, 19, 500, 120);
  pointerUp(touch, false, 1, 19);
  assert(touch.axisInvert[0]);
  pointerDown(touch, false, 1, 20, 30, 45);
  pointerUp(touch, false, 1, 20);
  assert(!touch.assignMode && touch.page == 0);
  pointerDown(touch, false, 1, 21, 50, 90);
  pointerDown(touch, false, 2, 21, 200, 90);
  pointerUp(touch, false, 1, 21);
  assert(touch.activeTrigger == 1);
  pointerUp(touch, false, 2, 21);
  assert(!touch.gateSources);

  assert(keyButton(SDLK_w) == leftTrigger && keyButton(SDLK_e) == rightTrigger);
  triggerAxis(app, 1, 16001);
  assert(app.activeTrigger == 1);
  action(app, leftTrigger, true, 1);
  triggerAxis(app, 1, 11999);
  assert(app.activeTrigger == 1);
  action(app, leftTrigger, false, 1);
  assert(!app.gateSources);

  SDL_CloseAudioDevice(app.audio);
  SDL_Quit();
  std::filesystem::remove(std::filesystem::u8path(presetPath(app, 0)));
  std::filesystem::remove(std::filesystem::u8path(folder + "axes.txt"));
  std::filesystem::remove(std::filesystem::u8path(folder + "theme.txt"));
  std::filesystem::remove(std::filesystem::u8path(folder));
}
