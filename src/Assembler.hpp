#ifndef CZI_NANOPORE2_ASSEMBLER_HPP
#define CZI_NANOPORE2_ASSEMBLER_HPP

// Nanopore2
#include "Kmer.hpp"
#include "LongBaseSequence.hpp"
#include "Marker.hpp"
#include "MemoryMappedObject.hpp"
#include "MultitreadedObject.hpp"
#include "ReadId.hpp"

// Standard library.
#include "string.hpp"

namespace ChanZuckerberg {
    namespace Nanopore2 {
        class Assembler;
        class AssemblerInfo;
    }
}



// Class used to store various pieces of assembler information in shared memory.
class ChanZuckerberg::Nanopore2::AssemblerInfo {
public:

    // The length of k-mers used to define markers.
    size_t k;
};



class ChanZuckerberg::Nanopore2::Assembler :
    public MultithreadedObject<Assembler> {
public:



    // The constructor specifies the file name prefixes for binary data files.
    // There are two prefixes, one used for small data and one used for large data.
    // If these are directory names, they must include the final "/".
    // It also specifies the page size for small and large binary data files.
    // Typically, small binary data files will reside in a regular
    // directory on disk or on /dev/shm mapped backed by 4K pages,
    // while large binary data file will reside in a huge page
    // file system backed by 2MB pages.
    // 1GB huge pages are also supported.
    // The page sizes specified here must be equal to, or be an exact multiple of,
    // the actual size of the pages backing the data.
    // If the system has no large pages and it is not possible to change that,
    // use 4096 for both page sizes, and for performance place both
    // the small and large binary data under /dev/shm (in-memory filesystem).
    Assembler(
        const string& smallDataFileNamePrefix,
        const string& largeDataFileNamePrefix,
        size_t smallDataPageSize,
        size_t largeDataPageSize
        );


    // Add reads from a fasta file.
    // The reads are added to those already previously present.
    void addReadsFromFasta(
        const string& fileName,
        size_t blockSize,
        size_t threadCountForReading,
        size_t threadCountForProcessing);

    // Access the reads and read names.
    void accessReadsReadOnly();
    void accessReadsReadWrite();
    void accessReadNamesReadOnly();
    void accessReadNamesReadWrite();

    // Create a histogram of read lengths.
    void histogramReadLength(const string& fileName);

    // Function to write one or all reads in Fasta format.
    void writeReads(const string& fileName);
    void writeRead(ReadId, const string& fileName);

    // Functions related to the k-mer table.
    void accessKmers();
    void writeKmers(const string& fileName) const;
    void randomlySelectKmers(
        size_t k,           // k-mer length.
        double probability, // The probability that a k-mer is selected as a marker.
        int seed            // For random number generator.
    );

    // Functions related to markers.
    // See the beginning of Marker.hpp for more information.
    void findMarkers(size_t threadCount);
    void accessMarkers();
    void writeMarkers(ReadId, const string& fileName);

private:

    // Data filled in by the constructor.
    string smallDataFileNamePrefix;
    string largeDataFileNamePrefix;
    size_t smallDataPageSize;
    size_t largeDataPageSize;

    // Functions to construct names for small and large binary objects.
    string smallDataName(const string& name) const
    {
        return smallDataFileNamePrefix + name;
    }
    string largeDataName(const string& name) const
    {
        return largeDataFileNamePrefix + name;
    }

    // Various pieces of assembler information stored in shared memory.
    // See class AssemblerInfo for more information.
    MemoryMapped::Object<AssemblerInfo> assemblerInfo;

    // The reads used for this assembly.
    // Indexed by ReadId.
    LongBaseSequences reads;
    ReadId readCount() const
    {
        return ReadId(reads.size());
    }

    // The names of the reads from the input fasta or fastq files.
    // Indexed by ReadId.
    // Note that we don't enforce uniqueness of read names.
    // We don't use read names to identify reads.
    // These names are only used as an aid in tracing each read
    // back to its origin.
    MemoryMapped::VectorOfVectors<char, uint64_t> readNames;

    // Function to write a read in Fasta format.
    void writeRead(ReadId, ostream&);



    // Table of all k-mers of length k.
    // Among all 4^k k-mers of length k, we choose a subset
    // that we call "markers".
    // The value of k used is stored in assemblerInfo.
    // The k-mer table is a vector of 4^k pairs,
    // indexed by k-mer id as computed using Kmer::id(k).
    // The markers are selected at the beginning of an assembly
    // and never changed, and selected in such a way that,
    // if (and only if) a k-mer is a marker, its reverse complement
    // is also a marker. That is, for all permitted values of i, 0 <= i < 4^k:
    // kmerTable[i].isMarker == kmerTable[kmerTable[i].reverseComplementKmerId].isMarker
    MemoryMapped::Vector<KmerInfo> kmerTable;


    // The markers on all reads. Indexed by ReadId.
    MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t> markers;
    void getMarkers(ReadId, vector<Marker>&) const;



};

#endif
