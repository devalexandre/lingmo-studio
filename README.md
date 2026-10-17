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
* **Local analysis** — automatic subtitles with whisper.cpp or faster-whisper
  (when installed), cut silence, split at scene changes, reframe to 9:16,
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
Optional: `whisper.cpp` (with a ggml model) or `whisper-ctranslate2`
(faster-whisper) for automatic subtitles.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

GPL-3.0-or-later.
