# Lingmo Studio architecture

Lingmo Studio is a video editor built on the [MLT framework](https://www.mltframework.org/)
(the engine behind Shotcut and Kdenlive) and ffmpeg, with a Qt 6 / QML interface
that uses the shared Lingmo components (`LingmoUI.CompatibleModule`).

```
   QML pages (Media · Cut · Edit · Effects · Color · Audio · Deliver)
                         │ properties / Q_INVOKABLE calls
 ┌───────────────────────▼────────────────────────────────────────┐
 │ Editor (src/app/editor.*)  — the only controller QML talks to   │
 │  Document {Timeline, Sequences} ──modify()──▶ QUndoStack        │
 │  MediaItem list, ProjectSettings, autosave, recent projects     │
 └──┬──────────────┬──────────────┬─────────────┬─────────────────┘
    │              │              │             │
 TimelineModel  MediaBin       Player        RenderQueue / Scopes / Recorder
 (tracks for    (import,       (MLT preview)  (melt processes, frame
  QML)           proxies)                      analysis, ffmpeg capture)
    │              │              │             │
 ┌──▼──────────────▼──────────────▼─────────────▼─────────────────┐
 │ studio-core (src/core) — no QML, unit tested                     │
 │  model.h        value types: Clip, Track, Effect, Keyframe…      │
 │  timelineops.*  pure edit operations (insert, trim, roll, slip…) │
 │  effects.*      effect registry → MLT filter specs               │
 │  plan.*         Timeline → RenderPlan (MLT-shaped description)   │
 │  mltxml.*       RenderPlan → MLT XML (project files, export)     │
 │  mltbuilder.*   RenderPlan → live Mlt:: objects (preview)        │
 │  projectio.*    save/load .mlt (MLT xml producer for parsing)    │
 │  exporter.*     presets, melt command line, GPU encoder probe    │
 │  mediaprobe.*   media info, thumbnails, audio peaks, proxies     │
 │  aitools.*      whisper, silence/scene detection, loudness, SRT  │
 │  captions.*     word-timed captions: rules, styles, ASS/SRT/VTT  │
 │  highlights.*   Shorts candidates, heuristic + Ollama scoring    │
 │  platforms.*    data/platforms.json (sizes, limits, safe areas)  │
 │  reframe.*      subject path smoothing → Position X keyframes    │
 └──────────────────────────────────────────────────────────────────┘
```

The pages sit behind a page bar at the bottom of the window. The workspace
is always charcoal (`qml/StudioTheme.qml`, a singleton whose palette is also
handed to `LingmoUI.Theme`), and icons are Studio's own monochrome SVGs
(`data/icons`) tinted through `IconItem.color`. There is one MLT player: the
Edit page's source and timeline viewers share it, the inactive one holding
its last frame.

## Model

The document is a set of **plain value types** (`src/core/model.h`), not MLT
objects:

* `Timeline` — tracks in display order (subtitle, video top→bottom, audio) and
  markers.
* `Track` — kind (video/audio/subtitle), mute/hide/lock/solo, mixer volume/pan,
  clips sorted by position.
* `Clip` — `position`, `in`, `out` (frames), kind (`media`, `title`, `color`,
  `compound`), speed or speed ramp, freeze frame, fades, `transitionIn`
  (overlap with the previous clip), linked clip id (detached audio) and an
  ordered list of `Effect`s.
* `Effect` — registry id, params, keyframes. A keyframe stores all animatable
  values of the effect at a frame relative to the clip start; values are
  interpolated linearly.
* `Sequence` — a nested timeline used by compound clips.

Every edit runs a pure function from `timelineops.h` on a copy of the document
and pushes a snapshot command (`SnapshotCommand`, `editor.cpp`) on a
`QUndoStack`. Snapshots are cheap (implicitly shared Qt containers) and make
undo exact; slider drags merge into one step. The operations keep two
invariants: clips on a track never overlap except for a transition overlap,
and everything stays inside the source media. They are covered by
`tests/tst_timeline.cpp`.

## From the model to MLT

`Planner` (`plan.cpp`) turns a timeline into a `RenderPlan`: producers
(deduplicated by key), one playlist per track (blanks, cuts, and *mixes* — small
two-track tractors with a `luma` + `mix` transition for each transition
overlap), and the tractor's compositing (`mix` for audio of every track,
`qtblend` for every video track over a black background). The same plan is
used two ways:

