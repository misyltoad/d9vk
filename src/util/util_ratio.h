#include <numeric>
#include <algorithm>
#include <cstdint>
#include <compare>

namespace dxvk {

  /**
   * \brief Simplest ratio helper
   */
  template <typename T>
  class Ratio {

  public:

    Ratio(T top, T bottom) {
      const T gcd = std::gcd(top, bottom);

      m_top    = top    / gcd;
      m_bottom = bottom / gcd;
    }

    inline T top()    const { return m_top; }
    inline T bottom() const { return m_bottom; }

    inline bool operator == (const Ratio& other) const {
      return top() == other.top() && bottom() == other.bottom();
    }

    inline bool operator != (const Ratio& other) const {
      return !(*this == other);
    }

    inline std::strong_ordering operator <=> (const Ratio& other) const {
      return top() * other.bottom() <=> other.top() * bottom();
    }

  private:

    T m_top, m_bottom;

  };

}