"""Reproducible, exact-build DFC 1.4.8 DLAA alignment repair.
Requires pefile, keystone-engine. No NGX parameter, guide or resource changes.
Only the late DLAA equality gate changes; all preceding resource checks remain.
"""
import hashlib, struct, sys
from pathlib import Path
import pefile
from keystone import Ks, KS_ARCH_X86, KS_MODE_64
STOCK = '106143de0d74853b966a7141c19c2bb0fb46e7ce32c7f67d830d2dc5cddf80ec'
ENTRY, ACCEPT, REJECT = 0x88c1, 0x88e3, 0x88cb
MARKER = b'DFC_DLAA_ALIGNMENT_V1\0'
def repair(data):
    if hashlib.sha256(data).hexdigest() != STOCK:
        raise ValueError('Only the exact verified DFC 1.4.8-alpha binary is supported')
    pe = pefile.PE(data=data)
    sec = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    cave = sec.VirtualAddress + sec.Misc_VirtualSize
    # RDI is the validated local contract: input +28/+2c, output +30/+34,
    # quality +38. AL carries exact equality. Preserve both scratch registers.
    assembly = f"""
        cmp dword ptr [rdi+0x38], 5
        jne {ACCEPT}
        test al, al
        jne {ACCEPT}
        push rax
        push rdx
        mov eax, dword ptr [rdi+0x28]
        test eax, 7
        jne reject
        test eax, eax
        jz reject
        mov edx, dword ptr [rdi+0x30]
        cmp edx, eax
        jb reject
        sub edx, eax
        cmp edx, 7
        ja reject
        mov eax, dword ptr [rdi+0x2c]
        test eax, 7
        jne reject
        test eax, eax
        jz reject
        mov edx, dword ptr [rdi+0x34]
        cmp edx, eax
        jb reject
        sub edx, eax
        cmp edx, 7
        ja reject
        pop rdx
        pop rax
        jmp {ACCEPT}
    reject:
        pop rdx
        pop rax
        jmp {REJECT}
    """
    code = bytes(Ks(KS_ARCH_X86, KS_MODE_64).asm(assembly, addr=cave)[0])
    end = sec.Misc_VirtualSize + len(code) + len(MARKER)
    if end > sec.SizeOfRawData:
        raise ValueError('Executable padding insufficient')
    raw = sec.PointerToRawData + sec.Misc_VirtualSize
    if any(data[raw:raw+len(code)+len(MARKER)]):
        raise ValueError('Executable padding is not empty')
    offset = pe.get_offset_from_rva(ENTRY)
    if data[offset:offset+8] != bytes.fromhex('837f3805751c84c0'):
        raise ValueError('DLAA guard signature mismatch')
    output = bytearray(data)
    output[raw:raw+len(code)+len(MARKER)] = code + MARKER
    output[offset:offset+8] = b'\xe9' + struct.pack('<i', cave-ENTRY-5) + b'\x90'*3
    struct.pack_into('<I', output, sec.get_file_offset()+8, end)
    updated = pefile.PE(data=bytes(output))
    struct.pack_into('<I', output, updated.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'), updated.generate_checksum())
    return bytes(output), cave, code
if __name__ == '__main__':
    source, dest = map(Path, sys.argv[1:3])
    output, cave, code = repair(source.read_bytes())
    dest.write_bytes(output)
    print(hashlib.sha256(output).hexdigest(), 'cave', hex(cave), 'bytes', len(code))
