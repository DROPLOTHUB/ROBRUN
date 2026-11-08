#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>

#include "../../driver/driver.hpp"
#include "../../utils/math.hpp"
#include "../../utils/robrun_log.hpp"

#include "offsets.hpp"
#include "generated/holder_graph.hpp"

// =============================================================================
// ROB RUN SDK primitives.
//
// Thin, self-validating wrappers over the managed heap:
//   * IL2CPP class resolution from script.json RVAs (name checked)
//   * class info / full name / instance size with a pointer cache
//   * static field access
//   * replication primitive readers (IPrimitiveData<T> / IPrimitiveModel<T>)
//   * List<T> / Dictionary<K,V> / Il2CppString helpers
//
// Nothing in here knows about ESP or the world graph.
// =============================================================================

namespace sdk
{
    namespace unity
    {
        extern uintptr_t unity_base;
        extern uintptr_t game_assembly_base;
    }
}

namespace robrun
{
    // ---------------------------------------------------------------------
    // Basic value types (the game uses System.Numerics.Vector3 and
    // UnityEngine.Quaternion; both are plain floats in memory).
    // ---------------------------------------------------------------------
    struct vec3
    {
        float x = 0.f, y = 0.f, z = 0.f;

        vec3() = default;
        vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

        vec3 operator+(const vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        vec3 operator-(const vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

        float dot(const vec3& o) const { return x * o.x + y * o.y + z * o.z; }
        float length() const { return std::sqrt(x * x + y * y + z * z); }
        float distance(const vec3& o) const { return (*this - o).length(); }
        bool finite() const
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z)
                && std::fabs(x) < 1e7f && std::fabs(y) < 1e7f && std::fabs(z) < 1e7f;
        }
        bool zero() const { return x == 0.f && y == 0.f && z == 0.f; }

        Aspect::Vector3 to_aspect() const { return Aspect::Vector3(x, y, z); }
        operator Aspect::Vector3() const { return to_aspect(); }
    };

    struct quat
    {
        float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

        quat() = default;
        quat(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}

        quat operator*(const quat& q) const
        {
            return {
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w,
                w * q.w - x * q.x - y * q.y - z * q.z
            };
        }

        vec3 rotate(const vec3& v) const;

