"""Execute the actual patched x64 guard with Unicorn, including its entry jump."""
import importlib.util, random, struct
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
root = Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patch', root/'scripts/patch-dfc-dlaa-padding.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
# Keep original upstream binary as the reproducible input; shipped binary is repaired.
stock=(root/'test/fixtures/dfc-1.4.8-stock.addon64').read_bytes()
output,cave,code=patch.repair(stock)
pe=pefile.PE(data=output)
base=0x180000000
image=pe.get_memory_mapped_image()
uc=Uc(UC_ARCH_X86,UC_MODE_64);uc.mem_map(base,0x100000);uc.mem_write(base,image)
contract, stack = 0x200000, 0x300000
uc.mem_map(contract,0x1000);uc.mem_map(stack,0x1000)
results=[]
def stop(uc,address,size,data):
    if address in (base+patch.ACCEPT,base+patch.REJECT):
        results.append(address==base+patch.ACCEPT);uc.emu_stop()
uc.hook_add(UC_HOOK_CODE,stop)
def check(iw,ih,ow,oh,quality):
    data=bytearray(0x100);struct.pack_into('<IIIII',data,0x28,iw,ih,ow,oh,quality)
    uc.mem_write(contract,bytes(data));uc.reg_write(UC_X86_REG_RDI,contract)
    al=int(iw==ow and ih==oh);rax=0x1234567890000000|al;rdx=0x98765432
    uc.reg_write(UC_X86_REG_RAX,rax);uc.reg_write(UC_X86_REG_RDX,rdx);uc.reg_write(UC_X86_REG_RSP,stack+0x800)
    uc.emu_start(base+patch.ENTRY,base+0x100000,count=100)
    expected=quality!=5 or bool(al) or (iw>0 and ih>0 and iw%8==0 and ih%8==0 and 0<=ow-iw<=7 and 0<=oh-ih<=7)
    assert results.pop()==expected,(iw,ih,ow,oh,quality)
    assert uc.reg_read(UC_X86_REG_RAX)==rax
    assert uc.reg_read(UC_X86_REG_RDX)==rdx
    assert uc.reg_read(UC_X86_REG_RSP)==stack+0x800
    assert bytes(uc.mem_read(contract,len(data)))==data,'Native contract was modified'
for case in [(3824,3904,3826,3904,5),(3824,3904,3824,3904,5),(3824,3904,3840,3904,5),(3824,3904,3823,3904,5),(3825,3904,3826,3904,5),(0,0,1,1,5),(0xfffffff8,8,0,8,5)]:check(*case)
random.seed(10)
for _ in range(2000):
    iw=random.randrange(1,1024)*8;ih=random.randrange(1,1024)*8
    check(iw,ih,iw+random.randrange(-8,17),ih+random.randrange(-8,17),random.randrange(6))
# Published payload must be exactly reproducible, with no unrelated code changes.
assert (root/'payload/deep-fried-chicken/deep-fried-chicken.addon64').read_bytes()==__import__('runpy').run_path(str(root/'scripts/patch-dfc-queue-identity.py'))['repair'](output)
print('PASS: 2007 executable guard cases; registers/stack/contract unchanged; exact reproducible payload')

# On Windows, also execute the same patched machine code on the host CPU.
import os
if os.name == 'nt':
    import ctypes
    from keystone import Ks, KS_ARCH_X86, KS_MODE_64
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.VirtualAlloc.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_ulong,ctypes.c_ulong]
    kernel.VirtualAlloc.restype=ctypes.c_void_p
    kernel.VirtualFree.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_ulong]
    allocation=kernel.VirtualAlloc(None,0x100000,0x3000,0x40)
    assert allocation
    try:
        ctypes.memmove(allocation,image,len(image))
        ctypes.memmove(allocation+patch.ACCEPT,bytes.fromhex('b8010000005fc3'),7)
        ctypes.memmove(allocation+patch.REJECT,bytes.fromhex('31c05fc3'),4)
        wrapper=bytes(Ks(KS_ARCH_X86,KS_MODE_64).asm(f'push rdi; mov rdi,rcx; mov eax,edx; jmp {patch.ENTRY}',addr=0xf0000)[0])
        ctypes.memmove(allocation+0xf0000,wrapper,len(wrapper))
        call=ctypes.WINFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint)(allocation+0xf0000)
        for case in [(3824,3904,3826,3904,5),(3824,3904,3824,3904,5),(3824,3904,3840,3904,5),(3824,3904,3823,3904,5),(3825,3904,3826,3904,5),(0,0,1,1,5),(0xfffffff8,8,0,8,5)]:
            iw,ih,ow,oh,quality=case
            data=bytearray(0x100);struct.pack_into('<IIIII',data,0x28,*case)
            buf=ctypes.create_string_buffer(bytes(data))
            equality=iw==ow and ih==oh
            expected=quality!=5 or equality or (iw>0 and ih>0 and iw%8==0 and ih%8==0 and 0<=ow-iw<=7 and 0<=oh-ih<=7)
            assert bool(call(buf,int(equality)))==expected,case
            assert buf.raw[:len(data)]==data
        print('PASS: Windows CPU execution of patched branch and alignment boundaries')
    finally:
        kernel.VirtualFree(allocation,0,0x8000)
