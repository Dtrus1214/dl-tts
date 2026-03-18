## Generating translation files (.qm)

This project loads app translations from:

- `./i18n/CrystalTts_<lang>.qm` (next to the built executable)
- or `:/i18n/CrystalTts_<lang>.qm` (if you later add them as Qt resources)

### 1) Update `.ts` from source (extract new strings)

From a Qt command prompt:

```bash
lupdate CrystalTts.pro -ts i18n/CrystalTts_ja.ts
```

### 2) Compile `.ts` -> `.qm`

```bash
lrelease i18n/CrystalTts_ja.ts -qm i18n/CrystalTts_ja.qm
```

### Notes

- The app setting is stored at `settings/appLanguage` (e.g. `ja`, `en`).
- Restart the app after changing the Application language (current implementation loads translator at startup).
