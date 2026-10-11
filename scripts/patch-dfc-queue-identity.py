"""Normalize ReShade queue wrappers before DFC's single-target Execute hook.
Exact supported binaries only. Retains all retirement fences and failure gates.
"""
import hashlib, struct, sys, uuid
from pathlib import Path
import pefile
from keystone import Ks, KS_ARCH_X86, KS_MODE_64
ENTRY = 0xfb50
GUID = uuid.UUID('7f2c9a11-3b4e-4d6a-812f-5e9cd37a1b42').bytes_le
BASES = {
    '1c88c0ffddb46ceb6b97ddff60e96ed3dd7bbe478546b38a8d67ce9b0d31047a': 'generic',
    '132d409dea58be1c522ffa2f8a8c754b4193da7b41f9e6c9b68df73627f9c0c0': 'nms',
}
def align(n, a): return (n+a-1)//a*a
def repair(data):
    digest = hashlib.sha256(data).hexdigest()
    if not any(digest == h for h in BASES):
        raise ValueError('Unsupported DFC build')
    pe = pefile.PE(data=data)
    if pe.get_data(ENTRY,8) != bytes.fromhex('4055b850200000e8'):
        raise ValueError('Queue hook prologue changed')
    # Only seven complete bytes are relocated (PUSH RBP; MOV EAX,2050h).
    va = align(pe.sections[-1].VirtualAddress+pe.sections[-1].Misc_VirtualSize,pe.OPTIONAL_HEADER.SectionAlignment)
    raw = align(len(data),pe.OPTIONAL_HEADER.FileAlignment)
    trampoline, guid = va+0x180, va+0x200
    src = f'''
      sub rsp, 0x38
      mov [rsp+0x28], rcx
      mov qword ptr [rsp+0x30], 0
      test rcx, rcx
      je invoke
      mov rax, [rcx]
      lea r8, [rsp+0x30]
      lea rdx, [rip+{guid}]
      call qword ptr [rax]
      test eax, eax
      js invoke
      mov rcx, [rsp+0x30]
      test rcx, rcx
      jne call_original
    invoke:
      mov rcx, [rsp+0x28]
    call_original:
      call {trampoline}
      mov [rsp+0x20], eax
      mov rcx, [rsp+0x30]
      test rcx, rcx
      je finish
      mov rax, [rcx]
      call qword ptr [rax+0x10]
    finish:
      mov eax, [rsp+0x20]
      add rsp, 0x38
      ret
    '''
    ks=Ks(KS_ARCH_X86,KS_MODE_64)
    code=bytearray(ks.asm(src,va)[0])
    pattern=b'\x48\x8d\x15'+struct.pack('<i',guid)
    i=code.index(pattern)
    code[i+3:i+7]=struct.pack('<i',guid-(va+i+7))
    tail=bytes(ks.asm(f'push rbp; mov eax,0x2050; jmp {ENTRY+7}',trampoline)[0])
    section=bytearray(0x240)
    section[:len(code)]=code
    section[0x180:0x180+len(tail)]=tail
    section[0x200:0x210]=GUID
    section[0x210:0x218]=bytes([1,4,1,0,4,0x62,0,0])
    section[0x218:0x220]=bytes([1,1,1,0,1,0x50,0,0])
    directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata=pe.get_data(directory.VirtualAddress,directory.Size)
    section[0x240:]=pdata+struct.pack('<III',va,va+len(code),va+0x210)+struct.pack('<III',trampoline,trampoline+len(tail),va+0x218)
    header=pe.sections[-1].get_file_offset()+40
    if header+40>pe.OPTIONAL_HEADER.SizeOfHeaders: raise ValueError('No header room')
    out=bytearray(data);out.extend(b'\0'*(raw-len(out)))
    rawsize=align(len(section),pe.OPTIONAL_HEADER.FileAlignment)
    out.extend(section);out.extend(b'\0'*(rawsize-len(section)))
    out[header:header+40]=struct.pack('<8sIIIIIIHHI',b'.dfcque',len(section),va,rawsize,raw,0,0,0,0,0x60000020)
    struct.pack_into('<H',out,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),pe.FILE_HEADER.NumberOfSections+1)
    struct.pack_into('<I',out,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),align(va+len(section),pe.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into('<II',out,directory.get_file_offset(),va+0x240,len(pdata)+24)
    off=pe.get_offset_from_rva(ENTRY)
    out[off:off+7]=b'\xe9'+struct.pack('<i',va-ENTRY-5)+b'\x90'*2
    return bytes(out)
if __name__=='__main__':
    src,dst=map(Path,sys.argv[1:]);out=repair(src.read_bytes());dst.write_bytes(out)
    print(hashlib.sha256(out).hexdigest())