        bool finite() const
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w)
                && std::fabs(x) < 2.f && std::fabs(y) < 2.f && std::fabs(z) < 2.f && std::fabs(w) < 2.f;
        }
    };

    inline vec3 quat::rotate(const vec3& v) const
    {
        const vec3 u{ x, y, z };
        const vec3 uv = vec3{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        const vec3 uuv = vec3{ u.y * uv.z - u.z * uv.y, u.z * uv.x - u.x * uv.z, u.x * uv.y - u.y * uv.x };
        return v + uv * (2.f * w) + uuv * 2.f;
    }

    // Unity's Quaternion.Euler(x, y, z) with angles in degrees. The game's
    // TransformModel stores Pitch/Yaw/Roll in degrees and its native Euler
    // helper converts to radians before composing, so match that here.
    inline quat quat_from_euler_deg(float x_deg, float y_deg, float z_deg)
    {
        constexpr float kDeg2Rad = 0.017453292519943295f;
        const float hx = x_deg * kDeg2Rad * 0.5f;
        const float hy = y_deg * kDeg2Rad * 0.5f;
        const float hz = z_deg * kDeg2Rad * 0.5f;

        const float cx = std::cos(hx), sx = std::sin(hx);
        const float cy = std::cos(hy), sy = std::sin(hy);
        const float cz = std::cos(hz), sz = std::sin(hz);

        const quat qx{ sx, 0.f, 0.f, cx };
        const quat qy{ 0.f, sy, 0.f, cy };
        const quat qz{ 0.f, 0.f, sz, cz };
        return qy * qx * qz;
    }

    // ---------------------------------------------------------------------
    // Memory access
    // ---------------------------------------------------------------------
    inline std::uintptr_t& ga_override()
    {
        static std::uintptr_t v = 0;
        return v;
    }
    inline void set_game_assembly_base(std::uintptr_t b) { ga_override() = b; }
    inline std::uintptr_t ga_base()
    {
        return ga_override() ? ga_override() : sdk::unity::game_assembly_base;
    }

    inline bool plausible(std::uintptr_t p)
    {
        return p >= 0x10000 && p < 0x7FFFFFFFFFFFULL && p != 0xFFFFFFFFFFFFFFFFULL;
    }
    inline bool obj_ptr(std::uintptr_t p) { return plausible(p) && (p & 7) == 0; }

    inline bool read_mem(std::uintptr_t addr, void* out, std::size_t n)
    {
        if (!driver || !addr || !n) return false;
        return driver->read(addr, out, n);
    }

    template <class T>
    inline T read_t(std::uintptr_t addr)
    {
        T v{};
        read_mem(addr, &v, sizeof(T));
        return v;
    }

    inline std::uintptr_t read_ptr(std::uintptr_t addr)
    {
        std::uintptr_t v = 0;
        if (!read_mem(addr, &v, sizeof(v))) return 0;
        return plausible(v) ? v : 0;
    }

    inline std::string read_cstr(std::uintptr_t addr, std::size_t max_len = 128)
    {
        std::string out;
        if (!plausible(addr)) return out;
        char buf[32];
        while (out.size() < max_len)
        {
            const std::size_t want = (std::min)(sizeof(buf), max_len - out.size());
            if (!read_mem(addr + out.size(), buf, want)) break;
            for (std::size_t i = 0; i < want; ++i)
            {
                if (buf[i] == '\0') return out;
                out.push_back(buf[i]);
            }
        }
        return out;
    }

    // ---------------------------------------------------------------------
    // Scatter batches
    //   VMMDLL_Scatter_ExecuteRead() handles a whole group of reads in one
    //   DMA transaction.  The handle is reusable: ExecuteReadScatter() clears
    //   it afterwards, so a single scope can run several dependent stages
    //   (pointer chase -> value read) with one round trip per stage instead of
    //   one round trip per read.
    // ---------------------------------------------------------------------
    class scatter_batch
    {
    public:
        // The handle is created lazily: worlds are constructed before the DMA
        // device/VMM is up, and one handle is reused for the whole process
        // (ExecuteReadScatter clears the request list after every run).
        scatter_batch() = default;
        ~scatter_batch() { if (driver && handle_) driver->close_scatter(handle_); }

        scatter_batch(const scatter_batch&) = delete;
        scatter_batch& operator=(const scatter_batch&) = delete;

        bool valid() const
        {
            if (!handle_ && driver) handle_ = driver->create_scatter();
            return handle_ != nullptr;
        }

        // Cumulative diagnostics: how much DMA traffic the per-frame refresh
        // actually issues. Cheap counters, read once per status log.
        std::uint64_t requests_total = 0;
        std::uint64_t runs_total = 0;
        // Cumulative wall time spent inside ExecuteReadScatter. Comparing this
        // against the worker's update time tells whether a slow refresh is
        // DMA-bound (many requests / high latency) or CPU-bound.
        double run_ms_total = 0.0;

        void add(std::uintptr_t addr, void* buffer, std::size_t size)
        {
            if (!valid()) return;
            if (addr && buffer && size)
            {
                driver->add_scatter_read(handle_, addr, buffer, size);
                ++requests_total;
            }
        }

        template <class T>
        void add(std::uintptr_t addr, T& out)
        {
            add(addr, &out, sizeof(T));
        }

        void run()
        {
            if (!valid()) return;
            const auto t0 = std::chrono::steady_clock::now();
            driver->execute_scatter(handle_);
            run_ms_total += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            ++runs_total;
        }

    private:
        mutable VMMDLL_SCATTER_HANDLE handle_ = nullptr;
    };

    inline const std::uint16_t* graph_offsets(const char* key, std::uint16_t& count)
    {
        return graph::lookup(key, count);
    }

    // ---------------------------------------------------------------------
    // IL2CPP class helpers
    // ---------------------------------------------------------------------
    struct klass_info
    {
        std::uintptr_t klass = 0;
        std::uint32_t instance_size = 0;
        std::uintptr_t element_class = 0;
        bool is_array = false;
        bool is_delegate = false;
        std::string name;       // simple name as stored by IL2CPP
        std::string ns;         // namespace (outermost for nested types)
        std::string full;       // "Namespace.Outer.Inner"
        const std::uint16_t* holder_offsets = nullptr;
        std::uint16_t holder_count = 0;
    };

    inline std::unordered_map<std::uintptr_t, klass_info>& klass_cache()
    {
        static std::unordered_map<std::uintptr_t, klass_info> cache;
        return cache;
    }

    inline std::uintptr_t multicast_delegate_klass();
    inline const klass_info* info_of_klass(std::uintptr_t klass);

    inline bool simple_name_is(std::uintptr_t klass, const char* expect)
    {
        if (!obj_ptr(klass) || !expect) return false;
        const std::uintptr_t np = read_ptr(klass + il2cpp::Class_Name);
        if (!plausible(np)) return false;
        return read_cstr(np, 96) == expect;
    }

    inline std::uintptr_t klass_from_rva(std::uintptr_t rva, const char* expect)
    {
        const std::uintptr_t base = ga_base();
        if (!base || !rva) return 0;

        // TypeInfo slots never move while the module is loaded, so resolve each
        // RVA once. Without this every per-frame camera / entity refresh paid a
        // pointer read plus a string compare per class.
        static std::unordered_map<std::uintptr_t, std::uintptr_t> rva_cache;
        const auto hit = rva_cache.find(rva);
        if (hit != rva_cache.end()) return hit->second;

        const std::uintptr_t slot = read_ptr(base + rva);
        if (slot && simple_name_is(slot, expect))
        {
            rva_cache.emplace(rva, slot);
            return slot;
        }

        // Some Unity 6 builds expose the metadata table as an array of class
        // pointers, so the RVA may need one more dereference.
        const std::uintptr_t extra = read_ptr(slot);
        if (extra && simple_name_is(extra, expect))
        {
            rva_cache.emplace(rva, extra);
            return extra;
        }
        return 0;
    }

    inline std::uintptr_t multicast_delegate_klass()
    {
        static std::uintptr_t k = 0;
        if (!k) k = klass_from_rva(rva::MulticastDelegate_TypeInfo, "MulticastDelegate");
        return k;
    }

    inline const klass_info* info_of_klass(std::uintptr_t klass)
    {
        if (!obj_ptr(klass)) return nullptr;

        auto& cache = klass_cache();
        const auto it = cache.find(klass);
        if (it != cache.end()) return &it->second;
        if (cache.size() > 65536) cache.clear();

        klass_info info;
        info.klass = klass;
        info.instance_size = read_t<std::uint32_t>(klass + il2cpp::Class_InstanceSize);
        info.element_class = read_ptr(klass + il2cpp::Class_ElementClass);

        const std::uintptr_t np = read_ptr(klass + il2cpp::Class_Name);
        if (!plausible(np)) return nullptr;
        info.name = read_cstr(np, 96);

        // Walk declaring types outwards, collecting names and the namespace.
        std::vector<std::string> parts;
        std::uintptr_t k = klass;
        std::string ns;
        for (int depth = 0; depth < 8 && obj_ptr(k); ++depth)
        {
            const std::uintptr_t name_ptr = read_ptr(k + il2cpp::Class_Name);
            if (!plausible(name_ptr)) break;
            parts.push_back(read_cstr(name_ptr, 96));
            const std::uintptr_t ns_ptr = read_ptr(k + il2cpp::Class_Namespace);
            if (ns.empty() && plausible(ns_ptr))
            {
                const std::string candidate = read_cstr(ns_ptr, 96);
                if (!candidate.empty()) ns = candidate;
            }
            k = read_ptr(k + il2cpp::Class_DeclaringType);
        }

        for (auto it2 = parts.rbegin(); it2 != parts.rend(); ++it2)
        {
            if (!info.full.empty()) info.full.push_back('.');
            info.full += *it2;
        }
        info.ns = ns;
        if (!ns.empty()) info.full = ns + "." + info.full;

        info.is_array = info.element_class != 0 && !info.name.empty()
                     && info.name.back() == ']';

        if (multicast_delegate_klass())
        {
            std::uintptr_t base_klass = klass;
            for (int depth = 0; depth < 32 && obj_ptr(base_klass); ++depth)
            {
                if (base_klass == multicast_delegate_klass()) { info.is_delegate = true; break; }
                base_klass = read_ptr(base_klass + il2cpp::Class_Parent);
            }
        }

        info.holder_count = 0;
        info.holder_offsets = graph_offsets(info.full.c_str(), info.holder_count);

        const auto res = cache.emplace(klass, std::move(info));
        return &res.first->second;
    }

    inline const klass_info* info_of(std::uintptr_t obj)
    {
        if (!obj_ptr(obj)) return nullptr;
        return info_of_klass(read_ptr(obj));
    }

    inline const char* class_name_of(std::uintptr_t obj)
    {
        const klass_info* i = info_of(obj);
        return i ? i->name.c_str() : "";
    }

    inline const char* class_full_name_of(std::uintptr_t obj)
    {
        const klass_info* i = info_of(obj);
        return i ? i->full.c_str() : "";
    }

    inline std::uintptr_t static_fields_of(std::uintptr_t klass)
    {
        if (!obj_ptr(klass)) return 0;
        // The static-field block is allocated once when the class is
        // initialised; cache it so the per-frame camera lookup stays at zero
        // extra round trips.
        static std::unordered_map<std::uintptr_t, std::uintptr_t> cache;
        const auto it = cache.find(klass);
        if (it != cache.end()) return it->second;
        const std::uintptr_t sf = read_ptr(klass + il2cpp::Class_StaticFields);
        if (sf) cache.emplace(klass, sf);
        return sf;
    }

    // klass (by RVA) + static fields + slot offset
    inline std::uintptr_t static_value(std::uintptr_t rva, const char* expect, std::uintptr_t slot)
    {
        const std::uintptr_t klass = klass_from_rva(rva, expect);
        if (!klass) return 0;
        const std::uintptr_t sf = static_fields_of(klass);
        if (!sf) return 0;
        return read_ptr(sf + slot);
    }

    // ---------------------------------------------------------------------
    // Replication primitives
    //   IPrimitiveData<T>   : _value @ 0x18, stored INLINE for value types
    //   IPrimitiveModel<T>  : depends on the concrete class
    //
    // Older code treated _value as a pointer to a boxed T and read at
    // box + 0x10. To keep those call sites working without a pointer chase,
    // prim_boxed() returns an address such that (address + 0x10) is the value:
    // address = prim + PrimitiveData_Value - 0x10.
    // ---------------------------------------------------------------------
    inline bool prim_boxed(std::uintptr_t prim, std::uintptr_t& out)
    {
        out = prim ? prim + (il2cpp::PrimitiveData_Value - 0x10) : 0;
        return prim != 0;
    }

    inline bool prim_bool(std::uintptr_t prim, bool& out)
    {
        std::uintptr_t box = 0;
        if (!prim_boxed(prim, box)) return false;
        std::uint8_t v = 0;
        if (!read_mem(box + 0x10, &v, 1)) return false;
        out = v != 0;
        return true;
    }

    inline bool prim_int(std::uintptr_t prim, std::int32_t& out)
    {
        std::uintptr_t box = 0;
        if (!prim_boxed(prim, box)) return false;
        return read_mem(box + 0x10, &out, sizeof(out));
    }

    inline bool prim_float(std::uintptr_t prim, float& out)
    {
        std::uintptr_t box = 0;
        if (!prim_boxed(prim, box)) return false;
        return read_mem(box + 0x10, &out, sizeof(out));
    }

    inline bool prim_vec3(std::uintptr_t prim, vec3& out)
    {
        std::uintptr_t box = 0;
        if (!prim_boxed(prim, box)) return false;
        return read_mem(box + 0x10, &out, sizeof(out));
    }

    inline std::uintptr_t prim_ref(std::uintptr_t prim)
    {
        return read_ptr(prim + il2cpp::PrimitiveData_Value);
    }

    // IPrimitiveModel<T> has two concrete shapes:
    //
    //   NoReplicationPrimitiveModel<T>   _Value @ +0x10  -> boxed T / object
    //   PrimitiveModel<T>                _data  @ +0x10  -> IPrimitiveData<T>
    //                                                     -> _value @ +0x18 (boxed)
    //
    // Instead of trusting the model's own class name (subclasses such as
    // ReplicatedPrimitiveModel<T> would be misread), classify by the object the
    // +0x10 pointer actually references.  IPrimitiveData<T> comes from
    // Framework.Replication.Data.Primitive and always carries "PrimitiveData"
    // in its class name.
    inline bool pm_value(std::uintptr_t model, std::uintptr_t& value_box)
    {
        const std::uintptr_t inner = read_ptr(model + il2cpp::PrimitiveModel_Value);
        if (!obj_ptr(inner)) return false;

        const klass_info* ki = info_of(inner);
        const bool is_data = ki
            && (ki->name.find("PrimitiveData") != std::string::npos
                || ki->full.find("PrimitiveData") != std::string::npos);

        if (is_data)
        {
            // inner is an IPrimitiveData<T>: _value is inline at +0x18.
            value_box = inner + (il2cpp::PrimitiveData_Value - 0x10);
            return true;
        }

        // inner *is* the boxed value (or the referenced object for class Ts).
        value_box = inner;
        return true;
    }

    inline bool pm_bool(std::uintptr_t model, bool& out)
    {
        std::uintptr_t box = 0;
        if (!pm_value(model, box)) return false;
        std::uint8_t v = 0;
        if (!read_mem(box + 0x10, &v, 1)) return false;
        out = v != 0;
        return true;
    }

    inline bool pm_int(std::uintptr_t model, std::int32_t& out)
    {
        std::uintptr_t box = 0;
        if (!pm_value(model, box)) return false;
        return read_mem(box + 0x10, &out, sizeof(out));
    }

    inline std::uintptr_t pm_ref(std::uintptr_t model)
    {
        // pm_value already unwraps IPrimitiveData<T>; for reference-typed Ts the
        // stored value is the object reference itself in both shapes.
        std::uintptr_t value = 0;
        if (!pm_value(model, value)) return 0;
        return value;
    }

    // ---------------------------------------------------------------------
    // Strings / containers
    // ---------------------------------------------------------------------
    inline std::string read_il2cpp_string(std::uintptr_t str, std::size_t max_chars = 48)
    {
        std::string out;
        if (!obj_ptr(str)) return out;
        const std::int32_t len = read_t<std::int32_t>(str + il2cpp::String_Length);
        if (len <= 0 || len > 512) return out;
        const std::size_t n = (std::min)(static_cast<std::size_t>(len), max_chars);
        std::vector<char16_t> buf(n);
        if (!read_mem(str + il2cpp::String_Chars, buf.data(), n * sizeof(char16_t))) return out;
        for (char16_t c : buf)
        {
            if (c == 0) break;
            out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        return out;
    }

    template <class Fn>
    inline void for_each_list(std::uintptr_t list, Fn&& fn, int max_items = 4096)
    {
        if (!obj_ptr(list)) return;
        const std::uintptr_t items = read_ptr(list + il2cpp::List_Items);
        const std::int32_t size = read_t<std::int32_t>(list + il2cpp::List_Size);
        if (!obj_ptr(items) || size <= 0) return;
        const int n = (std::min)(size, max_items);
        std::vector<std::uintptr_t> buf(static_cast<std::size_t>(n));
        if (!read_mem(items + il2cpp::Array_Data, buf.data(), buf.size() * sizeof(std::uintptr_t))) return;
        for (std::uintptr_t v : buf)
            if (obj_ptr(v)) fn(v);
    }

    template <class Fn>
    inline void for_each_dict(std::uintptr_t dict, Fn&& fn, int max_entries = 8192)
    {
        if (!obj_ptr(dict)) return;
        const std::uintptr_t entries = read_ptr(dict + il2cpp::Dictionary_Entries);
        const std::int32_t count = read_t<std::int32_t>(dict + il2cpp::Dictionary_Count);
        if (!obj_ptr(entries) || count <= 0) return;
        const int n = (std::min)(count, max_entries);
        const std::size_t stride = il2cpp::Dictionary_EntryStride;
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(n) * stride);
        if (!read_mem(entries + il2cpp::Array_Data, buf.data(), buf.size())) return;
        for (int i = 0; i < n; ++i)
        {
            const std::uint8_t* e = buf.data() + static_cast<std::size_t>(i) * stride;
            std::int32_t hash = 0;
            std::int32_t key = 0;
            std::uintptr_t value = 0;
            std::memcpy(&hash, e + il2cpp::Dictionary_EntryHashCode, 4);
            if (hash < 0) continue;
            std::memcpy(&key, e + il2cpp::Dictionary_EntryKey, 4);
            std::memcpy(&value, e + il2cpp::Dictionary_EntryValue, sizeof(value));
            if (!obj_ptr(value)) continue;
            fn(key, value);
        }
    }

    // ---------------------------------------------------------------------
    // Diagnostics
    // ---------------------------------------------------------------------
    inline void log(const std::string& line)
    {
        ::robrun_log::write("[robrun] " + line);
    }
}
