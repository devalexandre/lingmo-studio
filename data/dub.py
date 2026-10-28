#!/usr/bin/env python3
# Copyright (C) 2026 LingmoOS Team.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
"""Dubbing helper of Lingmo Studio.

Runs inside the dubbing virtualenv (Chatterbox Multilingual TTS, MIT). The
studio drives it with JSON files and reads JSON lines from stdout:

  dub.py check                 what this machine can do
  dub.py synth    job.json     speak each text in the cloned voice
  dub.py assemble plan.json    lay the spoken pieces on one timeline-long track
  dub.py warmup [v3|v2]        download and load the model once

Events (one JSON object per line on stdout):
  {"event": "ready", "device": "cuda", ...}          check
  {"event": "progress", "done": 3, "total": 12, "id": "c3", "seconds": 4.2, "cached": false}
  {"event": "log", "message": "..."}
  {"event": "done", ...}
  {"event": "error", "message": "..."}               (exit status 1)

Everything is deterministic for a given seed, and every spoken piece is cached
by its content, so a cancelled run continues where it stopped.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import wave

SR = 24000  # Chatterbox's sample rate


class NoWatermark:
    """Chatterbox stamps its output with Perth's inaudible watermark: Lingmo writes nothing
    into the audio, so the user can publish it wherever they like."""

    def apply_watermark(self, wav, sample_rate=None):
        return wav


def emit(event, **fields):
    fields["event"] = event
    sys.stdout.write(json.dumps(fields, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def log(message):
    emit("log", message=str(message))


# ------------------------------------------------------------------ audio files


def read_wav(path):
    """float32 mono samples and the sample rate."""
    import numpy as np

    try:
        import soundfile as sf

        data, sr = sf.read(path, dtype="float32", always_2d=True)
        return data.mean(axis=1).astype(np.float32), sr
    except ImportError:
        with wave.open(path, "rb") as w:
            sr, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
            raw = w.readframes(w.getnframes())
        if width != 2:
            raise RuntimeError("only 16-bit wav without soundfile")
        data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
        if ch > 1:
            data = data.reshape(-1, ch).mean(axis=1)
        return data, sr


def write_wav(path, samples, sr):
    import numpy as np

    samples = np.clip(samples, -1.0, 1.0).astype(np.float32)
    try:
        import soundfile as sf

        # explicit format: callers write to "<name>.wav.part" first, which libsndfile cannot guess
        sf.write(path, samples, sr, format="WAV", subtype="FLOAT")
    except ImportError:
        pcm = (samples * 32767.0).astype("<i2")
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes(pcm.tobytes())


def trim_silence(x, sr, floor_db=-42.0, margin_ms=45, fade_ms=8):
    """Cuts the silence the model leaves around a sentence, keeps a short margin."""
    import numpy as np

    if x.size == 0:
        return x
    win = max(1, int(sr * 0.01))
    n = x.size // win
    if n == 0:
        return x
    frames = np.abs(x[: n * win]).reshape(n, win).max(axis=1)
    peak = float(frames.max())
    if peak <= 1e-6:
        return x
    loud = np.where(frames > peak * (10.0 ** (floor_db / 20.0)))[0]
    if loud.size == 0:
        return x
    margin = int(sr * margin_ms / 1000)
    start = max(0, int(loud[0]) * win - margin)
    end = min(x.size, (int(loud[-1]) + 1) * win + margin)
    y = x[start:end].copy()
    fade = min(int(sr * fade_ms / 1000), y.size // 2)
    if fade > 1:
        ramp = np.linspace(0.0, 1.0, fade, dtype=np.float32)
        y[:fade] *= ramp
        y[-fade:] *= ramp[::-1]
    return y


def rms(x):
    import numpy as np

    return float(np.sqrt(np.mean(np.square(x)))) if x.size else 0.0


# ------------------------------------------------------------------ check


def cmd_check(_args):
    info = {"python": sys.version.split()[0], "chatterbox": False, "v3": False, "torch": "", "cuda": False, "device": "cpu", "gpu": "", "vramGb": 0.0}
    try:
        import torch

        info["torch"] = torch.__version__
    except Exception as e:  # noqa: BLE001
        info["problem"] = "torch: %s" % e
        emit("ready", ready=False, **info)
        return 0
    # The engine is installed whatever the GPU says: a full card (an Ollama model or a
    # game holding the memory) is reported apart, not as a missing install
    try:
        info["cuda"] = bool(torch.cuda.is_available())
        if info["cuda"]:
            info["device"] = "cuda"
            free, total = torch.cuda.mem_get_info()
            info["gpu"] = torch.cuda.get_device_name(0)
            info["vramGb"] = round(total / 1e9, 1)
            info["freeVramGb"] = round(free / 1e9, 1)
            info["gpuBusy"] = free < 4.2e9
    except Exception as e:  # noqa: BLE001
        info["gpuBusy"] = True
        info["gpuNote"] = str(e).splitlines()[0] if str(e) else "CUDA error"
        try:
            out = subprocess.run(["nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader,nounits"],
                                 capture_output=True, text=True, timeout=10).stdout.strip().splitlines()
            if out:
                name, mib = out[0].rsplit(",", 1)
                info["gpu"] = name.strip()
                info["vramGb"] = round(float(mib) / 1024, 1)
        except Exception:  # noqa: BLE001
            pass
    try:
        import inspect

        from chatterbox.mtl_tts import ChatterboxMultilingualTTS

        info["chatterbox"] = True
        info["v3"] = "t3_model" in inspect.signature(ChatterboxMultilingualTTS.from_pretrained).parameters
    except Exception as e:  # noqa: BLE001
        info["problem"] = "chatterbox: %s" % e
    info["numpy"] = True
    emit("ready", ready=bool(info["chatterbox"]), **info)
    return 0


# ------------------------------------------------------------------ synth


class Voice:
    """The model, loaded once, on the GPU when it fits (else the CPU)."""

    def __init__(self, job):
        self.job = job
        self.model = None
        self.device = None
        self.version = "v2"
        self.prepared = None

    def pick_device(self):
        import torch

        want = self.job.get("device", "auto")
        if want in ("cuda", "cpu"):
            return want
        if torch.cuda.is_available():
            try:
                # another program (an Ollama model still loaded, a game) may be freeing
                # the GPU right now: give it up to 30 s before falling back to the CPU
                import time

                free = 0
                for _ in range(16):
                    free, _total = torch.cuda.mem_get_info()
                    if free >= 4.2e9:
                        return "cuda"
                    time.sleep(2)
                log("only %.1f GB of video memory free: using the CPU (much slower)" % (free / 1e9))
            except Exception:  # noqa: BLE001
                return "cuda"
        return "cpu"

    def load(self, device=None):
        import inspect

        import torch
        from chatterbox.mtl_tts import ChatterboxMultilingualTTS

        self.device = device or self.pick_device()
        kwargs = {}
        if "t3_model" in inspect.signature(ChatterboxMultilingualTTS.from_pretrained).parameters and self.job.get("model", "v3") in ("v3", "v2"):
            kwargs["t3_model"] = self.job.get("model", "v3")
            self.version = kwargs["t3_model"]
        log("loading Chatterbox %s on %s" % (self.version, self.device))
        started = __import__("time").time()
        # a plain string: chatterbox compares `device in ["cpu", "mps"]` to pick map_location,
        # which a torch.device never matches (the CUDA-saved weights then fail to load on the CPU)
        self.model = ChatterboxMultilingualTTS.from_pretrained(device=self.device, **kwargs)
        self.model.watermarker = NoWatermark()
        self.prepared = None
        log("model ready in %.0f s" % (__import__("time").time() - started))

    def prepare(self, reference, exaggeration):
        self.model.prepare_conditionals(reference, exaggeration=exaggeration)
        self.prepared = (reference, exaggeration)

    def generate(self, text, language, seed, exaggeration, cfg, temperature):
        import torch

        if self.prepared is None or self.prepared[1] != exaggeration:
            self.prepare(self.job["reference"], exaggeration)
        torch.manual_seed(seed)
        if self.device == "cuda":
            torch.cuda.manual_seed_all(seed)
        wav = self.model.generate(text, language_id=language, audio_prompt_path=None, exaggeration=exaggeration, cfg_weight=cfg, temperature=temperature)
        return wav.squeeze().detach().cpu().numpy().astype("float32")


def split_sentences(text, limit=300):
    """Long texts are spoken sentence by sentence (the model drifts on long inputs)."""
    import re

    text = " ".join(text.split())
    if len(text) <= limit:
        return [text]
    parts = re.split(r"(?<=[.!?…。！？])\s+", text)
    out, cur = [], ""
    for p in parts:
        if cur and len(cur) + 1 + len(p) > limit:
            out.append(cur)
            cur = p
        else:
            cur = (cur + " " + p).strip()
    if cur:
        out.append(cur)
    return out


def clean_text(text):
    text = " ".join(text.split())
    if text and text[-1] not in ".!?…。！？":
        text += "."
    return text


def cache_key(job, ref_hash, version, text, seed_base):
    h = hashlib.sha1()
    for part in (version, job["language"], ref_hash, text, str(job.get("exaggeration", 0.5)), str(job.get("cfg_weight", 0.0)),
                 str(job.get("temperature", 0.8)), str(seed_base), str(job.get("attempts", 2))):
        h.update(part.encode("utf-8"))
        h.update(b"\0")
    return h.hexdigest()[:24]


def is_cuda_oom(e):
    return "out of memory" in str(e).lower()


def speak(voice, job, text, seed_base, slot):
    """One sentence-sized text → samples. Several seeds, the most natural wins."""
    import numpy as np

    exaggeration = float(job.get("exaggeration", 0.5))
    cfg = float(job.get("cfg_weight", 0.0))
    temperature = float(job.get("temperature", 0.8))
    attempts = max(1, int(job.get("attempts", 2)))
    best, best_score = None, 1e9
    for attempt in range(attempts):
        seed = seed_base + attempt * 7919
        try:
            x = voice.generate(text, job["language"], seed, exaggeration, cfg, temperature)
        except RuntimeError as e:
            if voice.device == "cuda" and is_cuda_oom(e):
                import torch

                log("video memory ran out: continuing on the CPU")
                voice.model = None
                torch.cuda.empty_cache()
                voice.load("cpu")
                x = voice.generate(text, job["language"], seed, exaggeration, cfg, temperature)
            else:
                raise
        x = trim_silence(x, SR)
        seconds = x.size / SR
        if seconds <= 0.05:
            continue
        cps = max(len(text), 1) / seconds
        # natural speech is 8-22 characters a second; far outside means a cut-off or a ramble
        score = abs(np.log(cps / 14.0))
        if slot and seconds > slot * 1.5:
            score += 0.3
        if 6.0 <= cps <= 30.0 and score < best_score:
            best, best_score = x, score
            if score < 0.35 and not (slot and seconds > slot * 1.35):
                break
        elif best is None and score < best_score:
            best, best_score = x, score + 5
    return best if best is not None else np.zeros(int(SR * 0.3), dtype="float32")


def cmd_synth(args):
    import numpy as np

    with open(args[0], encoding="utf-8") as f:
        job = json.load(f)
    out_dir = job["out_dir"]
    cache_dir = job.get("cache_dir", out_dir)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(cache_dir, exist_ok=True)
    segments = job["segments"]
    with open(job["reference"], "rb") as f:
        ref_hash = hashlib.sha1(f.read()).hexdigest()[:16]

    voice = None
    results = {}
    total = len(segments)
    seed0 = int(job.get("seed", 1234))
    for i, seg in enumerate(segments):
        text = clean_text(seg["text"])
        key = cache_key(job, ref_hash, job.get("model", "v3"), text, seed0 + i * 101)
        path = os.path.join(cache_dir, key + ".wav")
        cached = os.path.exists(path)
        if not cached:
            # nothing to say (empty, or only punctuation): the model would invent speech
            # (empty text is replaced by a canned sentence in chatterbox), so stay silent
            parts = [p for p in split_sentences(text) if any(c.isalnum() for c in p)]
            if parts and voice is None:
                from chatterbox.mtl_tts import SUPPORTED_LANGUAGES

                if str(job["language"]).lower() not in SUPPORTED_LANGUAGES:
                    raise ValueError("unsupported language %r (supported: %s)" % (job["language"], ", ".join(sorted(SUPPORTED_LANGUAGES))))
                voice = Voice(job)
                voice.load()
                voice.prepare(job["reference"], float(job.get("exaggeration", 0.5)))
            pieces = [speak(voice, job, part, seed0 + i * 101, seg.get("slot") if len(parts) == 1 else None)
                      for part in parts]
            if not pieces:
                log("segment %s has no speakable text: silence" % seg["id"])
                pieces = [np.zeros(int(SR * 0.3), dtype="float32")]
            gap = np.zeros(int(SR * 0.12), dtype="float32")
            samples = pieces[0]
            for p in pieces[1:]:
                samples = np.concatenate([samples, gap, p])
            tmp = path + ".part"
            write_wav(tmp, samples, SR)
            os.replace(tmp, path)
            seconds = samples.size / SR
        else:
            x, sr = read_wav(path)
            seconds = x.size / sr
        results[seg["id"]] = {"file": path, "seconds": round(seconds, 3)}
        emit("progress", done=i + 1, total=total, id=seg["id"], seconds=round(seconds, 3), cached=cached,
             device=voice.device if voice else "cache")
    with open(os.path.join(out_dir, "result.json"), "w", encoding="utf-8") as f:
        json.dump(results, f)
    emit("done", count=total)
    return 0


# ------------------------------------------------------------------ assemble


def stretch(path, ratio, workdir):
    """Same pitch, `ratio` times the duration (rubberband R3, else ffmpeg atempo)."""
    out = os.path.join(workdir, "s%s.wav" % hashlib.sha1((path + str(ratio)).encode()).hexdigest()[:10])
    rb = shutil.which("rubberband")
    if rb:
        r = subprocess.run([rb, "-t", "%.5f" % ratio, "-3", path, out], capture_output=True)
        if r.returncode == 0 and os.path.exists(out):
            return out
    ff = shutil.which("ffmpeg")
    if ff:
        tempo = 1.0 / ratio
        r = subprocess.run([ff, "-hide_banner", "-loglevel", "error", "-y", "-i", path, "-filter:a", "atempo=%.5f" % tempo, out], capture_output=True)
        if r.returncode == 0 and os.path.exists(out):
            return out
    return path


def cmd_assemble(args):
    import numpy as np

    with open(args[0], encoding="utf-8") as f:
        plan = json.load(f)
    length = float(plan["length"])
    items = plan["items"]
    total = int(length * SR) + SR
    track = np.zeros(total, dtype=np.float32)
    levels = []
    loaded = []
    with tempfile.TemporaryDirectory() as work:
        for i, it in enumerate(items):
            path = it["file"]
            ratio = float(it.get("ratio", 1.0))
            if abs(ratio - 1.0) > 0.015:
                path = stretch(path, ratio, work)
            x, sr = read_wav(path)
            if sr != SR:
                idx = np.linspace(0, x.size - 1, int(x.size * SR / sr)).astype(np.float64)
                x = np.interp(idx, np.arange(x.size), x).astype(np.float32)
            loaded.append(x)
            if rms(x) > 1e-5:
                levels.append(rms(x))
            emit("progress", done=i + 1, total=len(items), stage="stretch")
    # one even level for every piece: the median of the voice
    target = float(np.median(levels)) if levels else 0.1
    fade = int(SR * 0.012)
    for it, x in zip(items, loaded):
        if x.size == 0:
            continue
        gain = target / rms(x) if rms(x) > 1e-5 else 1.0
        y = (x * min(gain, 4.0)).astype(np.float32)
        f = min(fade, y.size // 2)
        if f > 1:
            ramp = np.linspace(0.0, 1.0, f, dtype=np.float32)
            y[:f] *= ramp
            y[-f:] *= ramp[::-1]
        start = int(float(it["start"]) * SR)
        if start >= total:
            continue
        end = min(total, start + y.size)
        track[start:end] += y[: end - start]
    track = track[: int(length * SR)]
    peak = float(np.max(np.abs(track))) if track.size else 0.0
    if peak > 0.89:
        track *= 0.89 / peak  # headroom for the encoder
    write_wav(plan["output"], track, SR)
    emit("done", seconds=round(track.size / SR, 3), peak=round(peak, 3))
    return 0


def cmd_warmup(args):
    """Downloads and loads the model once, so the first dub does not wait for it."""
    voice = Voice({"model": args[0] if args else "v3", "device": "cpu"})
    voice.load("cpu")
    emit("done", model=voice.version)
    return 0


COMMANDS = {"check": cmd_check, "synth": cmd_synth, "assemble": cmd_assemble, "warmup": cmd_warmup}


def main(argv):
    if len(argv) < 2 or argv[1] not in COMMANDS:
        sys.stderr.write(__doc__)
        return 2
    try:
        return COMMANDS[argv[1]](argv[2:])
    except Exception as e:  # noqa: BLE001
        import traceback

        traceback.print_exc(file=sys.stderr)
        emit("error", message="%s: %s" % (type(e).__name__, e))
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