* `mltxml.cpp` writes it as **MLT XML**. This is the project format (`.mlt`) and
  what export renders. `studio:*` properties carry what MLT has no notion of
  (clip JSON with effects and keyframes, media bin, markers, nested
  sequences); `melt`, Shotcut and Kdenlive ignore them.
* `mltbuilder.cpp` builds **live `Mlt::` objects** for the preview, caching
  media producers by key so a rebuild after an edit does not reopen files.

Loading (`projectio.cpp`) parses the file with MLT's own `xml` producer and walks
the tractor: track flags, cuts and mixes become clips again (the clip JSON
restores effects; files from other tools load structure only).

Effects (`effects.cpp`) map to MLT services: `qtblend` (transform/opacity),
`qtcrop` (crop, masks), `avfilter.eq` + `avfilter.colortemperature` (primaries),
`lift_gamma_gain` (wheels), `avfilter.curves`, `hslrange` (HSL qualifier),
`avfilter.lut3d` (.cube), `avfilter.gblur`, `frei0r.glow`, `avfilter.chromakey`,
`qtext` (titles/subtitles), `volume`, `panner`, `avfilter.equalizer`,
`avfilter.acompressor`, `avfilter.afftdn`, `dynamic_loudness`. Speed uses the
`timewarp` producer, speed ramps a `chain` with the `timeremap` link, freeze
frames the `freeze` filter, fades `brightness` + `volume` keyframes.

## Captions and Shorts

A caption is a clip of kind `caption` on a subtitle track. Its words live in
`genParams["words"]` with times in ms relative to the clip's source origin
(`position - in`): moving the clip moves the words, trimming an edge hides
words without losing them, and the pure operations in `timelineops.h`
(`setCaptionText`, `splitCaption`, `mergeCaptionWithNext`, `nudgeCaptionWord`,
`setCaptionSpeaker`, `toggleCaptionEmphasis`) keep the timing in sync. The
track's `captionStyle` is a `Captions::Style` map (preset + overrides + the
platform safe area + readability rules).

The planner turns each visible caption track into an ASS script
(`Captions::toAss`, times snapped to frames) written to the cache by content
hash, and adds an `avfilter.ass` filter to the tractor. Preview and export
therefore use the same renderer (libass); `tst_captions` renders the same
karaoke frames through the live builder and through melt and compares them.
The export sets the consumer frame rate explicitly: filters created while
melt loads a project otherwise time their frames with melt's default 25 fps.

`Transcriber` (app) runs ffmpeg + whisper-cli `-ojf` (or whisper-ctranslate2
`--word_timestamps True`) in the background, with progress and cancel, and
caches transcripts per file/model/language. `ShortsGenerator` drives "Gerar
cortes": transcript → silences, loudness curve, scene cuts (ffmpeg) →
candidates on sentence boundaries sized for the platforms → heuristic score →
Ollama `/api/chat` (`think:false`, JSON schema) for the best ones → cards.
`Editor::createShorts` makes one `Sequence` per pick (video reframed, caption
track) laid out on the main timeline as compound clips with markers;
`data/facetrack.py` (OpenCV) reports the face per sample and
`Editor::applyReframe` bakes the smoothed path into keyframes.
`Editor::exportShorts` renders each cut per platform, re-filling the frame
from the stored subject path when the platform's aspect differs.

## Rendering path (preview)

`Player` owns an `Mlt::Profile` scaled to the preview size (540p by default;
pixel-valued effect parameters are scaled with it — MLT's text filters misplace
text when only the consumer is scaled), and an `sdl2_audio` consumer with an
`audiolevel` filter. MLT's read-ahead thread renders each frame as RGBA; on
`consumer-frame-show` (consumer thread) the image is copied into a `QImage`
and posted to the GUI thread, where `VideoItem` (a `QQuickItem`, scene graph
texture) draws it and `Scopes` analyses it. Audio clocks the video. Scrubbing
seeks the producer and asks for a refresh; J/K/L set the producer speed.

Rebuilding the preview stops the consumer (the builder reuses producers the
consumer reads), swaps the tractor and restarts at the same frame.

## Threading

