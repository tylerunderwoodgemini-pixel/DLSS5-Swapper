"""Exercise the real loader hook with normal and Unity's misaligned stack."""
import importlib.util, struct
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('loader',root/'scripts/patch-dfc-loader-stack.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)

def run(data, alignment, repaired):
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    u=Uc(UC_ARCH_X86,UC_MODE_64)
    u.mem_map(base,0x200000);u.mem_write(base,pe.get_memory_mapped_image())
    mem=0x200000;u.mem_map(mem,0x20000)
    target,error,seterror,end=[mem+x for x in [0x100,0x200,0x300,0x400]]
    def qword(a,v):u.mem_write(a,struct.pack('<Q',v))
    qword(base+0x9bf58,target) # original LoadLibraryExW pointer
    qword(base+0x68060,error);qword(base+0x68068,seterror)
    # CFG dispatch thunk is already in the PE and tail-jumps through RAX.
    sp=mem+0x1f000+alignment;qword(sp,end)
    sentinel=0xabcde
    args=[0x1234,0x5678,0x800,0x9876]
    for reg,value in zip([UC_X86_REG_RCX,UC_X86_REG_RDX,UC_X86_REG_R8,UC_X86_REG_R9],args):u.reg_write(reg,value)
    for reg in [UC_X86_REG_RBX,UC_X86_REG_RBP,UC_X86_REG_RSI,UC_X86_REG_RDI]:u.reg_write(reg,sentinel)
    u.reg_write(UC_X86_REG_RSP,sp)
    observed=[];last_error=0
    def ret(value):
        stack=u.reg_read(UC_X86_REG_RSP);dest=struct.unpack('<Q',u.mem_read(stack,8))[0]
        u.reg_write(UC_X86_REG_RSP,stack+8);u.reg_write(UC_X86_REG_RIP,dest);u.reg_write(UC_X86_REG_RAX,value)
    def hook(uc,address,size,user):
        nonlocal last_error
        if address==end:uc.emu_stop()
        elif address==target:
            observed.append(u.reg_read(UC_X86_REG_RSP)%16)
            assert [u.reg_read(r) for r in [UC_X86_REG_RCX,UC_X86_REG_RDX,UC_X86_REG_R8,UC_X86_REG_R9]]==args
            last_error=126;ret(0)
        elif address==error:ret(last_error)
        elif address==seterror:last_error=u.reg_read(UC_X86_REG_RCX);ret(0)
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(base+patch.ENTRY,end,count=300)
    assert observed==[8 if repaired else alignment],observed
    assert u.reg_read(UC_X86_REG_RSP)==sp+8
    assert u.reg_read(UC_X86_REG_RAX)==0 and last_error==126
    for reg in [UC_X86_REG_RBX,UC_X86_REG_RBP,UC_X86_REG_RSI,UC_X86_REG_RDI]:assert u.reg_read(reg)==sentinel

def module(name):
    s=importlib.util.spec_from_file_location(name,root/'scripts'/f'{name}.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

for name in ['generic']:
    baseline=module('patch-dfc-queue-identity').repair(module('patch-dfc-dlaa-padding').repair((root/'test/fixtures/dfc-1.4.8-stock.addon64').read_bytes())[0])
    fixed=patch.repair(baseline)
    for alignment in [0,8]:run(baseline,alignment,False);run(fixed,alignment,True)
    old=pefile.PE(data=baseline);new=pefile.PE(data=fixed)
    for section in old.sections:
        before=bytearray(old.get_data(section.VirtualAddress,section.Misc_VirtualSize))
        after=bytearray(new.get_data(section.VirtualAddress,section.Misc_VirtualSize))
        if section.VirtualAddress<=patch.ENTRY<section.VirtualAddress+section.Misc_VirtualSize:
            pos=patch.ENTRY-section.VirtualAddress;before[pos:pos+6]=after[pos:pos+6]
        assert before==after,'Original code/data changed outside six-byte loader entry'
    entries=[e.struct.BeginAddress for e in new.DIRECTORY_ENTRY_EXCEPTION]
    assert entries==sorted(entries),'Unwind table must remain sorted'
    assert fixed==(root/'payload/deep-fried-chicken/deep-fried-chicken.addon64').read_bytes()
nms=(root/'payload/deep-fried-chicken/deep-fried-chicken-nms.addon64').read_bytes()
assert patch.MARKER in nms
for alignment in [0,8]:run(nms,alignment,True)
print('PASS: reproduced misalignment; generic/NMS repair preserves all register arguments, stack, nonvolatile registers, result and last-error; unrelated code/data unchanged')
