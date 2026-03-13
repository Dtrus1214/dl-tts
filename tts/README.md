# Offline TTS (sherpa-onnx)

CrystalTts uses **sherpa-onnx** in-process for offline text-to-speech. No Piper CLI or external process.

## Why sherpa-onnx?

- **In-process** – no subprocess, single binary (plus sherpa-onnx DLLs)
- **Same models as Piper** – VITS/Piper ONNX models with `tokens.txt` and `espeak-ng-data`
- **C API** – link `sherpa-onnx-c-api` and call from Qt

## Build setup

### 1. Get sherpa-onnx (with TTS)

- **Prebuilt (Windows):**  
  [sherpa-onnx releases](https://github.com/k2-fsa/sherpa-onnx/releases)  
  Download the package that includes TTS (e.g. “shared libraries” for your arch).  
  Some older prebuilt packages have TTS disabled; use a recent release or build from source.

- **Build from source (recommended if prebuilt has no TTS):**
  ```bash
  git clone https://github.com/k2-fsa/sherpa-onnx
  cd sherpa-onnx
  mkdir build && cd build
  cmake -DSHERPA_ONNX_ENABLE_C_API=ON -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=/path/to/install ..
  cmake --build . -j
  cmake --install .
  ```

### 2. Point the project at sherpa-onnx

- **qmake (.pro):** set `SHERPA_ONNX_DIR` to the install directory, then:
  ```qmake
  INCLUDEPATH += $$SHERPA_ONNX_DIR/include
  LIBS += -L$$SHERPA_ONNX_DIR/lib -lsherpa-onnx-c-api
  ```
  On Windows (MSVC): add `SHERPA_ONNX_DIR\include` to **Include** and `SHERPA_ONNX_DIR\lib` + `sherpa-onnx-c-api.lib` to **Link**. Copy required DLLs (e.g. `sherpa-onnx-c-api.dll`, `onnxruntime.dll`) next to the exe or into PATH.

- **Runtime:** the loader must find `sherpa-onnx-c-api` and ONNX Runtime (e.g. `onnxruntime.dll`). Put them in the same directory as the exe or in your PATH.

## Model setup (Piper/VITS)

Use a **Piper**-style model that sherpa-onnx supports (VITS with `tokens` + `espeak-ng-data`).

### Option A: Pre-converted Piper models

- **sherpa-onnx TTS models:**  
  [sherpa-onnx/releases tag: tts-models](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models)  
  Download a model that includes:
  - `*.onnx`
  - `tokens.txt`
  - (optional) `espeak-ng-data` or instructions to get it

- **espeak-ng-data** (needed for Piper phonemization):  
  [espeak-ng-data.tar.bz2](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2)  
  Extract so the app can point to the `espeak-ng-data` directory.

### Option B: Convert from rhasspy/piper-voices

See sherpa-onnx docs: [Piper — sherpa](https://k2-fsa.github.io/sherpa/onnx/tts/piper.html).  
You need the ONNX model, add metadata, generate `tokens.txt`, and use the same `espeak-ng-data`.

## Install next to the app (default paths)

By default the engine looks under the application directory:

```
CrystalTts.exe
tts-model/
  model.onnx
  tokens.txt
  espeak-ng-data/    (directory from sherpa-onnx or extracted archive)
```

Or set paths in code before `initialize()`:

```cpp
m_ttsEngine->setModelPath("C:/path/to/voice.onnx");
m_ttsEngine->setTokensPath("C:/path/to/tokens.txt");
m_ttsEngine->setDataDir("C:/path/to/espeak-ng-data");
m_ttsEngine->initialize();
```

## References

- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)
- [sherpa-onnx TTS docs](https://k2-fsa.github.io/sherpa/onnx/tts/index.html)
- [Piper models in sherpa-onnx](https://k2-fsa.github.io/sherpa/onnx/tts/piper.html)
