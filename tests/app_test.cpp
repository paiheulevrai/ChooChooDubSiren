#define main dubsiren_main
#include "../src/main.cpp"
#undef main

#include <cassert>

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

  app.patch.pitch = 1234;
  app.patch.echo = true;
  app.patch.triggerWaves = {{6, 4, 3, 5}};
  assert(savePreset(app, 0));
  app.patch.pitch = 700;
  assert(savePreset(app, 0));
  SirenPatch loaded;
  assert(loadPreset(app, 0, loaded));
  assert(loaded.pitch == 700 && loaded.echo);
  assert((loaded.triggerWaves == std::array<int, 4>{{6, 4, 3, 5}}));
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
  assert((loaded.triggerWaves == std::array<int, 4>{{2, 1, 6, 5}}));
  {
    std::ofstream out(std::filesystem::u8path(presetPath(app, 0)), std::ios::trunc);
    out << "DUBSIREN2\n700 4 1 0.25 0.35 0.45 0.35 0 0 1 0 0 1 2 7\n";
  }
  assert(!loadPreset(app, 0, loaded));
  App missing;
  missing.folder = folder + "missing/";
  assert(!savePreset(missing, 0));

  app.assignments = {{7, 0, 5, 3}};
  assert(saveAssignments(app));
  app.assignments = {{0, 0, 0, 0}};
  loadAssignments(app);
  assert((app.assignments == std::array<int, 4>{{7, 0, 5, 3}}));

  App square, triangle, selected, pulse;
  square.audio = triangle.audio = selected.audio = pulse.audio = app.audio;
  action(square, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  action(triangle, leftTrigger, true, 1);
  action(selected, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true, 1);
  action(pulse, rightTrigger, true, 1);
  std::array<int16_t, 512> squareAudio{}, triangleAudio{}, selectedAudio{}, pulseAudio{};
  square.siren.render(squareAudio.data(), 256);
  triangle.siren.render(triangleAudio.data(), 256);
  selected.siren.render(selectedAudio.data(), 256);
  pulse.siren.render(pulseAudio.data(), 256);
  assert(squareAudio != triangleAudio && triangleAudio != selectedAudio &&
         selectedAudio != squareAudio && pulseAudio != squareAudio && pulseAudio != triangleAudio &&
         pulseAudio != selectedAudio);
  assert(square.patch.wave == 0 && triangle.patch.wave == 0 && selected.patch.wave == 0);

  App unchanged, changed;
  unchanged.audio = changed.audio = app.audio;
  action(unchanged, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  action(changed, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  std::array<int16_t, 1024> unchangedAudio{}, changedAudio{};
  unchanged.siren.render(unchangedAudio.data(), 512);
  changed.siren.render(changedAudio.data(), 512);
  assert(unchangedAudio == changedAudio);
  adjust(changed, 3, 1);
  unchanged.siren.render(unchangedAudio.data(), 512);
  changed.siren.render(changedAudio.data(), 512);
  assert(unchangedAudio != changedAudio);

  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 1);
  assert(app.gateSources && app.activeTrigger == 0);
  action(app, leftTrigger, true, 1);
  assert(app.activeTrigger == 1);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true, 1);
  assert(app.activeTrigger == 2);
  action(app, rightTrigger, true, 1);
  assert(app.activeTrigger == 3);
  action(app, rightTrigger, false, 1);
  assert(app.activeTrigger == 2);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false, 1);
  assert(app.activeTrigger == 1);
  action(app, leftTrigger, false, 1);
  assert(app.activeTrigger == 0);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true, 2);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false, 1);
  assert(app.gateSources && app.activeTrigger == 0);
  app.preset = 1;
  recall(app);
  assert(app.patch.pitch == 700);
  assert(app.status == "RELEASE TRIGGER FIRST");
  panic(app);
  recall(app);
  assert(app.status == "RELEASE TRIGGER FIRST");
  action(app, leftTrigger, true, 1);
  assert(app.gateSources == 0);
  action(app, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false, 2);
  action(app, leftTrigger, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true, 1);
  assert(app.gateSources == 1);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false, 1);

  assert(keyButton(SDLK_w) == leftTrigger && keyButton(SDLK_e) == rightTrigger);
  triggerAxis(app, 1, 16001);
  assert(app.activeTrigger == 1);
  action(app, leftTrigger, true, 1);
  triggerAxis(app, 1, 11999);
  assert(app.activeTrigger == 1);
  action(app, leftTrigger, false, 1);
  assert(!app.gateSources);
  triggerAxis(app, 3, 16001);
  assert(app.activeTrigger == 3);
  triggerAxis(app, 3, 11999);
  assert(!app.gateSources);

  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_B, true, 1);
  assert(!app.gateSources && app.triggerBlocked);
  action(app, SDL_CONTROLLER_BUTTON_B, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false, 1);
  assert(!app.triggerBlocked);
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_B, true, 1);
  assert(app.gateSources == 0);
  assert(app.status == "PANIC: AUDIO STOPPED");
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_B, false, 1);
  app.selection = 14;
  app.preset = 0;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.status == "PRESET LOADED");
  app.assignMode = true;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(!app.assignMode);
  app.selection = 0;

  adjust(app, 3, 1);
  assert(app.patch.triggerWaves[0] == 3);
  adjust(app, 6, -1);
  assert(app.patch.triggerWaves[3] == 4);

  const int directions[] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                            SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
  std::array<bool, parameterCount> reachable{};
  reachable[0] = true;
  for (int i = 0; i < parameterCount; ++i) {
    for (int from = 0; from < parameterCount; ++from) {
      if (reachable[from])
        for (int direction : directions) reachable[nextSelection(from, direction)] = true;
    }
  }
  assert(std::all_of(reachable.begin(), reachable.end(), [](bool found) { return found; }));
  assert(nextSelection(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) == 1);
  assert(nextSelection(1, SDL_CONTROLLER_BUTTON_DPAD_DOWN) == 12);
  assert(nextSelection(13, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) == 8);
  assert(nextSelection(8, SDL_CONTROLLER_BUTTON_DPAD_UP) == 7);
  assert(nextSelection(10, SDL_CONTROLLER_BUTTON_DPAD_DOWN) == 6);
  assert(nextSelection(6, SDL_CONTROLLER_BUTTON_DPAD_DOWN) == 14);
  assert(nextSelection(14, SDL_CONTROLLER_BUTTON_DPAD_UP) == 4);

  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true, 2);
  assert(app.selection == 11);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false, 2);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true, 1);
  assert(app.selection == 3);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false, 1);

  app.selection = 8;
  app.patch.retrig = false;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.patch.retrig && app.selection == 8);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(!app.patch.retrig);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 2);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(!app.patch.retrig);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 2);
  assert(app.patch.retrig);

  app.selection = 0;
  app.patch.pitch = 600;
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_UP, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_UP, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.patch.pitch == 900 && app.selection == 0);

  app.selection = 14;
  app.preset = 0;
  app.status = "EDITING";
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.preset == 1 && app.status == "EDITING" && app.patch.pitch == 900);
  app.preset = 4;
  action(app, SDL_CONTROLLER_BUTTON_BACK, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_BACK, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.status == "PRESET SAVED");
  assert(loadPreset(app, 0, loaded) && loaded.pitch == 900);

  app.assignMode = true;
  app.selectedAxis = 0;
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false, 1);
  assert(app.selectedAxis == 1 && app.assignMode);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false, 1);
  assert(app.selectedAxis == 3);
  const int priorAssignment = app.assignments[3];
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_LEFT, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(app.assignMode && app.selectedAxis == 3);
  assert(app.assignments[3] == (priorAssignment + 7) % 8);
  action(app, SDL_CONTROLLER_BUTTON_A, true, 1);
  action(app, SDL_CONTROLLER_BUTTON_A, false, 1);
  assert(!app.assignMode);

  SDL_CloseAudioDevice(app.audio);
  SDL_Quit();
  std::filesystem::remove(std::filesystem::u8path(presetPath(app, 0)));
  std::filesystem::remove(std::filesystem::u8path(folder + "axes.txt"));
  std::filesystem::remove(std::filesystem::u8path(folder));
}
