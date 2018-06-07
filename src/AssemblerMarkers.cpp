// Nanopore2.
#include "Assembler.hpp"
#include "MarkerFinder.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;



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
    csv << "GlobalMarkerId,GlobalOrientedMarkerId,Ordinal,KmerId,Kmer,Position\n";
    for(const Marker& marker: markers) {
        const OrientedMarkerId orientedMarkerId =
            getGlobalOrientedMarkerId(orientedReadId, marker.ordinal);
        CZI_ASSERT(orientedMarkerId.getStrand() == strand);
        csv << orientedMarkerId.getMarkerId() << ",";
        csv << orientedMarkerId.getValue() << ",";
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



// Given a marker by its OrientedReadId and ordinal,
// return the corresponding global marker id.
OrientedMarkerId Assembler::getGlobalOrientedMarkerId(
    OrientedReadId orientedReadId, uint32_t ordinal) const
{

    if(orientedReadId.getStrand() == 0) {
        // Count forward from the first marker of the read.
        return OrientedMarkerId(
            (markers.begin(orientedReadId.getReadId()) - markers.begin()) + ordinal,
            0);

    } else {
        // Count backwards from the last marker of the read
        // (which is the marker preceding the first marker of the next read).
        return OrientedMarkerId(
            (markers.begin(orientedReadId.getReadId()+1ULL) - markers.begin()) - 1ULL - ordinal,
            1);
    }

}


// Inverse of the above: given a global marker id,
// return its OrientedReadId and ordinal.
// This requires a binary search in the markers toc.
// This could be avoided, at the cost of storing
// an additional 4 bytes per marker.
pair<OrientedReadId, uint32_t>
    Assembler::findGlobalOrientedMarkerId(OrientedMarkerId orientedMarkerId) const
{
    const MarkerId markerId = orientedMarkerId.getMarkerId();
    const Strand strand = Strand(orientedMarkerId.getStrand());
    ReadId readId;
    uint32_t ordinal;
    tie(readId, ordinal) = markers.find(markerId);

    if(strand == 1) {
        ordinal = uint32_t(markers.size(readId)) - 1 - ordinal;
    }

    return make_pair(OrientedReadId(readId, strand), ordinal);
}

