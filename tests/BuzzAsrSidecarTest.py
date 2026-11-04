import contextlib
import importlib.util
import io
import json
from pathlib import Path
import types
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "buzzasr_transcribe", Path(__file__).resolve().parents[1] / "scripts/buzzasr_transcribe.py"
)
sidecar = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sidecar)


class BuzzAsrSidecarTest(unittest.TestCase):
    def test_out_of_memory_explains_how_to_recover(self):
        message = sidecar.describe_error(RuntimeError("CUDA out of memory"))
        self.assertIn("Brak pamięci GPU", message)
        self.assertIn("CUDA out of memory", message)

    def test_cudnn_error_retains_original_cause(self):
        message = sidecar.describe_error(RuntimeError("CUDNN_STATUS_SUBLIBRARY_LOADING_FAILED"))
        self.assertIn("CUDA/cuDNN", message)
        self.assertIn("CUDNN_STATUS_SUBLIBRARY_LOADING_FAILED", message)

    def test_multiline_unicode_text_is_one_response(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            sidecar.emit_response(text="Zażółć\ngęślą jaźń")
        self.assertEqual(len(output.getvalue().splitlines()), 1)
        self.assertEqual(json.loads(output.getvalue())["text"], "Zażółć\ngęślą jaźń")

    def test_missing_cuda_returns_error_before_ready(self):
        torch = types.SimpleNamespace(cuda=types.SimpleNamespace(is_available=lambda: False))
        transformers = types.SimpleNamespace(WhisperForConditionalGeneration=None, WhisperProcessor=None)
        output = io.StringIO()
        with patch.dict("sys.modules", {"torch": torch, "soundfile": types.SimpleNamespace(), "transformers": transformers}):
            with patch.object(sidecar, "configure_cudnn"):
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(sidecar.main(), 1)
        response = json.loads(output.getvalue())
        self.assertIn("CUDA is unavailable", response["error"])
        self.assertNotIn("ready", response)


if __name__ == "__main__":
    unittest.main()
