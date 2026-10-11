"""Exact-build NMS DFC device identity repair; requires pefile/keystone.
Retains Streamline Vulkan repair. Compares canonical COM identity after
optional ReShade device unwrap, preserving cross-device rejection and refs.
"""
import hashlib, struct, sys, uuid
from pathlib import Path
import pefile
from keystone import Ks, KS_ARCH_X86, KS_MODE_64
BASE="f837693a3db573df9716eba740d7c684b6f5a82f79158df3f2d2a2a5b81bb15f"
ENTRY=0x2d520

def align(n,a): return (n+a-1)//a*a

def repair(data):
    if hashlib.sha256(data).hexdigest()!=BASE: raise ValueError("Expected exact crash-repaired NMS payload")
    pe=pefile.PE(data=data)
    if pe.get_data(ENTRY,5)!=bytes.fromhex("48895c2410"):raise ValueError("Unexpected identity helper")
    va=align(pe.sections[-1].VirtualAddress+pe.sections[-1].Misc_VirtualSize,pe.OPTIONAL_HEADER.SectionAlignment)
    raw=align(len(data),pe.OPTIONAL_HEADER.FileAlignment)
    normal=va+0x200; device=0x6add0; unwrap=va+0x400; unknown=unwrap+16
    ks=Ks(KS_ARCH_X86,KS_MODE_64)
    # All temporaries live in stack locals; only volatile registers are used.
    parent=f"""
      sub rsp, 0x58
      mov [rsp+0x28], rdx
      mov qword ptr [rsp+0x20], 0
      mov qword ptr [rsp+0x30], 0
      mov qword ptr [rsp+0x38], 0
      mov rax, [rcx]
      lea r8, [rsp+0x20]
      lea rdx, [rip+{device}]
      call qword ptr [rax+0x38]
      test eax,eax
      js done
      mov rcx,[rsp+0x20]
      lea rdx,[rsp+0x30]
      call {normal}
      mov [rsp+0x40],eax
      mov rcx,[rsp+0x20]
      mov rax,[rcx]
      call qword ptr [rax+0x10]
      mov eax,[rsp+0x40]
      test eax,eax
      js done
      mov rcx,[rsp+0x28]
      lea rdx,[rsp+0x38]
      call {normal}
      mov [rsp+0x40],eax
      test eax,eax
      js release_actual
      mov rax,[rsp+0x30]
      cmp rax,[rsp+0x38]
      mov eax,0x80070057
      jne mismatch
      xor eax,eax
    mismatch:
      mov [rsp+0x40],eax
      mov rcx,[rsp+0x38]
      mov rax,[rcx]
      call qword ptr [rax+0x10]
    release_actual:
      mov rcx,[rsp+0x30]
      mov rax,[rcx]
      call qword ptr [rax+0x10]
      mov eax,[rsp+0x40]
    done:
      add rsp,0x58
      ret
    """
    normalize=f"""
      sub rsp,0x38
      mov [rsp+0x20],rcx
      mov [rsp+0x30],rdx
      mov qword ptr [rsp+0x28],0
      mov rax,[rcx]
      lea rdx,[rip+{unwrap}]
      lea r8,[rsp+0x28]
      call qword ptr [rax]
      test eax,eax
      js original
      cmp qword ptr [rsp+0x28],0
      je original
      mov rcx,[rsp+0x28]
      jmp canonical
    original:
      mov qword ptr [rsp+0x28],0
      mov rcx,[rsp+0x20]
    canonical:
      mov rax,[rcx]
      lea rdx,[rip+{unknown}]
      mov r8,[rsp+0x30]
      call qword ptr [rax]
      mov [rsp+0x20],eax
      mov rcx,[rsp+0x28]
      test rcx,rcx
      je finish
      mov rax,[rcx]
      call qword ptr [rax+0x10]
    finish:
      mov eax,[rsp+0x20]
      add rsp,0x38
      ret
    """
    # Keystone RIP operands are displacements, not absolute RVAs. Resolve
    # GUID LEAs in a second pass by replacing their displacement bytes.
    def assemble(src,address,targets):
      blob=bytearray(ks.asm(src,address)[0])
      for target in targets:
        pattern=b"\x48\x8d\x15"+struct.pack("<i",target)
        i=blob.find(pattern)
        if i<0:raise ValueError("Missing GUID reference")
        blob[i+3:i+7]=struct.pack("<i",target-(address+i+7))
      return bytes(blob)
    code=assemble(parent,va,[device]);helper=assemble(normalize,normal,[unwrap,unknown])
    assert len(code)<0x200 and len(helper)<0x200
    section=bytearray(0x500)
    section[:len(code)]=code;section[0x200:0x200+len(helper)]=helper
    section[0x400:0x410]=uuid.UUID("7f2c9a11-3b4e-4d6a-812f-5e9cd37a1b42").bytes_le
    section[0x410:0x420]=uuid.UUID("00000000-0000-0000-c000-000000000046").bytes_le
    # Windows x64 unwind records for SUB RSP, 58h / 38h.
    section[0x420:0x428]=bytes([1,4,1,0,4,0xa2,0,0])
    section[0x428:0x430]=bytes([1,4,1,0,4,0x62,0,0])
    directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    pdata=pe.get_data(directory.VirtualAddress,directory.Size)
    section[0x500:]=pdata+struct.pack("<III",va,va+len(code),va+0x420)+struct.pack("<III",normal,normal+len(helper),va+0x428)
    # Existing runtime functions precede the new section: table remains sorted.
    header=pe.sections[-1].get_file_offset()+40
    if header+40>pe.OPTIONAL_HEADER.SizeOfHeaders:raise ValueError("No section header room")
    output=bytearray(data);output.extend(b"\0"*(raw-len(output)))
    rawsize=align(len(section),pe.OPTIONAL_HEADER.FileAlignment)
    output.extend(section);output.extend(b"\0"*(rawsize-len(section)))
    output[header:header+40]=struct.pack("<8sIIIIIIHHI",b".dfcfix",len(section),va,rawsize,raw,0,0,0,0,0x60000020)
    struct.pack_into("<H",output,pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections"),pe.FILE_HEADER.NumberOfSections+1)
    struct.pack_into("<I",output,pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage"),align(va+len(section),pe.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into("<II",output,directory.get_file_offset(),va+0x500,len(pdata)+24)
    offset=pe.get_offset_from_rva(ENTRY)
    output[offset:offset+5]=b"\xe9"+struct.pack("<i",va-ENTRY-5)
    return bytes(output)

if __name__=="__main__":
    source,dest=map(Path,sys.argv[1:]);out=repair(source.read_bytes());dest.write_bytes(out);print(hashlib.sha256(out).hexdigest())
