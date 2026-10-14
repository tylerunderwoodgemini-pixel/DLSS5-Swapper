"""Realign the DFC LoadLibraryExW boundary for Unity/Mono callers.

Exact builds only. Preserve arguments, result, last-error and incoming stack.
The original hook body, NGX processing and fences remain unchanged.
"""
import hashlib, struct, sys
from pathlib import Path
import pefile
from keystone import Ks, KS_ARCH_X86, KS_MODE_64

BASES = {
    '7076f85df1c883800d714ed66c36461170a13dabadf7b3d77824587b941cd972',
    '7a50dc8ddd6c1a7e389106c7fe8b1ed49abd5f045cc133de477675f25a5d882d',
}
ENTRY = 0x228e0
MARKER = b'DFC_LOADER_STACK_V1\0'
def align(n, a): return (n+a-1)//a*a

def repair(data):
    if hashlib.sha256(data).hexdigest() not in BASES:
        raise ValueError('Unsupported DFC build')
    pe = pefile.PE(data=data)
    prologue = bytes.fromhex('40534883ec20')
    if pe.get_data(ENTRY, 6) != prologue:
        raise ValueError('LoadLibraryExW hook prologue changed')
    va = align(pe.sections[-1].VirtualAddress+pe.sections[-1].Misc_VirtualSize, pe.OPTIONAL_HEADER.SectionAlignment)
    raw = align(len(data), pe.OPTIONAL_HEADER.FileAlignment)
    tail = va+0x40
    ks = Ks(KS_ARCH_X86, KS_MODE_64)
    # Three register arguments, no stack arguments. RBP remembers the incoming
    # stack before alignment. No API calls after the original: last-error survives.
    code = bytes(ks.asm(f'''push rbp; mov rbp, rsp; and rsp, -16;
        sub rsp, 0x20; call {tail}; mov rsp, rbp; pop rbp; ret''', va)[0])
    inner = prologue+bytes(ks.asm(f'jmp {ENTRY+6}', tail+6)[0])
    section = bytearray(0x100)
    section[:len(code)] = code
    section[0x40:0x40+len(inner)] = inner
    # Windows x64 unwind metadata for dynamic RSP with RBP as frame register.
    section[0x60:0x6c] = bytes([1,12,3,5,12,0x32,4,3,1,0x50,0,0])
    section[0x70:0x78] = bytes([1,6,2,0,6,0x32,2,0x30])
    section[0x80:0x80+len(MARKER)] = MARKER
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata = pe.get_data(directory.VirtualAddress, directory.Size)
    section.extend(pdata+struct.pack('<III',va,va+len(code),va+0x60)+struct.pack('<III',tail,tail+len(inner),va+0x70))
    header = pe.sections[-1].get_file_offset()+40
    if header+40 > pe.OPTIONAL_HEADER.SizeOfHeaders: raise ValueError('No section header room')
    out = bytearray(data)
    out.extend(b'\0'*(raw-len(out)))
    rawsize = align(len(section), pe.OPTIONAL_HEADER.FileAlignment)
    out.extend(section); out.extend(b'\0'*(rawsize-len(section)))
    out[header:header+40] = struct.pack('<8sIIIIIIHHI', b'.dfcldr',len(section),va,rawsize,raw,0,0,0,0,0x60000020)
    struct.pack_into('<H',out,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),pe.FILE_HEADER.NumberOfSections+1)
    struct.pack_into('<I',out,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),align(va+len(section),pe.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into('<II',out,directory.get_file_offset(),va+0x100,len(pdata)+24)
    offset = pe.get_offset_from_rva(ENTRY)
    out[offset:offset+6] = b'\xe9'+struct.pack('<i',va-ENTRY-5)+b'\x90'
    return bytes(out)

if __name__ == '__main__':
    src, dst = map(Path, sys.argv[1:])
    out = repair(src.read_bytes()); dst.write_bytes(out)
    print(hashlib.sha256(out).hexdigest())
