import onnxruntime as ort
import numpy as np
import time

print("Loading model...")
session = ort.InferenceSession("Resources/htdemucs.onnx", providers=['CPUExecutionProvider'])
print("Model loaded.")

input_data = np.zeros((1, 2, 343980), dtype=np.float32)

print("Running inference...")
start = time.time()
output = session.run(["stems"], {"mix": input_data})
end = time.time()

print(f"Inference took {end-start:.2f} seconds.")
print("Output shape:", output[0].shape)
print("Output max value:", np.max(output[0]))
