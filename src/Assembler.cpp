// Nanopore2.
#include "Assembler.hpp"
#include "MarkerFinder.hpp"
#include "orderPairs.hpp"
#include "OverlapFinder.hpp"
#include "ReadLoader.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;

// Boost libraries.
#include <boost/pending/disjoint_sets.hpp>

// Standard libraries.
#include "iterator.hpp"
#include <map>
#include "stdexcept.hpp"



Assembler::Assembler(
    const string& smallDataFileNamePrefix,
    const string& largeDataFileNamePrefix,
    size_t smallDataPageSize,
    size_t largeDataPageSize) :
    MultithreadedObject(*this),
    smallDataFileNamePrefix(smallDataFileNamePrefix),
    largeDataFileNamePrefix(largeDataFileNamePrefix),
    smallDataPageSize(smallDataPageSize),
    largeDataPageSize(largeDataPageSize)
{
    try {
        assemblerInfo.accessExistingReadWrite(smallDataName("Info"));
    } catch(runtime_error) {
        assemblerInfo.createNew(smallDataName("Info"));
        reads.createNew(largeDataName("Reads"), largeDataPageSize);
        readNames.createNew(largeDataName("ReadNames"), largeDataPageSize);
        reads.close();
        readNames.close();
    }

    // Either way, assemblerInfo is the only open object
    // when the constructor finishes.
}


void Assembler::accessReadsReadOnly()
{
    reads.accessExistingReadOnly(largeDataName("Reads"));
}
void Assembler::accessReadsReadWrite()
{
    reads.accessExistingReadWrite(largeDataName("Reads"));
}
void Assembler::accessReadNamesReadOnly()
{
    readNames.accessExistingReadOnly(largeDataName("ReadNames"));
}
void Assembler::accessReadNamesReadWrite()
{
    readNames.accessExistingReadWrite(largeDataName("ReadNames"));
}
void Assembler::checkReadsAreOpen() const
{
    if(!reads.isOpen()) {
        throw runtime_error("Reads are not accessible.");
    }
}
void Assembler::checkReadNamesAreOpen() const
{
    if(!readNames.isOpen()) {
        throw runtime_error("Read names are not accessible.");
    }
}
void Assembler::checkReadId(ReadId readId) const
{
    if(readId >= reads.size()) {
        throw runtime_error("Read id " + to_string(readId) +
            " is not valid. Must be between 0 and " + to_string(reads.size()) +
            " inclusive.");
    }
}



// Add reads from a fasta file.
// The reads are added to those already previously present.
void Assembler::addReadsFromFasta(
    const string& fileName,
    size_t blockSize,
    const size_t threadCountForReading,
    const size_t threadCountForProcessing)
{
    checkReadsAreOpen();
    checkReadNamesAreOpen();

    ReadLoader(
        fileName,
        blockSize,
        threadCountForReading,
        threadCountForProcessing,
        largeDataFileNamePrefix,
        largeDataPageSize,
        reads,
        readNames);

}


// Create a histogram of read lengths.
void Assembler::histogramReadLength(const string& fileName)
{
    checkReadsAreOpen();

    // Create the histogram.
    vector<size_t> histogram;
    for(ReadId readId=0; readId<readCount(); readId++) {
        const size_t length = reads[readId].baseCount;
        if(histogram.size() <= length) {
            histogram.resize(length+1, 0);
        }
        ++(histogram[length]);
    }

    // Write it out.
    ofstream csv(fileName);
    csv << "Length,Frequency,Bases,CumulativeFrequency,CumulativeBases\n";
    size_t cumulativeFrequency = 0;
    size_t cumulativeBaseCount = 0;
    for(size_t length=0; length<histogram.size(); length++) {
        const size_t frequency = histogram[length];
        if(frequency) {
            const  size_t baseCount = frequency * length;
            cumulativeFrequency += frequency;
            cumulativeBaseCount += baseCount;
            csv << length << "," << frequency << "," << baseCount << ",";
            csv<< cumulativeFrequency << "," << cumulativeBaseCount << "\n";
        }
    }

}



// Function to write one or all reads in Fasta format.
void Assembler::writeReads(const string& fileName)
{
    ofstream file(fileName);
    for(ReadId readId=0; readId<readCount(); readId++) {
        writeRead(readId, file);
    }

}



void Assembler::writeRead(ReadId readId, const string& fileName)
{
    ofstream file(fileName);
    writeRead(readId, file);
}


