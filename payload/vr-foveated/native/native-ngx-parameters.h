#pragma once
#include <map>
#include <cstdint>
#include <nvsdk_ngx_params.h>
#include <string>
#include <variant>
// NVIDIA's MSVC parameter ABI: the overload order is significant. Forward all
// unknown keys and override only our local view, never the provider's object.
class NativeParameters final : public NVSDK_NGX_Parameter {
  using Value = std::variant<unsigned long long, float, double, unsigned, int,
                             ID3D11Resource *, ID3D12Resource *, void *>;
  const NVSDK_NGX_Parameter *base;
  std::map<std::string, Value> values;
  template <class T> NVSDK_NGX_Result read(const char *key, T *out) const {
    auto it = values.find(key);
    if (it == values.end())
      return base->Get(key, out);
    if (auto v = std::get_if<T>(&it->second)) {
      *out = *v;
      return NVSDK_NGX_Result_Success;
    }
    // NGX consumers can retrieve the same resource through the generic void*
    // overload or the D3D11/D3D12 overload. Preserve its pointer representation,
    // as the SDK parameter object does; exact variant types are not its ABI.
    if constexpr (std::is_pointer_v<T>) {
      bool ok=false;
      std::visit([&](auto v){if constexpr(std::is_pointer_v<decltype(v)>){*out=reinterpret_cast<T>(v);ok=true;}else if constexpr(std::is_same_v<decltype(v),unsigned long long>){*out=reinterpret_cast<T>(static_cast<uintptr_t>(v));ok=true;}},it->second);
      if(ok)return NVSDK_NGX_Result_Success;
    }
    // DLSSNR 310.8.0 retrieves Color/Output/Backbuffer through vtable slot
    // 8 (Get unsigned long long), not the typed resource getter. Resources
    // must round-trip through that opaque 64-bit representation unchanged.
    if constexpr (std::is_same_v<T,unsigned long long>) {
      bool ok=false;
      std::visit([&](auto v){if constexpr(std::is_pointer_v<decltype(v)>){*out=static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(v));ok=true;}},it->second);
      if(ok)return NVSDK_NGX_Result_Success;
    }
    if constexpr (std::is_arithmetic_v<T>) {
      bool ok = false;
      std::visit(
          [&](auto v) {
            if constexpr (std::is_arithmetic_v<decltype(v)>) {
              *out = static_cast<T>(v);
              ok = true;
            }
          },
          it->second);
      if (ok)
        return NVSDK_NGX_Result_Success;
    }
    return NVSDK_NGX_Result_FAIL_InvalidParameter;
  }

public:
  explicit NativeParameters(const NVSDK_NGX_Parameter *p) : base(p) {}
#define NATIVE_SET(T)                                                          \
  void Set(const char *key, T v) override { values.insert_or_assign(key, v); }
  NATIVE_SET(unsigned long long)
  NATIVE_SET(float) NATIVE_SET(double) NATIVE_SET(unsigned) NATIVE_SET(int)
      NATIVE_SET(ID3D11Resource *) NATIVE_SET(ID3D12Resource *)
          NATIVE_SET(void *)
#undef NATIVE_SET
#define NATIVE_GET(T)                                                          \
  NVSDK_NGX_Result Get(const char *key, T *v) const override {                 \
    return read(key, v);                                                       \
  }
              NATIVE_GET(unsigned long long) NATIVE_GET(float)
                  NATIVE_GET(double) NATIVE_GET(unsigned) NATIVE_GET(int)
                      NATIVE_GET(ID3D11Resource *) NATIVE_GET(ID3D12Resource *)
                          NATIVE_GET(void *)
#undef NATIVE_GET
                              void Reset() override {
    values.clear();
  }
};
