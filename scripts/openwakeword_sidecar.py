#!/usr/bin/env python3

import argparse
import json
import os
import sys
from pathlib import Path

def emit(event, **payload):
    message = {"event": event}
    message.update(payload)
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def find_builtin_model(phrase):
    normalized_phrase = phrase.strip().lower()
    try:
        import openwakeword
    except ImportError:
        return None

    resources_root = Path(openwakeword.__file__).resolve().parent / "resources" / "models"
    if not resources_root.exists():
        return None

    candidates = [
        f"{normalized_phrase}_v0.1.tflite",
        f"{normalized_phrase}.tflite",
        f"{normalized_phrase}_v0.1.onnx",
        f"{normalized_phrase}.onnx",
    ]

    for candidate in candidates:
        path = resources_root / candidate
        if path.exists():
            return str(path)

    for path in resources_root.glob(f"{normalized_phrase}*.tflite"):
        if path.exists():
            return str(path)
    for path in resources_root.glob(f"{normalized_phrase}*.onnx"):
        if path.exists():
            return str(path)

    return None


def select_model_path(phrase, configured_path):
    if configured_path:
        candidate = os.path.expanduser(configured_path)
        if os.path.exists(candidate):
            return candidate
        raise RuntimeError(f"Configured wake-word model does not exist: {candidate}")

    builtin_model = find_builtin_model(phrase)
    if builtin_model is not None:
        return builtin_model

    raise RuntimeError(
        "Could not resolve built-in wake-word model. Install openwakeword and verify resources/models includes "
        f"a model for '{phrase}'."
    )

def inference_framework_for(model_path):
    suffix = Path(model_path).suffix.lower()
    if suffix == ".onnx":
        return "onnx"
    if suffix == ".tflite":
        return "tflite"
    raise RuntimeError(f"Unsupported model extension in path: {model_path}")


def parse_args():
    parser = argparse.ArgumentParser(description="openWakeWord sidecar for speak-to-computer")
    parser.add_argument("--phrase", required=True, help="Wake-word phrase identifier")
    parser.add_argument("--threshold", required=True, type=float, help="Detection threshold from 0 to 1")
    parser.add_argument("--model-path", default="", help="Optional custom ONNX/TFLite model path")
    return parser.parse_args()


def main():
    args = parse_args()

    if args.threshold <= 0.0 or args.threshold > 1.0:
        emit("error", message="Threshold must be in range (0.0, 1.0].")
        return 2

    try:
        from openwakeword.model import Model
    except ImportError:
        emit("error", message="Python package 'openwakeword' is not installed in sidecar environment.")
        return 3

    try:
        import numpy as np

        model_path = select_model_path(args.phrase, args.model_path)
        inference_framework = inference_framework_for(model_path)
        model = Model(wakeword_models=[model_path], inference_framework=inference_framework)
    except Exception as error:
        emit("error", message=f"Wake-word initialization failed: {error}")
        return 4

    emit("ready", phrase=args.phrase, model_path=model_path, threshold=args.threshold,
         inference_framework=inference_framework)

    # 16kHz, 16-bit PCM mono. openWakeWord works best with chunks >= 80ms (1280 samples).
    bytes_per_sample = 2
    frame_samples = 1280
    frame_bytes = frame_samples * bytes_per_sample

    buffered = b""
    try:
        while True:
            chunk = sys.stdin.buffer.read(frame_bytes)
            if not chunk:
                break
            buffered += chunk

            while len(buffered) >= frame_bytes:
                frame = buffered[:frame_bytes]
                buffered = buffered[frame_bytes:]
                samples = np.frombuffer(frame, dtype=np.int16)
                predictions = model.predict(samples)
                score = 0.0
                for value in predictions.values():
                    try:
                        score = max(score, float(value))
                    except (TypeError, ValueError):
                        continue
                if score >= args.threshold:
                    emit("detected", score=score)
    except Exception as error:
        emit("error", message=f"Wake-word processing failed: {error}")
        return 5

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
