from pathlib import Path

import openwakeword
from openwakeword.model import Model

print("openwakeword import OK")
openwakeword.utils.download_models(model_names=["alexa"])

models_root = Path(openwakeword.__file__).resolve().parent / "resources" / "models"
tflite_models = sorted(models_root.glob("alexa*.tflite"))
onnx_models = sorted(models_root.glob("alexa*.onnx"))
if tflite_models:
    selected_model = tflite_models[0]
    model = Model(wakeword_models=[str(selected_model)], inference_framework="tflite")
elif onnx_models:
    selected_model = onnx_models[0]
    model = Model(wakeword_models=[str(selected_model)], inference_framework="onnx")
else:
    raise RuntimeError(f"No downloaded Alexa model found under: {models_root}")

print(f"model init OK ({len(model.models)} models loaded)")
