# miniaudio

Single-file audio playback and capture library, used by the `audio_input` and `audio_capture` plugins.

- Author: David Reid
- Website: https://miniaud.io
- Source: https://github.com/mackron/miniaudio
- Version: 0.11.24 (2026-01-17), files taken unmodified from the release tag
- License: public domain (Unlicense) or MIT No Attribution, at the user's choice; see `LICENSE`

`miniaudio.c` only defines `MINIAUDIO_IMPLEMENTATION` and includes `miniaudio.h`. It is built as the
`illixr_miniaudio` static library in `utils/CMakeLists.txt`.

To update, replace `miniaudio.h`, `miniaudio.c` and `LICENSE` with the files from a newer release tag
(`update_miniaudio.csh` does this) and change the version above.