void Assembler::writeRead(ReadId readId, ostream& file)
{
    checkReadsAreOpen();
    checkReadNamesAreOpen();
    checkReadId(readId);

    const auto readSequence = reads[readId];
    const auto readName = readNames[readId];

    file << ">" << readId;
    file << " " << readSequence.baseCount << " ";
    copy(readName.begin(), readName.end(), ostream_iterator<char>(file));
    file << "\n";
    file << readSequence << "\n";

}



void Assembler::writeOrientedRead(ReadId readId, Strand strand, const string& fileName)
{
    writeOrientedRead(OrientedReadId(readId, strand), fileName);
}



void Assembler::writeOrientedRead(OrientedReadId orientedReadId, const string& fileName)
{
    ofstream file(fileName);
    writeOrientedRead(orientedReadId, file);
}



void Assembler::writeOrientedRead(OrientedReadId orientedReadId, ostream& file)
{
    checkReadsAreOpen();
    checkReadNamesAreOpen();

    const ReadId readId = orientedReadId.getReadId();
    checkReadId(readId);
    const Strand strand = orientedReadId.getStrand();
    const auto readSequence = reads[readId];
    const auto readName = readNames[readId];

    file << ">" << readId << "-" << strand;
    file << " " << readSequence.baseCount << " ";
    copy(readName.begin(), readName.end(), ostream_iterator<char>(file));
    file << "\n";
    readSequence.write(file, strand==1);
    file << "\n";

}





void Assembler::accessKmers()
{
    kmerTable.accessExistingReadOnly(smallDataName("Kmers"));
    if(kmerTable.size() != (1ULL<< (2*assemblerInfo->k))) {
        throw runtime_error("Size of k-mer vector is inconsistent with stored value of k.");
    }
}

void Assembler::checkKmersAreOpen()const
{
    if(!kmerTable.isOpen) {
        throw runtime_error("Kmers are not accessible.");
    }
}



// Randomly select the k-mers to be used as markers.
void Assembler::randomlySelectKmers(
    size_t k,           // k-mer length.
    double probability, // The probability that a k-mer is selected as a marker.
    int seed            // For random number generator.
)
{
    // Sanity check on the value of k, then store it.
    if(k > Kmer::capacity) {
        throw runtime_error("K-mer capacity exceeded.");
    }
    assemblerInfo->k = k;

    // Sanity check on the requested fraction.
    // It can be 1 at most. If it is 1, all k-mers
    // are guaranteed to be selected (because we use <=
    // comparison in the main loop.
    if(probability<0. || probability>1.) {
        throw runtime_error("Invalid k-mer probability " +
            to_string(probability) + " requested.");
    }



    // Compute the probability p with which we select
    // each k-mer and its reverse complement
    // in order to achieve the required k-mer fraction.
    // For k-mers that are not self-complementary,
    // the probability of not being selected
    // is (1-p)^2 (there are two chances,
    // with and without reverse complement).
    // So, probability = 1 - (1-p)^2, and therefore p=1-sqrt(1-probability).
    // For simplicity, we use the same p for k-mers that are
    // self-complementary. They are a small minority, and because
    //of this they are chose with lower probability.
    // Probably a good thing anyway.
    const double p = 1. - sqrt(1. - probability);
    if(probability == 1.) {
        CZI_ASSERT(p == 1.);
    }



    // Create the kmer table with the necessary size.
    kmerTable.createNew(smallDataName("Kmers"), smallDataPageSize);
    const size_t kmerCount = 1ULL << (2ULL*k);
    kmerTable.resize(kmerCount);

    // Store the reverse complement of each k-mer.
    for(uint64_t kmerId=0; kmerId<kmerCount; kmerId++) {
        const Kmer kmer(kmerId, k);
        const Kmer reverseComplementedKmer = kmer.reverseComplement(k);
        kmerTable[kmerId].reverseComplementedKmerId = KmerId(reverseComplementedKmer.id(k));
    }
    for(uint64_t kmerId=0; kmerId<kmerCount; kmerId++) {
        const uint64_t reverseComplementedKmerId = kmerTable[kmerId].reverseComplementedKmerId;
        CZI_ASSERT(kmerTable[reverseComplementedKmerId].reverseComplementedKmerId == kmerId);
    }

    // Prepare to generate uniformly distributed numbers between 0 and 1.
    std::mt19937 randomSource(seed);
    std::uniform_real_distribution<> uniformDistribution;

    // Pick each k-mer and its reverse complement with probability p.
    // Use <= comparison, so if probability=1, p=1, all k-mers are kept.
    for(uint64_t kmerId=0; kmerId<kmerCount; kmerId++) {
        const double x = uniformDistribution(randomSource);
        if(x <= p) {
            kmerTable[kmerId].isMarker = true;
            const uint64_t reverseComplementedKmerId = kmerTable[kmerId].reverseComplementedKmerId;
            kmerTable[reverseComplementedKmerId].isMarker = true;
        }
    }
    size_t usedKmerCount = 0;
    for(uint64_t kmerId=0; kmerId<kmerCount; kmerId++) {
        if(kmerTable[kmerId].isMarker) {
            ++usedKmerCount;
        }
    }
    cout << "Selected " << usedKmerCount << " " << k << "-mers out of ";
    cout << kmerCount << " total." << endl;
    cout << "Requested probability: " << probability << "." << endl;
    cout << "Actual fraction: ";
    cout << double(usedKmerCount)/double(kmerCount) << "." << endl;

    if(probability == 1.) {
        CZI_ASSERT(usedKmerCount == kmerCount);
    }

}



