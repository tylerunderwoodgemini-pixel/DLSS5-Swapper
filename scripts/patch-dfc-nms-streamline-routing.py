"""Exact-build NMS Vulkan fix: preserve Streamline tag command buffers.
The feeder's hidden D3D12 device changes DFC's shared API selector. NMS
Streamline calls still contain Vulkan command buffers, which are not COM.
Only the two tag-wrapper D3D12 unwrap branches are bypassed; native NGX
and feeder neural processing are unchanged. Never use this variant for D3D12.
"""
import hashlib, struct, sys
from pathlib import Path
import pefile
BASE = "19c3f85b3d7a05648d4f2d1af600aa9f323176e2c2aa717c030faae7a724d5a9"
GATES = ((0x25d40, 0x25e0d), (0x25f56, 0x2602b))
def repair(data):
    if hashlib.sha256(data).hexdigest() != BASE:
        raise ValueError("Expected the exact original NMS DFC payload")
    pe = pefile.PE(data=data)
    output = bytearray(data)
    for entry, target in GATES:
        offset = pe.get_offset_from_rva(entry)
        expected = b"\x0f\x85" + struct.pack("<i", target-entry-6)
        if data[offset:offset+6] != expected:
            raise ValueError("Streamline gate does not match verified build")
        output[offset:offset+6] = b"\xe9" + struct.pack("<i", target-entry-5) + b"\x90"
    return bytes(output)
if __name__ == "__main__":
    source, destination = map(Path, sys.argv[1:])
    result = repair(source.read_bytes())
    destination.write_bytes(result)
    print(hashlib.sha256(result).hexdigest())
