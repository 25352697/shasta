#ifndef CZI_NANOPORE2_KMER_HPP
#define CZI_NANOPORE2_KMER_HPP

#include "ShortBaseSequence.hpp"
#include <limits>

namespace ChanZuckerberg {
    namespace Nanopore2 {


        // Types used to represent a k-mer and a k-mer id.
        // These limit the maximum k-mer length that can be used.
        using Kmer = ShortBaseSequence8;
        using KmerId = uint16_t;

        // Check for consistency of these two types.
        static_assert(
            std::numeric_limits<KmerId>::digits == 2*Kmer::capacity,
            "Kmer and KmerId types are inconsistent.");
    }
}

#endif
