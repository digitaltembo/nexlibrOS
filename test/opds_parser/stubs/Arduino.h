#pragma once

#include <cstddef>

#include <cstdint>

struct EspHostStub {
  size_t getMaxAllocHeap() const { return 1024 * 1024; }
  uint32_t getFreeHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;
