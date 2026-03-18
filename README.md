## CrystalTts

CrystalTts is a small Qt desktop app that reads selected text using an offline TTS engine (sherpa-onnx) and provides a tray-based workflow.

### Features

- Global tray app (minimize-to-tray behavior)
- Selection-to-speech playback
- PDF viewer (optional; requires Poppler Qt5)
- Settings dialog (speaker UI draft, speed, repeat draft, pause draft, license draft)
- App icon (runtime) via Qt resources
- Optional Windows `.exe` icon via `icons/app.ico`
- App UI translations via Qt `.qm`

### Build requirements

- Qt (Qt 5.x or Qt 6.x)
- qmake toolchain
- sherpa-onnx C API (for offline TTS)
- Optional: Poppler Qt5 (for PDF viewer)

### Configure dependencies

Edit `CrystalTts.pro`:

- **sherpa-onnx**: set `SHERPA_ONNX_DIR` to your install directory (must contain `include/` and `lib/`).
- **Poppler (optional)**: set `POPPLER_DIR` to your Poppler build/install directory.

### Build (qmake)

From a Qt command prompt in the project directory:

```bash
qmake CrystalTts.pro
make
```

On Windows, depending on your Qt kit/toolchain, you may build from Qt Creator or use the generated VS solution/project.

### Runtime model files

By default, the app looks for the model files next to the executable:

- `./tts-model/model.onnx`
- `./tts-model/tokens.txt`
- `./tts-model/espeak-ng-data/`

### App icon

- **Runtime window/tray icon**: comes from Qt resources (`:/icons/app.svg`)
- **Windows executable icon** (Explorer): put an `.ico` at `icons/app.ico` and rebuild (the `.pro` uses `RC_ICONS` if it exists).

### Translations (App language)

App language is loaded at startup and can also be applied immediately when changed in Settings (if the `.qm` exists).

- Translation sources live in `i18n/` (see `i18n/README.md`).
- Example: Japanese translation files
  - Source: `i18n/CrystalTts_ja.ts`
  - Binary: `i18n/CrystalTts_ja.qm`

Generate `.qm` (Qt tools required):

```bash
lupdate CrystalTts.pro -ts i18n/CrystalTts_ja.ts
lrelease i18n/CrystalTts_ja.ts -qm i18n/CrystalTts_ja.qm
```

Place the `.qm` next to the built executable under:

- `<appdir>/i18n/CrystalTts_ja.qm`

### Contributing

- Open an issue for bugs/features, and include reproduction steps (logs/screenshots help).
- PRs are welcome. Keep changes focused and prefer small, reviewable commits.
- UI text: wrap user-facing strings with `tr()` so they can be translated.

### License

This project is licensed under the **MIT License**. See `LICENSE`.

