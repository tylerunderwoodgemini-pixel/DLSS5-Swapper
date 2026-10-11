"""Run the actual DFC hook-identity gate with raw/proxy queue identities."""
import importlib.util, struct
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
root=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,root/'scripts'/f'{name}.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
padding=module('patch-dfc-dlaa-padding');patch=module('patch-dfc-queue-identity')
stock=(root/'test/fixtures/dfc-1.4.8-stock.addon64').read_bytes()
baseline=padding.repair(stock)[0]
patched=patch.repair(baseline)
def run(data,proxy,success=True):
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(base,0x100000);u.mem_write(base,pe.get_memory_mapped_image())
    mem=0x200000;u.mem_map(mem,0x20000)
    raw,wrapped,rawvt,proxyvt=mem,mem+0x100,mem+0x1000,mem+0x1100
    qi,release,rawexec,proxyexec,stub,end=[mem+x for x in [0x2000,0x2010,0x2020,0x2030,0x2040,0x2050]]
    def qword(a,v):u.mem_write(a,struct.pack('<Q',v))
    def dword(a,v):u.mem_write(a,struct.pack('<I',v))
    for obj,vt,target in [(raw,rawvt,rawexec),(wrapped,proxyvt,proxyexec)]:
        qword(obj,vt);qword(vt,qi);qword(vt+0x10,release);qword(vt+0x50,target)
    # Existing DFC process-lifetime raw Execute hook; no Detours installation
    # is simulated. Its real identity comparison and poison flag execute.
    dword(base+0x9c7cc,2);qword(base+0x9c828,rawexec)
    for rva in [0x68078,0x68080]:qword(base+rva,stub)
    refs=0
    def ret(value=None):
        sp=u.reg_read(UC_X86_REG_RSP);target=struct.unpack('<Q',u.mem_read(sp,8))[0]
        u.reg_write(UC_X86_REG_RSP,sp+8);u.reg_write(UC_X86_REG_RIP,target)
        if value is not None:u.reg_write(UC_X86_REG_RAX,value)
    def hook(uc,a,size,arg):
        nonlocal refs
        if a==end:uc.emu_stop()
        elif a==base+0x44aa0:ret() # compiler stack probe
        elif a==base+0x11f60:ret(1) # executable-address qualification
        elif a==base+0x13260:ret() # logging
        elif a==stub:ret()
        elif a==qi:
            assert bytes(u.mem_read(u.reg_read(UC_X86_REG_RDX),16))==patch.GUID
            if proxy and success:
                qword(u.reg_read(UC_X86_REG_R8),raw);refs+=1;ret(0)
            else:ret(0x80004002)
        elif a==release:refs-=1;ret(1)
        elif a==base+0x44b00:ret() # security-cookie check
    u.hook_add(UC_HOOK_CODE,hook)
    # Failures must still poison the gate; successful proxy normalization
    # must keep it armed, with balanced COM ownership, across many calls.
    for _ in range(100):
        sp=mem+0x1f008;qword(sp,end)
        u.reg_write(UC_X86_REG_RSP,sp);u.reg_write(UC_X86_REG_RCX,wrapped if proxy else raw)
        u.emu_start(base+patch.ENTRY,end,count=500)
        assert u.reg_read(UC_X86_REG_RSP)==sp+8
        assert refs==0
        state=struct.unpack('<I',u.mem_read(base+0x9c7cc,4))[0]
        expected=0xffffffff if proxy and (data==baseline or not success) else 2
        assert state==expected,(hex(state),hex(expected))
        if state==0xffffffff:break
run(baseline,True)
run(patched,True)
run(patched,False)
run(patched,True,False)
pe=pefile.PE(data=patched)
# Whole original text is unchanged except the verified seven-byte hook entry.
old=pefile.PE(data=baseline);before=bytearray(old.get_data(0x1000,0x66d30));after=bytearray(pe.get_data(0x1000,0x66d30))
before[patch.ENTRY-0x1000:patch.ENTRY-0x1000+7]=after[patch.ENTRY-0x1000:patch.ENTRY-0x1000+7]
assert before==after,'Unrelated retirement/fence code changed'
print('PASS: original proxy mismatch reproduced; 100 raw and 100 proxy registrations preserve hook; QI failure stays fail-safe; references balanced; retirement code unchanged')

assert (root/'payload/deep-fried-chicken/deep-fried-chicken.addon64').read_bytes()==patched
