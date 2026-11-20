<div align="center">

# CHOOCHOO DUB SIREN

**A sound-system siren for handheld consoles.**

Hold a trigger. Shape the movement. Send it into the echo.

![ChooChoo Dub Siren's sound-system control panel](docs/screenshot.png)

**DUB SIREN · LIVE CONTROL · TAPE-STYLE ECHO · PIXEL ART**

</div>

---

## One console. Four sirens. Your sound.

ChooChoo Dub Siren turns a handheld into a playable dub sound effect. Connect
the headphone output to a mixer, hold a trigger and shape the sound in real
time. It is an instrument, not a sequencer: every burst is played by hand.

- **Four triggers, four waveforms** — assign sine, triangle, square, saw,
  pulse or lo-fi triangle to L1, L2, R1 and R2 independently.
- **Expressive modulation** — adjust pitch, LFO speed, depth and shape.
  Choose whether the LFO restarts on each new trigger.
- **Tape-style echo** — adjust time, feedback and mix; repeats continue after
  you release the trigger or turn off the echo send.
- **Live controls** — navigate with the D-pad and adjust assigned parameters
  with the sticks. Consoles without sticks can use the D-pad for everything.
- **Your sounds** — four factory sounds and four user preset slots. Waveform
  assignments are saved with each preset; stick assignments are global.
- **Made for handhelds** — pixel-art hardware panel, PortMaster and NextUI
  support, plus a Windows build for testing.

## Get playing

1. Launch **ChooChoo Dub Siren** from your console's Ports menu, or run
   `DubSiren.exe` on Windows with the supplied DLLs beside it.
2. Hold **L1, L2, R1 or R2** to play. Each trigger has its own waveform.
3. Use the **D-pad** to move around the panel. Hold **A + D-pad** to change
   the selected control: right/up increases, left/down decreases. Up/down
   makes larger steps on continuous controls.
4. Select a trigger's pad and use **A + D-pad** to change its waveform.
5. Press **B** to stop the sound and clear the echo.

When several triggers are held, the most recently pressed one takes priority.
Releasing it returns to the previous held trigger without interrupting the
note. The audio oscillator runs freely; the retrigger option applies to the
LFO only.

## Controls

| Control | Action |
|---|---|
| L1 / L2 / R1 / R2 | Play the waveform assigned to that trigger |
| D-pad | Navigate the panel in four directions |
| Hold A + D-pad | Edit the selected value |
| A on RETRIG or ECHO | Toggle the selected switch |
| A on PRESET / X | Load the selected preset, with all triggers released |
| Y | Save the current sound to the selected user slot |
| Select + A on PRESET | Save the current sound to the selected user slot |
| Start | Open or close stick assignments |
| B | Panic: stop the siren and clear the echo |
| Select + Start | Quit |

In the stick screen, use the D-pad to select an axis and **A + D-pad** to
change its assignment. Changes are saved globally. **A**, **B** or **Start**
returns to the main panel; use **Select + B** for panic while on this screen.

Default stick assignments: **LX** pitch, **LY** LFO rate, **RX** LFO depth,
**RY** echo time. A dead zone helps prevent unintended adjustments.

### Keyboard

| Console control | Windows key |
|---|---|
| A / B / X / Y | X / Z / S / A |
| L1 / L2 / R1 / R2 | Q / W / R / E |
| D-pad | Arrow keys |
| Start / Select | Space / Left Shift |
| Quit | Escape |

SDL-compatible game controllers are supported on Windows.

## Presets

Select **PRESET**, then hold **A + D-pad** to choose one of four factory
sounds or four user slots. Press **A** or **X** to load it; press **Y** to
save your current settings to a user slot. Loading is blocked while a
trigger is held. Preset changes do not clear existing echo tails.

Older user presets remain readable: their original selectable waveform is
assigned to R1. Back up your user data before deleting or replacing an
installation.

## Connect to a sound system

```text
HANDHELD HEADPHONE OUT  ──▶  MIXER LINE INPUT  ──▶  SOUND SYSTEM
```

Use the appropriate cable or adapter for your mixer. Start with the console
volume and mixer gain **at minimum**, then raise them gradually while
monitoring the output. Avoid feeding the output back into itself and be
careful with high feedback settings.

The software limits the output, but it **cannot guarantee protection** for
your hearing, mixer or speakers. Always set levels and test the signal before
playing through a sound system.

## Install

**Windows:** run `DubSiren.exe` with `SDL2.dll` and `libwinpthread-1.dll` in
the same folder.

**PortMaster:** install `ChooChooDubSiren.sh` and the `choochoodubsiren`
folder under your console's `ports` directory. Launch it from **Ports**.

**NextUI:** install the contents of the Pak in
`Tools/tg5040/ChooChooDubSiren.pak` on the SD card.

On handhelds, user presets and stick mappings are stored under the
installation's `userdata` directory. On Windows they are stored in the SDL
user-data directory. Keep a backup of these files when updating.

Handheld compatibility and latency can vary with the device and firmware.
Test your specific setup before using it live.

## Build and test

The app is written in C++17 and uses SDL2.

- **Windows:** run `scripts\build-windows.ps1` with MSYS2 UCRT64 and SDL2
  installed. The executable and required DLLs are written to
  `build\windows-release`.
- **Linux:** run `make`; run the focused tests with `make test`.
- **ARM64:** from WSL with the ARM64 cross-compiler and SDL2 development
  libraries installed, run:

  ```sh
  PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    make BUILD=build/arm64 CXX=aarch64-linux-gnu-g++ \
    LDFLAGS="-static-libstdc++ -static-libgcc"
  ```

## Credits

Inspired by dub siren instruments and sound-system culture. The tape-style
echo draws on ChooChooTracker's Tape Echo; the bundled pixel font is also
from [ChooChooTracker](https://github.com/paiheulevrai/Choochootracker).
See `LICENSE.tracker` for the upstream MIT license.
