"""Verify the shipped NMS tag wrappers forward Vulkan buffers without COM queries."""
from pathlib import Path
import hashlib, struct
import pefile
root = Path(__file__).resolve().parents[1]
payload = root / "payload/deep-fried-chicken/deep-fried-chicken-nms.addon64"
data = payload.read_bytes()
assert hashlib.sha256(data).hexdigest() == "261bfbfb0848bfaba411c2f2866d2ac7549af4604492011f768e96d425582433"
pe = pefile.PE(data=data)
for entry, target in ((0x25d40, 0x25e0d), (0x25f56, 0x2602b)):
    code = pe.get_data(entry, 6)
    assert code[0] == 0xe9 and code[5] == 0x90
    assert entry + 5 + struct.unpack("<i", code[1:5])[0] == target
print("PASS: both NMS Streamline wrappers bypass D3D12 COM unwrapping")
