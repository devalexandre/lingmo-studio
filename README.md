# Lingmo Studio

A video editor for the Lingmo desktop, built on the MLT framework and ffmpeg,
with the Lingmo interface (Qt 6 / QML, `LingmoUI.CompatibleModule`).

* **Edit** — unlimited video, audio and subtitle tracks; select, razor, roll,
  slip and slide tools; ripple trims and deletes; magnetic snapping; markers;
  J/K/L shuttle; split at playhead (S); detached audio for J/L cuts; speed,
  reverse and speed ramps; freeze frames; compound clips (nested sequences);
  source and timeline monitor with in/out, insert and overwrite; undo history
  panel; proxies (automatic for 4K media).
* **Color** — lift/gamma/gain wheels, curves, primaries, white balance, HSL
  qualifier, `.cube` LUTs, copy/paste grade, before/after split, waveform,
  RGB parade, vectorscope and histogram.
* **Effects** — transform, crop, masks, blur, glow, sharpen, vignette, chroma
  key, text and title templates, picture in picture, 12 transitions, a
  keyframe curve editor.
* **Audio** — mixer with volume, pan, mute and solo per track; volume and pan
  keyframes; EQ, compressor, noise reduction, auto loudness; one-click
  normalisation to −14 LUFS; waveforms; voice-over recording.
* **Gerar cortes (Shorts/Reels)** — drop in a long video, pick TikTok,
  Instagram Reels, YouTube Shorts, Facebook Reels, Kwai, LinkedIn (1:1, 4:5), X
  or Stories; the video is transcribed, split on sentences, scored (local LLM
  through Ollama when configured in Settings › AI, otherwise audio energy,
  speech density, scene changes and pauses) and shown as cards to play, trim
  and pick. Each pick becomes a vertical sequence, reframed by following the
  face (OpenCV, optional) as editable Position X keyframes, with captions.
  Batch export: one file per cut and platform, plus `.srt`. Platform limits
  live in `data/platforms.json` (edit it to follow platform changes).
* **Captions synced to speech** — word timestamps from whisper.cpp or
  faster-whisper; readability rules (characters per line, 1–2 lines, CPS,
  duration limits, phrase-aware line breaks, platform safe areas); nine styles
  (Clássica, Caixa, Karaokê, Destaque, Palavra a palavra, Podcast, Neon, Alto
  contraste, Duas cores) rendered by libass for preview and export alike;
  words stay editable (text, split/merge, nudge, speaker, keywords) and cue
  edges drag on the timeline. Export burned in, or SRT, VTT and ASS.
* **Local analysis** — cut silence, split at scene changes, reframe to 9:16,
  search clips by spoken words. Nothing leaves the computer.
* **Deliver** — MP4 H.264/AAC (default), YouTube 1080p, 4K HEVC, AV1, WebM VP9,
  Shorts/Reels/TikTok 9:16, ProRes 422 HQ, DNxHR HQ, GIF, MP3, WAV; NVENC and
  VA-API when they work, CPU otherwise; background render queue.

Projects are MLT XML files (`.mlt`): `melt` renders them and Shotcut opens them.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
ctest --test-dir build   # needs ffmpeg and melt
```

Runtime: `mlt ffmpeg frei0r-plugins fftw sdl2 libxml2 libsamplerate vid.stab
qt6-base qt6-declarative qt6-svg LingmoUI lingmo-qt-plugins`.
Optional, checked at run time:

* `whisper-cpp` (official repos: `sudo pacman -S whisper-cpp`) plus a ggml
  model downloaded in Lingmo Settings › AI › Transcrição e legendas
  (`~/.local/share/lingmo/whisper-models/ggml-<model>.bin`), or
  `whisper-ctranslate2` (faster-whisper, pipx/AUR) — captions and cuts.
  Studio reads `subtitle_backend`, `subtitle_model` and `subtitle_language`
  from `~/.config/lingmoos/ai.conf`.
* `python-opencv` — face tracking for the vertical reframe
  (`data/facetrack.py`); without it the crop is centred.
* Ollama with a model chosen in Settings › AI — scores the cuts and writes
  hook titles; without it a heuristic decides.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

GPL-3.0-or-later.
