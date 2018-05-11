#ifndef CZI_NANOPORE2_OVERLAP_FINDER_HPP
#define CZI_NANOPORE2_OVERLAP_FINDER_HPP

// Nanopore2
#include "Marker.hpp"
#include "Overlap.hpp"
#include "MemoryMappedVectorOfVectors.hpp"
#include "MultitreadedObject.hpp"

namespace ChanZuckerberg {
    namespace Nanopore2 {
        class OverlapFinder;
    }
}



// This class uses the MinHash algorithm to find pairs
// of overlapping oriented reads. It uses as features
// sequences of m consecutive markers.
class ChanZuckerberg::Nanopore2::OverlapFinder :
    public MultithreadedObject<OverlapFinder>{
public:

    // The constructor does all the work.
    OverlapFinder(
        size_t m,                       // Number of consecutive markers that define a feature.
        size_t minHashIterationCount,   // Number of minHash iterations.
        size_t log2MinHashBucketCount,  // Base 2 log of number of buckets for minHash.
        size_t minFrequency,            // Minimum number of minHash hits for a pair to be considered an overlap.
        size_t threadCount,
        const MemoryMapped::Vector<KmerInfo>& kmerTable,
        const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& compressedMarkers,
        MemoryMapped::Vector<Overlap>& overlaps,
        MemoryMapped::VectorOfVectors<uint64_t, uint64_t>& overlapTable,
        const string& largeDataFileNamePrefix,
        size_t largeDataPageSize
);

private:

    // Store some of the arguments passed to the constructor.
    size_t m;                       // Number of consecutive markers that define a feature.
    const MemoryMapped::Vector<KmerInfo>& kmerTable;
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& compressedMarkers;
    const string& largeDataFileNamePrefix;
    size_t largeDataPageSize;

    // Vectors containing only the k-mer ids of all markers
    // for all oriented reads (noit just fir reads).
    // Indexed by OrientedReadId.getValue().
    // This is used to speed up the computation of hash functions.
    MemoryMapped::VectorOfVectors<KmerId, uint64_t> markers;
    void createMarkers();

    // The current MinHash iteration.
    // This is used to compute a different MurmurHash function
    // at each iteration.
    size_t iteration;

    // The bucket that each oriented read belongs to
    // at the current MinHash iteration.
    vector<uint32_t> orientedReadBucket;
    void computeBuckets(size_t threadId);

    // The mask used to compute a bucket.
    uint32_t mask;

    // The buckets containing oriented read ids (stored as read ids).
    vector< vector<ReadId> > buckets;

    // Inspect the buckets to find overlap candidates.
    void inspectBuckets(size_t threadId);

    // Data structure used to store overlap candidates.
    // Indexed by oriented read id of the lower numbered
    // oriented read in the pair.
    // It contains the oriented read id of the higher
    // numbered oriented read in the pair, and the number of
    // MinHash iteration that found this pair.
    // Each of these will generate two overlaps, if the
    // frequency is sufficiently high.
    vector< vector< pair<ReadId, uint32_t> > > overlapCandidates;
};

#endif
