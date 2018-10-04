#ifndef CZI_SHASTA_ALIGNMENT_HPP
#define CZI_SHASTA_ALIGNMENT_HPP

#include "OrientedReadPair.hpp"
#include "ReadId.hpp"

#include "algorithm.hpp"
#include "array.hpp"
#include "cstdint.hpp"
#include "utility.hpp"
#include "vector.hpp"

namespace ChanZuckerberg {
    namespace shasta {

        class Alignment;
        class AlignmentData;
        class AlignmentInfo;
    }
}



class ChanZuckerberg::shasta::Alignment {
public:

    // The ordinals in each of the two oriented reads of the
    // markers in the alignment.
    vector< pair<uint32_t, uint32_t> > ordinals;
};



class ChanZuckerberg::shasta::AlignmentInfo {
public:

    // The first and last ordinals in each of the two oriented reads.
    // These are not filled in if markerCount is zero.
    pair<uint32_t, uint32_t> firstOrdinals;
    pair<uint32_t, uint32_t> lastOrdinals;

    // The number of markers in the alignment.
    uint32_t markerCount;

    AlignmentInfo(const Alignment& alignment)
    {
        create(alignment);
    }
    void create(const Alignment& alignment)
    {
        markerCount = uint32_t(alignment.ordinals.size());
        if(markerCount) {
            firstOrdinals = alignment.ordinals.front();
            lastOrdinals  = alignment.ordinals.back();
        }
    }
    AlignmentInfo() {}

    // Update the alignment to reflect a swap the two oriented reads.
    void swap()
    {
        std::swap(firstOrdinals.first, firstOrdinals.second);
        std::swap(lastOrdinals.first, lastOrdinals.second);
    }

    // Update the alignment to reflect reverse complementing of the two reads.
    // Takes as input the total number of markers in each read.
    void reverseComplement(
        uint32_t markerCount0,
        uint32_t markerCount1)
    {
        std::swap(firstOrdinals.first, lastOrdinals.first);
        firstOrdinals.first = markerCount0 - 1 - firstOrdinals.first;
        lastOrdinals.first  = markerCount0 - 1 - lastOrdinals.first;

        std::swap(firstOrdinals.second, lastOrdinals.second);
        firstOrdinals.second = markerCount1 - 1 - firstOrdinals.second;
        lastOrdinals.second  = markerCount1 - 1 - lastOrdinals.second;

    }



    // Compute the left and right trim, expressed in markers.
    // This is the minimum number of markers (over the two oriented reads)
    // that are excluded from the alignment on each side.
    // If the trim is too high, the alignment is suspicious.
    pair<uint32_t, uint32_t> computeTrim(
        uint32_t markerCount0,
        uint32_t markerCount1) const
    {
        if(markerCount) {

            // The alignment is not empty.
            const uint32_t leftTrim = min(
                firstOrdinals.first,
                firstOrdinals.second);
            const uint32_t rightTrim = min(
                markerCount0 - 1 - lastOrdinals.first,
                markerCount1 - 1 - lastOrdinals.second);
            return make_pair(leftTrim, rightTrim);

        } else {

            // The alignment is empty.
            // The trim on each side equals the number of markers
            // of the shortest read.
            const uint32_t trim = min(markerCount0, markerCount1);
            return make_pair(trim, trim);

        }

    }
};



class ChanZuckerberg::shasta::AlignmentData :
    public ChanZuckerberg::shasta::OrientedReadPair{
public:

    // The AlignmentInfo computed with the first read on strand 0.
    AlignmentInfo info;

    AlignmentData() {}
    AlignmentData(
        const array<ReadId, 2>& readIds,
        bool isSameStrand,
        const AlignmentInfo& info) :
        OrientedReadPair(readIds, isSameStrand),
        info(info)
    {}
    AlignmentData(
        const OrientedReadPair& orientedReadPair,
        const AlignmentInfo& info) :
        OrientedReadPair(orientedReadPair),
        info(info)
    {}

};

#endif
