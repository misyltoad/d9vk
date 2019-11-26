#include <numeric>
#include <algorithm>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Simplest ratio helper
   */
  template <typename T>
  class Ratio {

  public:

    Ratio(T num, T denom) {
      const T gcd = std::gcd(num, denom);

      m_num    = num   / gcd;
      m_denom  = denom / gcd;
    }

    inline T num()    const { return m_num; }
    inline T denom() const { return m_denom; }

    inline bool operator == (const Ratio& other) const {
      return num() == other.num() && denom() == other.denom();
    }

    inline bool operator != (const Ratio& other) const {
      return !(*this == other);
    }

    inline bool operator >= (const Ratio& other) const {
      return num() * other.denom() >= other.num() * denom();
    }

    inline bool operator > (const Ratio& other) const {
      return num() * other.denom() > other.num() * denom();
    }

    inline bool operator < (const Ratio& other) const {
      return  !(*this >= other);
    }

    inline bool operator <= (const Ratio& other) const {
      return  !(*this > other);
    }

  private:

    T m_num, m_denom;

  };

}