| Work | Where |
|---|---|
| Edits, model, MLT graph building | GUI thread |
| Playback, frame rendering | MLT consumer threads; frames handed over with queued calls |
| Media probing, thumbnails, waveforms | `QtConcurrent` / image-provider thread pool, each with its own MLT producer |
| Scopes | `QtConcurrent`, throttled to ~10 Hz, only on the Color page |
| Proxies, silence/scene detection, loudness, whisper | `ffmpeg`/whisper child processes |
| Export | `melt` child process per job (render queue); a crash or cancel never touches the editor; GPU failure re-runs on the CPU |
| GPU encoder detection | `QtConcurrent` at startup: `ffmpeg -encoders` plus a 2-frame test encode per candidate (NVENC, VA-API) |

## Files and state

* Projects: `.mlt` (MLT XML), absolute media paths.
* Autosave: every minute while dirty, to `~/.local/share/lingmoos/lingmo-studio/autosave`;
  removed on save or clean exit, offered for recovery at the next start.
* Proxies: `~/.cache/lingmoos/lingmo-studio/proxies` (all-intra H.264, 540p).
* Settings: `~/.config/lingmoos/lingmo-studio.conf` (recent projects, proxy
  options, preview size, dark interface).

## Tests

`ctest` in the build folder (needs ffmpeg and melt; media is generated):

* `tst_timeline` — every edit operation, keyframe interpolation.
* `tst_projectio` — save/load round trip, live MLT build renders frames, every
  registered effect builds, speed ramp, freeze frame, foreign MLT files.
* `tst_export` — presets, GPU detection with a fake ffmpeg, melt command line,
  real renders (H.264/AAC MP4 with frame count, VP9, ProRes, DNxHR, GIF, MP3,
  AV1) checked with ffprobe.
* `tst_aitools` — SRT, transcript search, silence/scene/loudness detection.
* `tst_captions` — whisper.cpp/faster-whisper JSON, cue building and line
  breaks under the readability rules, ASS (karaoke timings, frame snapping,
  every style), sidecars, caption edits, project round trip, and rendered
  frames vs. word times in preview and export.
* `tst_shorts` — platform presets, sentences, candidates fitted to the
  length, heuristic scoring and selection, Ollama request/answer (a real call
  with `STUDIO_TEST_OLLAMA_MODEL=<model>`), reframe smoothing and keyframes.
* `tst_shortsflow` — the cuts flow through the controller (sequences,
  captions, keyframes, batch export at 9:16 and 1:1, style previews). With
  `STUDIO_E2E_MEDIA=<video>` and `STUDIO_TEST_HOME=<scratch home with ai.conf
  and a model>` it runs whisper, ffmpeg, Ollama, OpenCV and melt for real.
* `tst_editor` — the controller as QML uses it: import, undo/redo/history,
  keyframes, compound clips, save/open, autosave recovery, playback, analysis
  tools, proxies, GPU→CPU fallback, export through the render queue, templates,
  9:16 reframe. Runs with a scratch HOME and no audio device.

## Roadmap (beyond v1)

* **Color page**: GPU (movit/libplacebo) color pipeline in 10/16-bit, node
  graph for grades (serial/parallel/layer nodes), power windows with tracking,
  qualifier mask view, waveform/vectorscope on the full-resolution frame,
  color-managed output (ACES/OCIO), HDR scopes.
* **Effects**: node-based compositor (Fusion-like) on top of MLT transitions,
  bezier keyframe curves (MLT supports smooth/bezier keyframe types), roto
  masks (`rotoscoping`), motion tracking (OpenCV tracker filter), stabilisation
  analysis pass inside the app (vid.stab two-pass).
* **Edit**: multicam clips (synced by audio), dual-viewer trim mode,
  three-point edits from the source monitor with track targeting, nested
  sequence tabs, timeline thumbnails/waveforms cached on disk.
* **Audio (Fairlight-like)**: per-track effect chains and buses, real-time
  meters per track, automation lanes, LADSPA/LV2 plugins, ADR/voice-over
  takes, loudness-compliant mastering (EBU R128 report).
* **AI (local)**: subject-aware reframing (face/object tracking), smart
  B-roll suggestions with a local LLM (Ollama), filler-word removal from
  transcripts, speaker diarisation.
* **Delivery**: two-pass encodes, render in background service, chapters and
  subtitles muxed into MP4/MKV, batch renders, frame-server.
