# Lingmo Studio

A video editor for the Lingmo desktop, built on the MLT framework and ffmpeg,
with the Lingmo interface (Qt 6 / QML, `LingmoUI.CompatibleModule`).

* **Media** — import, the source viewer, clip details and a large Media Pool.
* **Cut** — fast assembly: one-click append, insert, place on top and smart
  trim, the whole program above a close-up timeline that follows the playhead,
  and “Gerar cortes” and captions one click away.
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
* **Dub in another language** (Deliver › the button under the SRT export) — the
  speaker's own voice, cloned, says the translated speech in up to 23 languages.
  The speech of the timeline is translated for the ear by the Ollama model of
  Settings › AI (each sentence sized to the time it takes, numbers and
  abbreviations written as they are spoken); the voice reference is the most
  continuous 10–15 s of the video (or a recording you choose);
  Chatterbox Multilingual V3 (MIT) speaks it — `cfg_weight` 0, so the timbre is
  kept without the accent of the original language — and every passage is
  placed where the original sentence began, sped up by at most 1.3× when the
  translation runs long. One WAV and one MP3 per language, as long as the
  video, for YouTube Studio › Languages › Add dub; or put the dub on a new
  audio track. The engine (PyTorch + a 3 GB model, ~9 GB) is installed on demand
  into `~/.local/share/lingmo/dub-venv` from the dialog (needs `uv` or Python
  3.10–3.12; `git` for the V3 build, else the stable V2). On a CPU it takes ~12×
  the length of the speech; a card with 6 GB of video memory is far faster.
  Nothing is written into the audio (Chatterbox's watermark is turned off).
* **Title, description and thumbnail** (Deliver › the button under the SRT
  export) — the words spoken on the timeline (captions, known transcripts, or
  a new Whisper run) go to the Ollama model chosen in Settings › AI, which
  writes a title, description, tags and a short thumbnail headline; timeline
  markers become YouTube chapters. Without a model a heuristic writes a simple
  version. The thumbnail is built from frames rendered from the timeline
  (scored for detail, contrast and light, so black or flat frames lose) with the
  headline on top in one of three layouts; every field and the frame stay
  editable, and the image saves as JPEG (under YouTube's 2 MB) or PNG.

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
* Ollama with a model chosen in Settings › AI — scores the cuts, writes hook
  titles, writes the video's title, description and tags, and translates the
  dubs (the only feature that cannot do without it); otherwise a heuristic decides.
* The dubbing engine (see Dub in another language): `uv` or Python 3.10–3.12, `git`,
  and network access once to install it.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

GPL-3.0-or-later.
