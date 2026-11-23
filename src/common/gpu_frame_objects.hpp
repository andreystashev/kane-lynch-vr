#pragma once
#include <windows.h>
namespace GPUFrame {
template <class T> struct Object {
  T *p = nullptr;
  Object() = default;
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  ~Object() { Reset(); }
  void Reset() {
    if (p)
      p->Release();
    p = nullptr;
  }
  T **Put() {
    Reset();
    return &p;
  }
  T *operator->() const { return p; }
};
struct Handle {
  HANDLE p = nullptr;
  Handle() = default;
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  ~Handle() { Reset(); }
  void Reset() {
    if (p && p != INVALID_HANDLE_VALUE)
      CloseHandle(p);
    p = nullptr;
  }
};
} // namespace GPUFrame
