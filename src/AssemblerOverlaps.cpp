#include "Assembler.hpp"
#include "OverlapFinder.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;




// Use the minHash algorithm to find pairs of overlapping oriented reads.
// Use as features sequences of m consecutive special k-mers.
void Assembler::findOverlaps(
    size_t m,                       // Number of consecutive k-mers that define a feature.
    size_t minHashIterationCount,   // Number of minHash iterations.
    size_t log2MinHashBucketCount,  // Base 2 log of number of buckets for minHash.
    size_t maxBucketSize,           // The maximum size for a bucket to be used.
    size_t minFrequency,            // Minimum number of minHash hits for a pair to become a candidate.
    size_t threadCount
)
{
    checkKmersAreOpen();
    checkMarkersAreOpen();
    const ReadId readCount = ReadId(markers0.size());

    // Check that log2MinHashBucketCount is not unreasonably small.
    if((1ULL << (log2MinHashBucketCount-3ULL)) < readCount) {
        throw runtime_error("log2MinHashBucketCount is unreasonably small.\n"
            "Must at least equal base 2 log of number of reads plus 3.");
    }

    // Create the overlaps and overlap table.
    overlaps.createNew(largeDataName("Overlaps"), largeDataPageSize);
    overlapTable.createNew(largeDataName("OverlapTable"), largeDataPageSize);

    // Call the OverlapFinder to do the MinHash computation.
    OverlapFinder overlapFinder(
        m,
        minHashIterationCount,
        log2MinHashBucketCount,
        maxBucketSize,
        minFrequency,
        threadCount,
        kmerTable,
        markers0,
        overlaps,
        overlapTable,
        largeDataFileNamePrefix,
        largeDataPageSize);
}



void Assembler::accessOverlaps()
{
    overlaps.accessExistingReadOnly(largeDataName("Overlaps"));
    overlapTable.accessExistingReadOnly(largeDataName("OverlapTable"));
}


void Assembler::checkOverlapsAreOpen() const
{
    if(!overlaps.isOpen || !overlapTable.isOpen()) {
        throw runtime_error("Overlaps are not accessible.");
    }
}



// Write the reads that overlap a given read.
void Assembler::writeOverlappingReads(
    ReadId readId0,
    Strand strand0,
    const string& fileName)
{
    // Check that we have what we need.
    checkReadsAreOpen();
    checkOverlapsAreOpen();



    // Open the output file and write the oriented read we were given.
    ofstream file(fileName);
    const OrientedReadId orientedReadId0(readId0, strand0);
    writeOrientedRead(orientedReadId0, file);

    const uint64_t length0 = reads[orientedReadId0.getReadId()].baseCount;
    cout << "Reads overlapping " << orientedReadId0 << " length " << length0 << endl;

    // Loop over all overlaps involving this oriented read.
    for(const uint64_t i: overlapTable[orientedReadId0.getValue()]) {
        const Overlap& overlap = overlaps[i];

        // Get the other oriented read involved in this overlap.
        const OrientedReadId orientedReadId1 = overlap.getOther(orientedReadId0);

        // Write it out.
        const uint64_t length1 = reads[orientedReadId1.getReadId()].baseCount;
        cout << orientedReadId1 << " length " << length1 << " frequency " << overlap.minHashFrequency << endl;
        writeOrientedRead(orientedReadId1, file);
    }
    cout << "Found " << overlapTable[orientedReadId0.getValue()].size();
    cout << " overlapping oriented reads." << endl;

}


