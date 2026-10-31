import json
import os
from pathlib import Path
import sys
import time
import traceback


MODEL_NAME = "nemo-parakeet-tdt-0.6b-v3"
DATA_HOME = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share")
MODEL_DIR = DATA_HOME / "speak-to-computer" / "parakeet" / "models" / MODEL_NAME
PROVIDERS = ["CUDAExecutionProvider", "CPUExecutionProvider"]


def emit_response(**response):
    print(json.dumps(response, ensure_ascii=False), flush=True)


def describe_error(error):
    detail = str(error)
    if "out of memory" in detail.lower():
        return f"Brak pamięci GPU. Zamknij inne programy korzystające z GPU. {detail}"
    if "cuda" in detail.lower() or "cudnn" in detail.lower():
        return f"Błąd CUDA/cuDNN. Sprawdź sterownik i uruchom scripts/install-parakeet-runtime.sh. {detail}"
    return f"{type(error).__name__}: {detail}"


def load_model():
    import onnx_asr
    import onnxruntime

    # CUDA and cuDNN come from pip wheels in the runtime venv, not from /usr/local/cuda.
    onnxruntime.preload_dlls()
    # A CUDA session for the resampler failed with CUDNN_STATUS_INTERNAL_ERROR, so it stays on CPU.
    return onnx_asr.load_model(
        MODEL_NAME,
        str(MODEL_DIR),
        providers=PROVIDERS,
        resampler_config={"providers": ["CPUExecutionProvider"]},
    )


def warm_up(model):
    import numpy as np

    model.recognize(np.zeros(16000, dtype=np.float32), sample_rate=16000)


def main():
    try:
        model = load_model()
        warm_up(model)
        with open(os.environ["INBOX"], "a+", encoding="utf-8") as inbox:
            inbox.seek(0, os.SEEK_END)
            emit_response(ready=True)
            while True:
                raw = inbox.readline()
                if not raw:
                    time.sleep(0.1)
                    continue
                try:
                    request = json.loads(raw)
                    emit_response(text=model.recognize(request["wav_path"]).strip())
                except Exception as error:
                    traceback.print_exc()
                    emit_response(error=describe_error(error))
    except Exception as error:
        traceback.print_exc()
        emit_response(error=describe_error(error))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
