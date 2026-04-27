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
- make the sys tray icon transparent
- add TLDR fast install instructions to the README
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

#### Building for NVIDIA GPU (Recommended for faster transcription)

If you have an NVIDIA GPU, building with CUDA support provides significantly faster transcription performance compared to CPU-only builds. GPU acceleration can reduce transcription time by 3-10x depending on your hardware.

**Prerequisites:**
- NVIDIA GPU with CUDA support
- NVIDIA drivers installed
- CUDA Toolkit 13.x installed

**Install CUDA Toolkit (Ubuntu/Debian):**

```bash
# Download and install CUDA repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-ubuntu2404.pin
sudo mv cuda-ubuntu2404.pin /etc/apt/preferences.d/cuda-repository-pin-600
wget https://developer.download.nvidia.com/compute/cuda/13.2.1/local_installers/cuda-repo-ubuntu2404-13-2-local_13.2.1-595.58.03-1_amd64.deb
sudo dpkg -i cuda-repo-ubuntu2404-13-2-local_13.2.1-595.58.03-1_amd64.deb
sudo cp /var/cuda-repo-ubuntu2404-13-2-local/cuda-*-keyring.gpg /usr/share/keyrings/
sudo apt-get update
sudo apt-get -y install cuda-toolkit-13-2
```

**Add CUDA to your PATH (add to ~/.bashrc or ~/.zshrc):**

```bash
export PATH=/usr/local/cuda-13.2/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda-13.2/lib64:$LD_LIBRARY_PATH
```

Then reload your shell: `source ~/.bashrc` (or `~/.zshrc`)


**Install CUDA Toolkit (Fedora):**
```bash

sudo dnf config-manager addrepo \
    --from-repofile=https://developer.download.nvidia.com/compute/cuda/repos/fedora43/x86_64/cuda-fedora43.repo
sudo dnf clean expire-cache
sudo dnf makecache
sudo dnf install cuda-toolkit

echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
```
Then reload your shell: `source ~/.zshrc`


**Build whisper.cpp with CUDA:**

```bash
cd ~/whisper.cpp
rm -rf build  # Clean previous build
cmake -B build -DGGML_CUDA=1 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build -j --config Release
```

The build will automatically detect your GPU architecture. Verify CUDA is working:

```bash
nvidia-smi  # Should show your GPU
```

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

Press `Super+Space` to start recording original-language dictation, or
`Super+Shift+Space` to start recording text that should be translated to
English. Press the same hotkey again to stop recording, transcribe, and paste
the text into the window that was active when recording started. While
recording, pressing the other hotkey stops the recording and uses that output
instead.

The app runs in the background and exposes a system tray icon. Right-click the
tray icon and choose `Quit` to stop it. Tray toggles:
- `Wake Word Listening` enables/disables wake-word trigger.
- `Voice Activity Auto-Stop` enables/disables VAD-based auto-stop (speech then silence).

## Install Autostart

Use this when you want the app to start when you log in:

```bash
cd $APP_LOCATION/speak-to-computer
./scripts/install-autostart.sh
```

The installer also tries to install `libfvad` (VAD runtime dependency) using
your system package manager when available.

The install script copies the freshly built binary and default notification sounds to:

```bash
~/.local/bin/speak-to-computer
~/.local/bin/activation_sound.wav
~/.local/bin/end_sound.wav
```

Wake-word runtime files are copied to:

```bash
~/.local/share/speak-to-computer/python/openwakeword_sidecar.py
~/.local/share/speak-to-computer/python/install-openwakeword-runtime.sh
~/.local/share/speak-to-computer/python/openwakeword/requirements.txt
~/.local/share/speak-to-computer/python/openwakeword/smoke_test.py
```

It also writes:

```bash
~/.local/share/applications/speak-to-computer.desktop
~/.config/autostart/speak-to-computer.desktop
~/.local/share/icons/hicolor/scalable/apps/speak-to-computer.svg
```

The script does not start or restart the app. Run it once manually after install,
or let it start on your next login:

```bash
~/.local/bin/speak-to-computer
```

## Settings

The first run writes defaults to:

```bash
~/.config/speak-to-computer/settings.ini
```

Supported settings:

