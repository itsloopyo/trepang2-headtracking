// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// One UFUNCTION, resolved by name once and dispatched through ProcessEvent.
//
// Every parameter slot is located through the engine's reflection data and
// checked to be at least as wide as the C++ type written into or read out of
// it, so a frame is never written at a guessed offset.
namespace t2_ht::ue_call {

struct Param {
    const char* Name;
    std::size_t Bytes;
};

class Function {
public:
    // Finds `outer`::`name` in the object table and checks every parameter.
    // Logs and returns false on a missing function or a slot that does not fit.
    bool Resolve(const char* outer, const char* name, std::initializer_list<Param> params);

    bool Ready() const { return m_fn != 0; }
    std::size_t FrameSize() const { return m_size; }
    std::size_t Offset(std::size_t index) const { return m_offsets[index]; }

    // `frame` must be FrameSize() bytes, already filled. False when the
    // dispatch faulted.
    bool Call(std::uintptr_t self, unsigned char* frame) const;

    static constexpr std::size_t kMaxFrame = 1024;

private:
    std::uintptr_t m_fn = 0;
    std::size_t m_size = 0;
    std::vector<std::size_t> m_offsets;
};

// A reusable, zeroed parameter frame for one Function.
class Frame {
public:
    explicit Frame(const Function& fn) : m_fn(fn) { std::memset(m_buf, 0, fn.FrameSize()); }

    template <typename T>
    void Set(std::size_t index, const T& value) {
        std::memcpy(m_buf + m_fn.Offset(index), &value, sizeof(T));
    }
    template <typename T>
    T Get(std::size_t index) const {
        T value{};
        std::memcpy(&value, m_buf + m_fn.Offset(index), sizeof(T));
        return value;
    }
    unsigned char* Data() { return m_buf; }
    const unsigned char* At(std::size_t index) const { return m_buf + m_fn.Offset(index); }
    bool Call(std::uintptr_t self) { return m_fn.Call(self, m_buf); }

private:
    const Function& m_fn;
    alignas(16) unsigned char m_buf[Function::kMaxFrame];
};

// Class default object of a native class, e.g. "KismetSystemLibrary".
std::uintptr_t DefaultObject(const char* className);

// The class of a live object, or 0.
std::uintptr_t ClassOf(std::uintptr_t obj);

// True when `className` is the object's class or one of its super classes.
// Dispatching a UFUNCTION on an object of another class runs its exec thunk on
// the wrong layout, so a caller that cannot be sure of the class asks this first.
bool IsA(std::uintptr_t obj, const char* className);

// The FUObjectItem for a global object index, or 0 when the index is outside
// the table or the walk to it does not read. The object itself is the first
// pointer in the item.
std::uintptr_t ObjectItem(std::int32_t objectIndex);

// The object a TWeakObjectPtr {ObjectIndex, SerialNumber} names, or 0 when the
// slot is empty or reused.
std::uintptr_t ResolveWeak(std::int32_t objectIndex, std::int32_t serialNumber);

}  // namespace t2_ht::ue_call