void Assembler::writeKmers(const string& fileName) const
{
    checkKmersAreOpen();

    // Get the k-mer length.
    const size_t k = assemblerInfo->k;
    const size_t kmerCount = 1ULL << (2ULL*k);
    CZI_ASSERT(kmerTable.size() == kmerCount);

    // Open the output file and write the header line.
    ofstream file(fileName);
    file << "KmerId,Kmer,IsMarker,ReverseComplementedKmerId,ReverseComplementedKmer\n";

    // Write a line for each k-mer.
    for(uint64_t kmerId=0; kmerId<kmerCount; kmerId++) {
        file << kmerId << ",";
        file << Kmer(kmerId, k) << ",";
        file << int(kmerTable[kmerId].isMarker) << ",";
        file << kmerTable[kmerId].reverseComplementedKmerId << ",";
        file << Kmer(kmerTable[kmerId].reverseComplementedKmerId, k) << "\n";
    }
}


void Assembler::findMarkers(size_t threadCount)
{
    checkReadsAreOpen();
    checkKmersAreOpen();

    markers.createNew(largeDataName("Markers"), largeDataPageSize);
    MarkerFinder markerFinder(
        assemblerInfo->k,
        kmerTable,
        reads,
        markers,
        threadCount);

}



void Assembler::accessMarkers()
{
    markers.accessExistingReadOnly(largeDataName("Markers"));
}

void Assembler::checkMarkersAreOpen() const
{
    if(!markers.isOpen()) {
        throw runtime_error("Markers are not accessible.");
    }
}


void Assembler::writeMarkers(ReadId readId, Strand strand, const string& fileName)
{
    // Check that we have what we need.
    checkKmersAreOpen();
    checkReadsAreOpen();
    checkMarkersAreOpen();
    checkReadId(readId);

    // Get the markers.
    const OrientedReadId orientedReadId(readId, strand);
    vector<Marker> markers;
    getMarkers(orientedReadId, markers);

    // Write them out.
    ofstream csv(fileName);
    csv << "Ordinal,KmerId,Kmer,Position\n";
    for(const Marker& marker: markers) {
        csv << marker.ordinal << ",";
        csv << marker.kmerId << ",";
        csv << Kmer(marker.kmerId, assemblerInfo->k) << ",";
        csv << marker.position << "\n";
    }
}



void Assembler::getMarkers(ReadId readId, vector<Marker>& readMarkers) const
{
    checkMarkersAreOpen();
    const auto readCompressedMarkers = markers[readId];
    const uint32_t n = uint32_t(readCompressedMarkers.size());

    readMarkers.clear();
    readMarkers.reserve(n);

    Marker marker;
    marker.position = 0;
    for(marker.ordinal=0; marker.ordinal<n; marker.ordinal++) {
        const CompressedMarker& compressedMarker = readCompressedMarkers[marker.ordinal];
        marker.position += compressedMarker.shift;
        marker.kmerId = compressedMarker.kmerId;
        readMarkers.push_back(marker);
    }
}



