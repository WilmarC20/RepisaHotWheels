#pragma once

#include <Arduino.h>

/* Matriz 6×8, serpentina desde abajo-izquierda. x=0 izquierda, y=0 abajo (48 LEDs, índices 0–47). */
namespace Matrix6x8 {
static constexpr uint8_t W = 6;
static constexpr uint8_t H = 8;
static constexpr uint16_t NUM = W * H;

inline uint16_t xyToIndex(uint8_t x, uint8_t y) {
   if (x >= W || y >= H) {
      return 0;
   }
   const uint16_t base = (uint16_t)y * W;
   return (y & 1u) ? (uint16_t)(base + (W - 1u - x)) : (uint16_t)(base + x);
}
} // namespace Matrix6x8