```ini
hotkey_dictate=Super+Space
hotkey_translate_en=Super+Shift+Space
audio_backend=auto
language=pl
threads=12
whisper_cli=~/whisper.cpp/build/bin/whisper-cli
model=~/whisper.cpp/models/ggml-small.bin
activation_sound=activation_sound.wav
end_sound=end_sound.wav
wake_word_enabled=false
wake_word_phrase=alexa
wake_word_model_path=
wake_word_threshold=0.5
wake_word_sidecar_executable=~/.local/share/speak-to-computer/python/.venv/bin/python
wake_word_sidecar_script=~/.local/share/speak-to-computer/python/openwakeword_sidecar.py
vad_autostop_enabled=false
vad_aggressiveness=2
vad_end_silence_ms=900
vad_min_speech_ms=250
```

Set `language=auto` to let Whisper detect the spoken language. Use `language=en`
for English-only dictation.

Use `hotkey_dictate` for original-language dictation and
`hotkey_translate_en` for translation to English. The output mode is chosen by
the hotkey used for the current recording, not stored as a persistent setting.

`audio_backend=auto` picks the first available recorder in this order:
`pw-record`, `parec`, `parecord`, then `arecord`. You can force a backend with
`audio_backend=pipewire`, `audio_backend=pulseaudio`, or `audio_backend=alsa`.
Systems using `pavucontrol` usually have a PulseAudio-compatible server, so
`audio_backend=pulseaudio` should work with either classic PulseAudio or
PipeWire's PulseAudio compatibility layer.

`vad_autostop_enabled=true` auto-stops recording only after speech is detected
and then trailing silence reaches `vad_end_silence_ms`. Silence without speech
does not auto-stop recording. Manual hotkey stop always works regardless of VAD
state.

### VAD Auto-Stop Runtime (`libfvad`)

`Voice Activity Auto-Stop` depends on `libfvad` at runtime.

The autostart installer (`./scripts/install-autostart.sh`) tries to install it
automatically, but some distributions do not provide `libfvad` in their
repositories (for example Pop!_OS 24.04 / Ubuntu 24.04).

Try package manager install first:

```bash
sudo apt-get update
sudo apt-get install -y libfvad-dev
```

If package installation fails (`Unable to locate package libfvad`), build it
from source:

```bash
sudo apt-get update
sudo apt-get install -y git build-essential autoconf automake libtool pkg-config

git clone https://github.com/dpirch/libfvad.git /tmp/libfvad
cd /tmp/libfvad
autoreconf -i
./configure --prefix=/usr/local
make -j"$(nproc)"
sudo make install
sudo ldconfig
```

Verify installation:

```bash
ldconfig -p | grep -i fvad
```

Expected output should include `libfvad.so` and `libfvad.so.0` (for example
from `/usr/local/lib`).

After installation, restart the app. If needed, tune settings in
`~/.config/speak-to-computer/settings.ini`:

```ini
vad_autostop_enabled=true
vad_aggressiveness=0
vad_min_speech_ms=120
vad_end_silence_ms=700
```

## Wake Word (optional)

Wake-word detection is optional and uses `openwakeword` through a local Python
sidecar. It does not send microphone data to cloud services.

Install wake-word runtime (creates dedicated venv and installs dependencies):

```bash
~/.local/share/speak-to-computer/python/install-openwakeword-runtime.sh
```
The installer also downloads required openWakeWord models (including `alexa`).
For current `openwakeword==0.6.0` Linux runtime, the installer pins `numpy<2`
to stay compatible with `tflite-runtime`.

If your default `python3` is 3.12+, install Python 3.11 (or 3.10) and run:

```bash
PYTHON_BIN=python3.11 ~/.local/share/speak-to-computer/python/install-openwakeword-runtime.sh
```

On Pop!_OS 24.04 / Ubuntu 24.04, a simple option is `uv`:

```bash
uv python install 3.11
~/.local/share/speak-to-computer/python/install-openwakeword-runtime.sh
```

Enable wake-word listening in `settings.ini`:

```ini
wake_word_enabled=true
wake_word_phrase=alexa
```

With the defaults above, the sidecar uses:

- `~/.local/share/speak-to-computer/python/.venv/bin/python`
- `~/.local/share/speak-to-computer/python/openwakeword_sidecar.py`

The sidecar auto-resolves the bundled Alexa model from the installed
`openwakeword` package. You only need `wake_word_model_path` if you want to
override with a custom ONNX/TFLite model.

You can also enable/disable wake-word listening at runtime from the tray menu
(`Wake Word Listening`). After detection, the app starts the same recording
flow as `Super+Space`, then returns to listening mode.
