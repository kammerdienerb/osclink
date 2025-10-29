#pragma once

#include <cstdint>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

template<typename F>
class Defer_Finalizer {
    F f;
    bool moved;
  public:
    template<typename T>
    Defer_Finalizer(T && f_) : f(std::forward<T>(f_)), moved(false) { }

    Defer_Finalizer(const Defer_Finalizer &) = delete;

    Defer_Finalizer(Defer_Finalizer && other) : f(std::move(other.f)), moved(other.moved) {
        other.moved = true;
    }

    ~Defer_Finalizer() {
        if (!moved) f();
    }
};

struct {
    template<typename F>
    Defer_Finalizer<F> operator<<(F && f) {
        return Defer_Finalizer<F>(std::forward<F>(f));
    }
} deferrer __attribute__((used));

#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define DEFER auto TOKENPASTE2(__deferred_lambda_call, __COUNTER__) = deferrer << [&]
