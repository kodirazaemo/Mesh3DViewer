#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace meshviewer {

inline std::string HResultText(HRESULT hr, const char* call) {
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08X)", call,
                static_cast<unsigned>(hr));
  return buffer;
}

inline void ThrowIfFailed(HRESULT hr, const char* call) {
  if (FAILED(hr)) {
    throw std::runtime_error(HResultText(hr, call));
  }
}

}  // namespace meshviewer
