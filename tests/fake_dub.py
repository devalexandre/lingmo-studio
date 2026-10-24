#!/usr/bin/env python3
# A stand-in for data/dub.py (same protocol, no PyTorch): the "voice" is a tone
# as long as the text takes to say. Used by tst_dubflow.
import array
import json
import math
import os
import sys
import time
import wave

SR = 24000


def emit(event, **fields):
    fields["event"] = event
    sys.stdout.write(json.dumps(fields) + "\n")
    sys.stdout.flush()


def write(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(array.array("h", samples).tobytes())


def read(path):
    with wave.open(path, "rb") as w:
        data = array.array("h")
        data.frombytes(w.readframes(w.getnframes()))
        return list(data)


def main():
    cmd = sys.argv[1]
    if cmd == "check":
        emit("ready", ready=True, v3=True, device="cuda", gpu="Fake GPU", vramGb=8.0, chatterbox=True)
        return 0
    if cmd == "synth":
        job = json.load(open(sys.argv[2]))
        os.makedirs(job["out_dir"], exist_ok=True)
        results = {}
        if os.environ.get("FAKE_DUB_FAIL"):
            emit("error", message="boom")
            return 1
        for i, seg in enumerate(job["segments"]):
            if os.environ.get("FAKE_DUB_SLOW"):
                time.sleep(float(os.environ["FAKE_DUB_SLOW"]))
            seconds = max(0.4, len(seg["text"]) / float(os.environ.get("FAKE_DUB_CPS", "15")))
            n = int(seconds * SR)
            samples = [int(9000 * math.sin(2 * math.pi * 220 * t / SR)) for t in range(n)]
            path = os.path.join(job["out_dir"], seg["id"] + ".wav")
            write(path, samples)
            results[seg["id"]] = {"file": path, "seconds": round(seconds, 3)}
            emit("progress", done=i + 1, total=len(job["segments"]), id=seg["id"], seconds=seconds, cached=False, device="cuda")
        json.dump(results, open(os.path.join(job["out_dir"], "result.json"), "w"))
        json.dump(job, open(os.path.join(job["out_dir"], "job-seen.json"), "w"))
        emit("done", count=len(job["segments"]))
        return 0
    if cmd == "assemble":
        plan = json.load(open(sys.argv[2]))
        total = int(plan["length"] * SR)
        track = [0] * total
        for it in plan["items"]:
            x = read(it["file"])
            n = int(len(x) * it["ratio"])
            start = int(it["start"] * SR)
            for k in range(n):
                if start + k < total:
                    track[start + k] += x[min(len(x) - 1, int(k / it["ratio"]))]
        write(plan["output"], [max(-32000, min(32000, v)) for v in track])
        json.dump(plan, open(os.path.join(os.path.dirname(plan["output"]), "plan-seen.json"), "w"))
        emit("done", seconds=plan["length"])
        return 0
    if cmd == "warmup":
        emit("done", model="v3")
        return 0
    return 2


sys.exit(main())
