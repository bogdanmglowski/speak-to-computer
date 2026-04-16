# speak-to-computer

## Description

This is a small speech-to-text application.

The project started as an experiment and a playground for working with AI coding agents (Codex CLI, Claude Code CLI).
I'm not a C++ developer, so most of the code was generated and iteratively refined using AI-assisted development.

The goals were:
- to practice working with coding agents,
- to explore "vibe coding" workflows,
- to experiment with speech-to-text integration.

This is not a production-ready project, but it turned out to be somewhat useful, so I'm sharing it here.

## Future ideas

Things I’d like to experiment with next:
- system tray support (run in background)
- automatic translation (PL speech → EN text, etc.)
- better logging + basic transcription history


## Requirements
- X11
- Qt 6 development packages
- CMake and a C++20 compiler
- Git and Make
- one supported audio recorder:
  - PipeWire: `pw-record`
  - PulseAudio-compatible: `parec` or `parecord`
  - ALSA fallback: `arecord`
- `xdotool`
- `whisper.cpp` built at `~/whisper.cpp` (can be overridden with `whisper_cli` setting)
- a multilingual Whisper model, defaulting to `ggml-small.bin` (can be overridden with `model` setting)

The current defaults:

```bash
~/whisper.cpp/build/bin/whisper-cli
~/whisper.cpp/models/ggml-small.bin
```

## Install whisper.cpp

Huge respect for the [`ggml-org/whisper.cpp`](https://github.com/ggml-org/whisper.cpp)
I strongly recommend checking it out.

`speak-to-computer` uses the upstream `whisper-cli` binary from
[`ggml-org/whisper.cpp`](https://github.com/ggml-org/whisper.cpp). Keep the
checkout under `~/whisper.cpp` so the app defaults work
without extra configuration.

#### Clone it to your Home dir:

```bash
git clone https://github.com/ggml-org/whisper.cpp.git
```

#### Build a fast CPU-local release binary:

```bash
cd ~/whisper.cpp
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DGGML_NATIVE=ON \
    -DGGML_OPENMP=ON
cmake --build build -j --config Release
```

This is the recommended default for users building on their own machine:

- `Release` enables optimized `-O3` builds.
- `GGML_NATIVE=ON` enables CPU-specific instructions for the current machine.
- `GGML_OPENMP=ON` lets Whisper use multiple CPU threads.
- `BUILD_SHARED_LIBS=ON` matches the current local build. `speak-to-computer`
  automatically sets `LD_LIBRARY_PATH` at runtime so `whisper-cli` can load the
  local Whisper shared libraries.

Use `-DGGML_NATIVE=OFF` only when building a portable binary that must run on
older or different CPUs than the build machine.

Consider using `-DGGML_CUDA=1` or `-DGGML_VULKAN=1` for GPU builds.
For more information, see the https://github.com/ggml-org/whisper.cpp

#### Download the default multilingual `small` model:

```bash
cd ~/whisper.cpp
sh ./models/download-ggml-model.sh small
```

#### Verify `whisper-cli` directly:

```bash
cd ~/whisper.cpp
LD_LIBRARY_PATH="$PWD/build/src:$PWD/build/ggml/src" \
    ./build/bin/whisper-cli -m models/ggml-small.bin -f samples/jfk.wav -l en -t 4 -np -nt
```


## Build App

`$APP_LOCATION` - is the directory where you cloned this repository

```bash
cd $APP_LOCATION/speak-to-computer
./scripts/clean-build.sh
```

The executable is created at:

```bash
$APP_LOCATION/speak-to-computer/build-speak-to-computer/speak-to-computer
```

## Rebuild After Code Changes

Use this after editing files in `src/`, `tests/`, or `CMakeLists.txt`:

```bash
cd $APP_LOCATION/speak-to-computer
cmake --build build-speak-to-computer
ctest --test-dir build-speak-to-computer --output-on-failure
```

If `CMakeLists.txt` changed and the build behaves strangely, rerun configure
before building:

```bash
cd $APP_LOCATION/speak-to-computer
./scripts/clean-build.sh
```

## Run

```bash
cd $APP_LOCATION/speak-to-computer
./build-speak-to-computer/speak-to-computer
```

To keep logs in the terminal while testing:

```bash
cd $APP_LOCATION/speak-to-computer
QT_LOGGING_RULES='*.info=true' ./build-speak-to-computer/speak-to-computer
```

Press `Super+Space` to start recording. Press the same hotkey again to stop
recording, transcribe, and paste the text into the window that was active when
recording started.

## Settings

The first run writes defaults to:

```bash
~/.config/speak-to-computer/settings.ini
```

Supported settings:

```ini
hotkey=Super+Space
audio_backend=auto
language=pl
threads=12
whisper_cli=~/whisper.cpp/build/bin/whisper-cli
model=~/whisper.cpp/models/ggml-small.bin
```

Set `language=auto` to let Whisper detect the spoken language. Use `language=en`
for English-only dictation.

`audio_backend=auto` picks the first available recorder in this order:
`pw-record`, `parec`, `parecord`, then `arecord`. You can force a backend with
`audio_backend=pipewire`, `audio_backend=pulseaudio`, or `audio_backend=alsa`.
Systems using `pavucontrol` usually have a PulseAudio-compatible server, so
`audio_backend=pulseaudio` should work with either classic PulseAudio or
PipeWire's PulseAudio compatibility layer.
