#pragma once
#include <map>
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
