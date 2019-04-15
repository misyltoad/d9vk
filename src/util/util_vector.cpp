#include "util_vector.h"

#include <cmath>

namespace dxvk {

  Vector4::Vector4()
    : x(0.0f), y(0.0f), z(0.0f), w(0.0f) { }

  Vector4::Vector4(float splat)
    : x(splat), y(splat), z(splat), w(splat) { }

  Vector4::Vector4(float x, float y, float z, float w)
    : x(x), y(y), z(z), w(w) { }

  Vector4::Vector4(float xyzw[4])
    : x(xyzw[0]), y(xyzw[1]), z(xyzw[2]), w(xyzw[3]) { }

  bool Vector4::operator==(const Vector4& other) const {
    for (uint32_t i = 0; i < 4; i++) {
      if (data[i] != other.data[i])
      return false;
    }

    return true;
  }

  bool Vector4::operator!=(const Vector4& other) const {
    return !operator==(other);
  }

  Vector4 Vector4::operator-() const { return {-x, -y, -z, -w}; }

  Vector4 Vector4::operator+(const Vector4& other) const {
    return {x + other.x, y + other.y, z + other.z, w + other.w};
  }

  Vector4 Vector4::operator-(const Vector4& other) const {
    return {x - other.x, y - other.y, z - other.z, w - other.w};
  }

  Vector4 Vector4::operator*(float scalar) const {
    return {scalar * x, scalar * y, scalar * z, scalar * w};
  }

  Vector4 Vector4::operator*(const Vector4& other) const {
    Vector4 result;
    for (uint32_t i = 0; i < 4; i++)
      result[i] = data[i] * other.data[i];
    return result;
  }

  Vector4 Vector4::operator/(const Vector4& other) const {
    Vector4 result;
    for (uint32_t i = 0; i < 4; i++)
      result[i] = data[i] / other.data[i];
    return result;
  }

  Vector4 Vector4::operator/(float scalar) const {
    return {x / scalar, y / scalar, z / scalar, w / scalar};
  }

  Vector4& Vector4::operator+=(const Vector4& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;

    return *this;
  }

  Vector4& Vector4::operator-=(const Vector4& other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;

    return *this;
  }

  Vector4& Vector4::operator*=(float scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;

    return *this;
  }

  Vector4& Vector4::operator/=(float scalar) {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;

    return *this;
  }

  float dot(const Vector4& a, const Vector4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  }

  float lengthSqr(const Vector4& a) { return dot(a, a); }

  float length(const Vector4& a) { return sqrtf(lengthSqr(a)); }

  Vector4 normalize(const Vector4& a) { return a * (1.0f / length(a)); }

  std::ostream& operator<<(std::ostream& os, const Vector4& v) {
    return os << "Vector4(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  }

}