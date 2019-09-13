#ifndef SHASTA_READ_LOADER_HPP
#define SHASTA_READ_LOADER_HPP

// shasta
#include "LongBaseSequence.hpp"
#include "MemoryMappedObject.hpp"
#include "MultitreadedObject.hpp"

// Standard library.
#include "memory.hpp"
#include "string.hpp"

namespace shasta {
    class ReadLoader;
}



// Class used to load reads from a fasta file.
class shasta::ReadLoader :
    public MultithreadedObject<ReadLoader>{
public:

    // The constructor does all the work.
    ReadLoader(
        const string& fileName,
        size_t minReadLength,
        size_t threadCountForReading,
        size_t threadCountForProcessing,
        const string& dataNamePrefix,
        size_t pageSize,
        LongBaseSequences& reads,
        MemoryMapped::VectorOfVectors<char, uint64_t>& readNames,
        MemoryMapped::VectorOfVectors<uint8_t, uint64_t>& readRepeatCounts);

    // The number of reads and raw bases discarded because the read length
    // was less than minReadLength.
    uint64_t discardedShortReadReadCount = 0;
    uint64_t discardedShortReadBaseCount = 0;

    // The number of reads and raw bases discarded because the read
    // contained repeat counts greater than 255.
    uint64_t discardedBadRepeatCountReadCount = 0;
    uint64_t discardedBadRepeatCountBaseCount = 0;

private:

    // The name of the file we are processing.
    const string& fileName;

    // The minimum read length. Shorter reads are not stored.
    const size_t minReadLength;

    // The number of threads to be used for read and for processing.
    size_t threadCountForReading;
    size_t threadCountForProcessing;
    void adjustThreadCounts();

    // Information that we can use to create temporary
    // memory mapped binary data structures.
    const string& dataNamePrefix;
    const size_t pageSize;

    // The data structure that the reads will be added to.
    LongBaseSequences& reads;
    MemoryMapped::VectorOfVectors<char, uint64_t>& readNames;
    MemoryMapped::VectorOfVectors<uint8_t, uint64_t>& readRepeatCounts;

    // Functions specific to each file format.
    void processCompressedRunnieFile();
};



#endif
