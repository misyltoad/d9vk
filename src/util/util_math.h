#pragma once

#include <cmath>

namespace dxvk {
  
  constexpr size_t CACHE_LINE_SIZE = 64;
  
  template<typename T>
  constexpr T clamp(T n, T lo, T hi) {
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
  }
  
  template<typename T, typename U = T>
  constexpr T align(T what, U to) {
    return (what + to - 1) & ~(to - 1);
  }

  // Equivelant of std::clamp for use with floating point numbers
  // Handles (-){INFINITY,NAN} cases.
  inline float fclamp(float value, float min, float max) {
    return std::fmax(
      std::fmin(value, max), min);
  }
  
}