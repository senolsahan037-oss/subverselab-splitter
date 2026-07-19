import onnxruntime as ort
import numpy as np

session = ort.InferenceSession("Resources/htdemucs.onnx", providers=['CPUExecutionProvider'])
input_data = np.random.uniform(-0.5, 0.5, (1, 2, 343980)).astype(np.float32)

output = session.run(["stems"], {"mix": input_data})
print("Output max value:", np.max(output[0]))
print("Output min value:", np.min(output[0]))
