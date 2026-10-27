import json
import os
from pathlib import Path
import sys
import sysconfig
import time
import traceback


def emit_response(**response):
    print(json.dumps(response, ensure_ascii=False), flush=True)


def describe_error(error):
    detail = str(error)
    if "out of memory" in detail.lower():
        return f"Brak pamięci GPU. Zamknij inne modele korzystające z GPU. {detail}"
    if "cuda" in detail.lower() or "cudnn" in detail.lower():
        return f"Błąd CUDA/cuDNN. Sprawdź sterownik i runtime BuzzASR. {detail}"
    return f"{type(error).__name__}: {detail}"


def configure_cudnn():
    cudnn_dir = str(Path(sysconfig.get_path("purelib")) / "nvidia/cudnn/lib")
    library_path = os.environ.get("LD_LIBRARY_PATH", "")
    if Path(cudnn_dir).is_dir() and cudnn_dir not in library_path.split(os.pathsep):
        os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [cudnn_dir, library_path]))
        os.execv(sys.executable, [sys.executable, *sys.argv])


def main():
    try:
        configure_cudnn()
        import soundfile as sf
        import torch
        from transformers import WhisperForConditionalGeneration, WhisperProcessor

        if not torch.cuda.is_available():
            raise RuntimeError("CUDA is unavailable")
        model = WhisperForConditionalGeneration.from_pretrained(
            "BuzzASR/polish", torch_dtype=torch.float16
        ).to("cuda").eval()
        processor = WhisperProcessor.from_pretrained("BuzzASR/polish")
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
                    wav, sample_rate = sf.read(request["wav_path"], dtype="float32")
                    features = processor(
                        wav, sampling_rate=sample_rate, return_tensors="pt"
                    ).input_features.to("cuda").half()
                    # The model's replacement tokenizer does not reliably support
                    # Whisper prompt_ids. Keep the model card's decoding settings.
                    with torch.inference_mode():
                        ids = model.generate(
                            features, num_beams=1, no_repeat_ngram_size=3,
                            repetition_penalty=1.2
                        )
                    emit_response(text=processor.batch_decode(ids, skip_special_tokens=True)[0].strip())
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
