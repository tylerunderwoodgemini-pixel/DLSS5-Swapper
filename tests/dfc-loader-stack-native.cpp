#include <windows.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cassert>
int main(int argc,char **argv) {
    assert(argc==2);
    std::ifstream f(argv[1],std::ios::binary);std::vector<char> disk((std::istreambuf_iterator<char>(f)),{});
    assert(!disk.empty());
    auto nt=(IMAGE_NT_HEADERS64*)(disk.data()+((IMAGE_DOS_HEADER*)disk.data())->e_lfanew);
    auto image=(BYTE*)VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(image);
    memcpy(image,disk.data(),nt->OptionalHeader.SizeOfHeaders);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)memcpy(image+sections[i].VirtualAddress,disk.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    auto table=(RUNTIME_FUNCTION*)(image+nt->OptionalHeader.DataDirectory[3].VirtualAddress);
    assert(RtlAddFunctionTable(table,nt->OptionalHeader.DataDirectory[3].Size/sizeof(RUNTIME_FUNCTION),(DWORD64)image));
    auto wrapper=image+sections[nt->FileHeader.NumberOfSections-1].VirtualAddress;
    for(unsigned alignment: {0u,8u}) {
        alignas(16) BYTE stack[1024]{};
        auto entry=(DWORD64)(stack+768+alignment);
        *(DWORD64*)entry=0x12345678;*(DWORD64*)(entry-8)=0xabcdef;
        CONTEXT c{};c.Rip=(DWORD64)(wrapper+12);c.Rbp=entry-8;c.Rsp=((entry-8)&~15ull)-0x20;
        DWORD64 base=0,establisher=0;PVOID handler=nullptr;
        auto fn=RtlLookupFunctionEntry(c.Rip,&base,nullptr);assert(fn);
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,fn,&c,&handler,&establisher,nullptr);
        assert(c.Rip==0x12345678 && c.Rbp==0xabcdef && c.Rsp==entry+8);
    }
    // Real CPU test: original loader replacement performs an aligned SIMD store
    // in its caller-provided home area. This would fault on the reported stack.
    BYTE api[]={0x0f,0x29,0x44,0x24,0x08,0x31,0xc0,0xc3};
    auto scratch=(BYTE*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(scratch);
    memcpy(scratch,api,sizeof(api));
    BYTE dispatch[]={0xff,0xe0};memcpy(scratch+32,dispatch,2);
    BYTE error[]={0xb8,126,0,0,0,0xc3};memcpy(scratch+48,error,sizeof(error));
    scratch[64]=0xc3;
    *(void**)(image+0x9bf58)=scratch;*(void**)(image+0x683e0)=scratch+32;
    *(void**)(image+0x68060)=scratch+48;*(void**)(image+0x68068)=scratch+64;
    for(BYTE allocation: {BYTE(0x20),BYTE(0x28)}) {
        BYTE caller[]={0x48,0x83,0xec,allocation,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,allocation,0xc3};
        auto address=(ULONG64)(image+0x228e0);memcpy(caller+6,&address,8);memcpy(scratch+128,caller,sizeof(caller));
        FlushInstructionCache(GetCurrentProcess(),scratch,4096);
        assert(((HMODULE(WINAPI*)(LPCWSTR,HANDLE,DWORD))(scratch+128))(L"test",nullptr,0x800)==nullptr);
    }
    RtlDeleteFunctionTable(table);VirtualFree(image,0,MEM_RELEASE);VirtualFree(scratch,0,MEM_RELEASE);
    puts("PASS: real CPU loader calls and Windows stack unwinding tolerate both incoming alignments");
}
