// Test only: exposes COM unwrap contracts, with no command-recording methods.
// Any attempt to record before unwrapping this object is deliberately invalid.
struct NativeListWrapper final : IUnknown {
  IUnknown *native; bool streamline; ULONG refs=1;
  NativeListWrapper(IUnknown *value,bool sl):native(value),streamline(sl){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
    *out=nullptr;
    static const GUID reshade={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    static const GUID sl={0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    if(iid==(streamline?sl:reshade)){*out=native;native->AddRef();return S_OK;}
    if(iid==IID_IUnknown||iid==__uuidof(ID3D12GraphicsCommandList)){*out=this;AddRef();return S_OK;}
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
  ULONG STDMETHODCALLTYPE Release() override {return --refs;}
};
