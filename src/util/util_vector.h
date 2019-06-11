#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>

namespace dxvk {

  struct Vector4 {
    Vector4();
    explicit Vector4(float splat);
    Vector4(float x, float y, float z, float w);
    Vector4(float xyzw[4]);
    Vector4(const Vector4& other) = default;

    inline       float& operator[](size_t index)       { return data[index]; }
    inline const float& operator[](size_t index) const { return data[index]; }

    bool operator==(const Vector4& other) const;
    bool operator!=(const Vector4& other) const;
	
    Vector4 operator-() const;

    Vector4 operator+(const Vector4& other) const;

    Vector4 operator-(const Vector4& other) const;

    Vector4 operator*(float scalar) const;
    Vector4 operator*(const Vector4& other) const;

    Vector4 operator/(const Vector4& other) const;
    Vector4 operator/(float scalar) const;

    Vector4& operator+=(const Vector4& other);
    Vector4& operator-=(const Vector4& other);
    Vector4& operator*=(float scalar);
    Vector4& operator/=(float scalar);

    union {
      float data[4];
      struct {
        float x, y, z, w;
      };
      struct {
        float r, g, b, a;
      };
    };

  };

  inline Vector4 operator*(float scalar, const Vector4& vector) {
    return vector * scalar;
  }

  float dot(const Vector4& a, const Vector4& b);

  float lengthSqr(const Vector4& a);
  float length(const Vector4& a);
  Vector4 normalize(const Vector4& a);

  inline Vector4 replaceNaN(Vector4 a, float value) {
    for (uint32_t i = 0; i < 4; i++) {
      if (std::isnan(a[i]))
        a[i] = value;
    }

    return a;
  }

  inline Vector4 replaceNaN(Vector4 a) {
    return replaceNaN(a, 0.0f);
  }

  std::ostream& operator<<(std::ostream& os, const Vector4& v);

}