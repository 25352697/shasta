#ifndef CZI_NANOPORE2_UINT_HPP
#define CZI_NANOPORE2_UINT_HPP

// Template class used to implement bare bones integer types
// that represent integers with any number of bytes
// less than 16 which is not a power of two.
// They support no operations except for conversions
// to the a longer built-in integer type.

#include "array.hpp"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ChanZuckerberg {
    namespace Nanopore2 {

        template<int N, class UintHelper> class Uint;

        using Uint24  = Uint< 3,   uint32_t>;
        using Uint40  = Uint< 5,   uint64_t>;
        using Uint48  = Uint< 6,   uint64_t>;
        using Uint56  = Uint< 7,   uint64_t>;
        using Uint72  = Uint< 9, __uint128_t>;
        using Uint80  = Uint<10, __uint128_t>;
        using Uint88  = Uint<11, __uint128_t>;
        using Uint96  = Uint<12, __uint128_t>;
        using Uint104 = Uint<13, __uint128_t>;
        using Uint112 = Uint<14, __uint128_t>;
        using Uint120 = Uint<15, __uint128_t>;
    }
}



// N = number of bytes stored.
// UintHelper = built-in integer type that Uint converts to-from.
// UintHelper must be an unsigned integer type at least N bytes long.

template<int N, class UintHelper> class ChanZuckerberg::Nanopore2::Uint {
public:

    Uint(const UintHelper& i)
    {
        memcpy(&data, &i, N);
    }
    Uint() {}

    operator UintHelper() const
    {
        UintHelper i = UintHelper(0);
        memcpy(&i, &data, N);
        return i;
    }

private:
    array<uint8_t, N> data;

    static_assert(std::is_integral<UintHelper>::value, "UintHelper must be an integral type.");
    static_assert(std::is_unsigned<UintHelper>::value, "UintHelper must be unsigned.");
    static_assert(sizeof(UintHelper)>=N, "UintHelper is too short for this N.");
};

#endif
