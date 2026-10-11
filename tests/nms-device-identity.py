"""Execute patched x64 identity helper against COM wrapper/device models."""
from pathlib import Path
import struct, uuid
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
root=Path(__file__).resolve().parents[1]
pe=pefile.PE(str(root / "payload/deep-fried-chicken/deep-fried-chicken-nms.addon64"))
RAW=uuid.UUID("7f2c9a11-3b4e-4d6a-812f-5e9cd37a1b42").bytes_le
UNK=uuid.UUID("00000000-0000-0000-c000-000000000046").bytes_le
DEV=uuid.UUID("189819f1-1db6-4b57-be54-1821339b85f7").bytes_le

def run(actual,expected,unwraps,identities,get_error=0,qi_error=None):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(0,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
 u.mem_write(0,pe.get_memory_mapped_image())
 u.mem_map(0x200000,0x1000);u.mem_write(0x200000,b"\xc3"*0x100)
 u.mem_map(0x300000,0x10000);u.mem_map(0x500000,0x10000)
 refs={};errors=[];calls=[]
 def qword(address,value):u.mem_write(address,struct.pack("<Q",value))
 for obj in set([0x300100,actual,expected]+list(unwraps.values())+list(identities.values())):
  qword(obj,0x308000);refs[obj]=0
 qword(0x308000,0x200000);qword(0x308010,0x200020);qword(0x308038,0x200010)
 def hook(u,address,size,data):
  if address==0x200030:u.emu_stop();return
  if address not in (0x200000,0x200010,0x200020):return
  obj=u.reg_read(UC_X86_REG_RCX);calls.append((address,obj))
  if address==0x200020:
   refs[obj]-=1
   if refs[obj]<0:errors.append("Unbalanced release")
   u.reg_write(UC_X86_REG_RAX,refs[obj]);return
  guid=bytes(u.mem_read(u.reg_read(UC_X86_REG_RDX),16));out=u.reg_read(UC_X86_REG_R8)
  target=None;hr=0x80004002
  if address==0x200010:
   assert guid==DEV;target=actual;hr=get_error
  elif guid==RAW:target=unwraps.get(obj);hr=0 if target else hr
  elif guid==UNK:
   target=identities.get(obj,obj);hr=0x80004002 if qi_error==obj else 0
  else:raise AssertionError("Unexpected GUID")
  if hr==0:refs[target]+=1;qword(out,target)
  u.reg_write(UC_X86_REG_RAX,hr)
 u.hook_add(UC_HOOK_CODE,hook)
 stack=0x508008;qword(stack,0x200030)
 u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RCX,0x300100);u.reg_write(UC_X86_REG_RDX,expected)
 # Preserve all nonvolatile registers and verify stack balance.
 saved={r:0x12340000+i for i,r in enumerate([UC_X86_REG_RBX,UC_X86_REG_RBP,UC_X86_REG_RSI,UC_X86_REG_RDI,UC_X86_REG_R12,UC_X86_REG_R13,UC_X86_REG_R14,UC_X86_REG_R15])}
 for r,v in saved.items():u.reg_write(r,v)
 u.emu_start(0x2d520,0x200031,count=1000)
 assert u.reg_read(UC_X86_REG_RIP)==0x200030
 assert u.reg_read(UC_X86_REG_RSP)==stack+8
 assert all(u.reg_read(r)==v for r,v in saved.items())
 assert not errors and all(v==0 for v in refs.values()),(errors,refs)
 return u.reg_read(UC_X86_REG_EAX)
A,B,W,V,I,J=0x301000,0x301100,0x301200,0x301300,0x301400,0x301500
assert run(A,A,{}, {})==0
assert run(W,A,{W:A},{})==0
assert run(A,W,{W:A},{})==0
assert run(W,V,{W:A,V:A},{})==0
assert run(A,B,{}, {A:I,B:I})==0
assert run(W,V,{W:A,V:B},{A:I,B:J})==0x80070057
assert run(A,B,{}, {})==0x80070057
assert run(A,A,{}, {},get_error=0x80004005)==0x80004005
assert run(A,B,{}, {},qi_error=A)==0x80004002
assert run(A,B,{}, {},qi_error=B)==0x80004002
# Exception table includes valid unwind descriptions for both new functions.
identity=next(s for s in pe.sections if s.Name.rstrip(b'\0')==b'.dfcfix')
starts={e.struct.BeginAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
assert identity.VirtualAddress in starts and identity.VirtualAddress+0x200 in starts
print("PASS: raw/wrapped/canonical device identities, rejection, HRESULTs, balanced references, ABI and unwind records")