void Assembler::getMarkers(OrientedReadId orientedReadId, vector<Marker>& markers)
{
    checkReadsAreOpen();
    checkKmersAreOpen();

    // Get uncompressed markers for this read.
    const ReadId readId = orientedReadId.getReadId();
    const Strand strand = orientedReadId.getStrand();
    getMarkers(readId, markers);

    // For strand 0, we are done.
    if(strand == 0) {
        return;
    }

    // For strand 1, we have to reverse complement the markers
    // and their ordinals and positions.
    const uint32_t k = uint32_t(assemblerInfo->k);
    const uint32_t readLength = uint32_t(reads[readId].baseCount);
    reverse(markers.begin(), markers.end());
    for(uint32_t ordinal=0; ordinal<markers.size(); ordinal++) {
     Marker& marker = markers[ordinal];
        marker.ordinal = ordinal;
        marker.position = readLength - k - marker.position;
        marker.kmerId = kmerTable[marker.kmerId].reverseComplementedKmerId;
    }

}



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
    const ReadId readCount = ReadId(markers.size());

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
        markers,
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

    // Loop over all overlaps involving this oriented read.
    for(const uint64_t i: overlapTable[orientedReadId0.getValue()]) {
        const Overlap& overlap = overlaps[i];

        // Get the other oriented read involved in this overlap.
        OrientedReadId orientedReadId1;
        if(overlap.orientedReadIds[0] == orientedReadId0) {
            orientedReadId1 = overlap.orientedReadIds[1];
        } else if(overlap.orientedReadIds[1] == orientedReadId0) {
            orientedReadId1 = overlap.orientedReadIds[0];
        } else {
            CZI_ASSERT(0);
        }

        // Write it out.
        writeOrientedRead(orientedReadId1, file);
    }

}



