#!/usr/bin/env python3
# Copyright (C) 2026 LingmoOS Team. GPL-3.0-or-later.
#
# Lingmo Studio subject tracker for automatic reframing (9:16 and friends).
# Samples a range of a video and prints one JSON object per sample:
#   {"t": <seconds>, "x": <subject centre 0..1 or null>, "w": <width 0..1>, "c": <confidence>}
# Uses OpenCV when installed (python-opencv): the YuNet DNN face detector when
# its model is present, otherwise the Haar cascade shipped with OpenCV.
# Exit code 3 = OpenCV is not installed (the editor then centres the crop).

import argparse
import glob
import json
import os
import sys

try:
    import cv2
except Exception:  # noqa: BLE001 - any import problem means "not available"
    print(json.dumps({"error": "opencv-missing"}), flush=True)
    sys.exit(3)


def find_cascade():
    names = []
    data = getattr(cv2, "data", None)
    if data is not None and getattr(data, "haarcascades", None):
        names.append(os.path.join(data.haarcascades, "haarcascade_frontalface_default.xml"))
    names += glob.glob("/usr/share/opencv*/haarcascades/haarcascade_frontalface_default.xml")
    names += glob.glob("/usr/local/share/opencv*/haarcascades/haarcascade_frontalface_default.xml")
    for n in names:
        if os.path.exists(n):
            return n
    return None


def find_yunet():
    dirs = [os.path.expanduser("~/.local/share/lingmo/models"), "/usr/share/lingmo-studio/models"]
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, "face_detection_yunet*.onnx"))):
            return f
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--duration", type=float, default=0.0)
    ap.add_argument("--fps", type=float, default=4.0)
    ap.add_argument("input")
    a = ap.parse_args()

    cap = cv2.VideoCapture(a.input)
    if not cap.isOpened():
        print(json.dumps({"error": "cannot-open"}), flush=True)
        return 2
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    total = cap.get(cv2.CAP_PROP_FRAME_COUNT) / fps if cap.get(cv2.CAP_PROP_FRAME_COUNT) > 0 else 0
    end = a.start + a.duration if a.duration > 0 else total
    step = 1.0 / max(0.5, a.fps)

    yunet = None
    model = find_yunet()
    if model and hasattr(cv2, "FaceDetectorYN"):
        try:
            yunet = cv2.FaceDetectorYN.create(model, "", (320, 320), 0.6, 0.3, 5000)
        except Exception:  # noqa: BLE001
            yunet = None
    cascade = None
    if yunet is None:
        path = find_cascade()
        if not path:
            print(json.dumps({"error": "no-detector"}), flush=True)
            return 4
        cascade = cv2.CascadeClassifier(path)

    prev = None
    t = a.start
    while t <= end + 1e-6:
        cap.set(cv2.CAP_PROP_POS_MSEC, t * 1000.0)
        ok, frame = cap.read()
        if not ok:
            break
        h, w = frame.shape[:2]
        scale = 480.0 / max(w, h)
        small = cv2.resize(frame, (int(w * scale), int(h * scale)))
        faces = []
        if yunet is not None:
            yunet.setInputSize((small.shape[1], small.shape[0]))
            _, found = yunet.detect(small)
            if found is not None:
                for f in found:
                    faces.append((f[0], f[1], f[2], f[3], float(f[-1])))
        else:
            gray = cv2.equalizeHist(cv2.cvtColor(small, cv2.COLOR_BGR2GRAY))
            for (x, y, fw, fh) in cascade.detectMultiScale(gray, 1.1, 5, minSize=(24, 24)):
                faces.append((x, y, fw, fh, 0.8))
        best = None
        if faces:
            sw = small.shape[1]

            def rank(f):
                cx = (f[0] + f[2] / 2.0) / sw
                size = f[2] * f[3]
                near = 0.0 if prev is None else abs(cx - prev)
                return size * f[4] * (1.0 - min(near, 0.9))

            best = max(faces, key=rank)
        if best is not None:
            cx = (best[0] + best[2] / 2.0) / small.shape[1]
            prev = cx
            out = {"t": round(t, 3), "x": round(cx, 4), "w": round(best[2] / small.shape[1], 4), "c": round(best[4], 3)}
        else:
            out = {"t": round(t, 3), "x": None}
        print(json.dumps(out), flush=True)
        t += step
    return 0


if __name__ == "__main__":
    sys.exit(main())
