import contextlib
import importlib.util
import io
import json
from pathlib import Path
import types
import unittest
from unittest.mock import Mock, patch


spec = importlib.util.spec_from_file_location(
    "parakeet_transcribe", Path(__file__).resolve().parents[1] / "scripts/parakeet_transcribe.py"
)
sidecar = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sidecar)


class ParakeetSidecarTest(unittest.TestCase):
    def test_out_of_memory_explains_how_to_recover(self):
        message = sidecar.describe_error(RuntimeError("CUDA out of memory"))
        self.assertIn("Brak pamięci GPU", message)
        self.assertIn("CUDA out of memory", message)

    def test_cudnn_error_retains_original_cause(self):
        message = sidecar.describe_error(RuntimeError("cuDNN is unavailable or disabled for CUDA Execution Provider"))
        self.assertIn("CUDA/cuDNN", message)
        self.assertIn("cuDNN is unavailable", message)

    def test_multiline_unicode_text_is_one_response(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            sidecar.emit_response(text="Zażółć\ngęślą jaźń")
        self.assertEqual(len(output.getvalue().splitlines()), 1)
        self.assertEqual(json.loads(output.getvalue())["text"], "Zażółć\ngęślą jaźń")

    def test_model_load_failure_returns_error_before_ready(self):
        onnx_asr = types.SimpleNamespace(load_model=Mock(side_effect=RuntimeError("CUDA is unavailable")))
        onnxruntime = types.SimpleNamespace(preload_dlls=Mock())
        output = io.StringIO()
        with patch.dict("sys.modules", {"onnx_asr": onnx_asr, "onnxruntime": onnxruntime}):
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(sidecar.main(), 1)
        response = json.loads(output.getvalue())
        self.assertIn("CUDA is unavailable", response["error"])
        self.assertNotIn("ready", response)


if __name__ == "__main__":
    unittest.main()