// Compute connected components of the global overlap graph.
// We only keep components that are large enough.
// For each pair of complementary connected component
// we only keep the one in which
// the lowest numbered read in on the positive strand.
// Self-complementary connected components are kept.
void Assembler::computeOverlapGraphComponents(
    size_t minFrequency,            // Minimum number of minHash hits for an overlap to be used.
    size_t minComponentSize         // Minimum size for a connected component to be kept.
    )
{
    checkReadsAreOpen();
    checkOverlapsAreOpen();
    const OrientedReadId::Int orientedReadCount = OrientedReadId::Int(2*reads.size());

    // Initialize the disjoint set data structures.
    vector<ReadId> rank(orientedReadCount);
    vector<ReadId> parent(orientedReadCount);
    boost::disjoint_sets<ReadId*, ReadId*> disjointSets(&rank[0], &parent[0]);
    for(OrientedReadId::Int orientedReadId=0; orientedReadId<orientedReadCount; orientedReadId++) {
        disjointSets.make_set(orientedReadId);
    }

    // Loop over overlaps with frequency at least minFrequency.
    for(const Overlap& overlap: overlaps) {
        if(overlap.minHashFrequency >= minFrequency) {
            disjointSets.union_set(
                overlap.orientedReadIds[0].getValue(),
                overlap.orientedReadIds[1].getValue());
        }
    }

    // Store the component that each oriented read belongs to.
    overlapGraphComponent.createNew(largeDataName("OverlapGraphComponent"), largeDataPageSize);
    overlapGraphComponent.resize(orientedReadCount);
    for(OrientedReadId::Int orientedReadId=0; orientedReadId<orientedReadCount; orientedReadId++) {
        overlapGraphComponent[orientedReadId] = disjointSets.find_set(orientedReadId);
    }

    // Gather the vertices (oriented read ids) of each component.
    // The vertices of each component are sorted by construction.
    std::map<OrientedReadId::Int, vector<OrientedReadId::Int> > componentMap;
    for(OrientedReadId::Int orientedReadId=0; orientedReadId<orientedReadCount; orientedReadId++) {
        componentMap[overlapGraphComponent[orientedReadId]].push_back(orientedReadId);
    }



    // Sort the components by decreasing size.
    // We only keep components that are large enough.
    // For each pair of complementary connected component
    // we only keep the one in which
    // the lowest numbered read in on the positive strand.
    // Self-complementary connected components are kept.
    vector< pair<OrientedReadId::Int, OrientedReadId::Int> > componentTable;
    for(const auto& p: componentMap) {
        const auto componentId = p.first;
        const auto& component = p.second;
        const auto componentSize = component.size();
        CZI_ASSERT(componentSize > 0);
        if(componentSize >= minComponentSize) {
            const OrientedReadId firstOrientedReadId = OrientedReadId(component.front());
            if(firstOrientedReadId.getStrand() == 1) {
                continue;   // Keep the other component in this self-complementary pair.
            }
            // Keep this component.
            componentTable.push_back(make_pair(componentId, componentSize));

        }
    }
    sort(componentTable.begin(), componentTable.end(),
        OrderPairsBySecondOnlyGreater<ReadId, ReadId>());



    // Renumber the components.
    std::map<ReadId, ReadId> componentNumberMap;
    for(ReadId newComponentId=0; newComponentId<componentTable.size(); newComponentId++) {
        const auto& p = componentTable[newComponentId];
        const auto oldComponentId = p.first;
        componentNumberMap.insert(make_pair(oldComponentId, newComponentId));
    }
    for(ReadId i=0; i<orientedReadCount; i++) {
        ReadId& readComponentId = overlapGraphComponent[i];
        const auto it = componentNumberMap.find(readComponentId);
        if(it == componentNumberMap.end()) {
            readComponentId = std::numeric_limits<ReadId>::max();
        } else {
            readComponentId = it->second;
        }
    }

    // Permanently store the renumbered components.
    overlapGraphComponents.createNew(largeDataName("OverlapGraphComponents"), largeDataPageSize);
    for(ReadId newComponentId=0; newComponentId<componentTable.size(); newComponentId++) {
        const auto& p = componentTable[newComponentId];
        const auto oldComponentId = p.first;
        const auto& component = componentMap[oldComponentId];
        CZI_ASSERT(component.size() == p.second);
        overlapGraphComponents.appendVector();
        for(const auto& orientedRead: component) {
            overlapGraphComponents.append(OrientedReadId(orientedRead));
        }
    }



    // Write out the size of each component we kept,
    // and whether it is self-complementary.
    size_t selfComplementaryComponentCount = 0;
    for(ReadId newComponentId=0; newComponentId<overlapGraphComponents.size(); newComponentId++) {
        const auto component = overlapGraphComponents[newComponentId];

        //See if it is self-complementary.
        bool isSelfComplementary = false;
        const auto componentSize = component.size();
        if(componentSize > 1) {
            const OrientedReadId firstOrientedReadId = OrientedReadId(component[0]);
            const OrientedReadId secondOrientedReadId = OrientedReadId(component[1]);
            if(secondOrientedReadId.getReadId() == firstOrientedReadId.getReadId()) {
                CZI_ASSERT((componentSize%2) == 0);
                for(size_t i=0; i<componentSize; i+=2) {
                    const OrientedReadId orientedReadId0  = OrientedReadId(component[i]);
                    const OrientedReadId orientedReadId1  = OrientedReadId(component[i+1]);
                    CZI_ASSERT(orientedReadId0.getReadId() == orientedReadId1.getReadId());
                    CZI_ASSERT(orientedReadId0.getStrand() == 0);
                    CZI_ASSERT(orientedReadId1.getStrand() == 1);
                }
                ++selfComplementaryComponentCount;
                isSelfComplementary = true;
            }

        }
        cout << "Component " << newComponentId << " has size ";
        cout << componentSize;
        if(isSelfComplementary) {
            cout << " and is self-complementary";
        }
        cout << "." << endl;

    }
    cout << "Found " << selfComplementaryComponentCount << " self-complementary components." << endl;


}



// Compute a marker alignment of two oriented reads.
void Assembler::alignOrientedReads(
    ReadId readId0, Strand strand0,
    ReadId readId1, Strand strand1
)
{
    alignOrientedReads(
        OrientedReadId(readId0, strand0),
        OrientedReadId(readId1, strand1)
        );
}
void Assembler::alignOrientedReads(
    OrientedReadId orientedReadId0,
    OrientedReadId orientedReadId1
)
{
    // Get the markers for the two oriented reads and
    // sort them by kmer id.
    checkMarkersAreOpen();
    vector<Marker> markers0;
    vector<Marker> markers1;
    getMarkers(orientedReadId0, markers0);
    getMarkers(orientedReadId1, markers1);
    sort(markers0.begin(), markers0.end(), OrderMarkersByKmerId());
    sort(markers1.begin(), markers1.end(), OrderMarkersByKmerId());

    // Call the lower level function.
    alignOrientedReads(markers0, markers1);
}



// This lower level version takes as input vectors of
// markers already sorted by kmerId.
void Assembler::alignOrientedReads(
    const vector<Marker>& markers0,
    const vector<Marker>& markers1
)
{

}
