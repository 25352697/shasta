// Nanopore2
#include "LocalMarkerGraph2.hpp"
#include "CZI_ASSERT.hpp"
#include "findMarkerId.hpp"
#include "Marker.hpp"
#include "MemoryMappedVectorOfVectors.hpp"
#include "ReadId.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;

// Boost libraries.
#include <boost/graph/graphviz.hpp>

// Standard libraries.
#include "fstream.hpp"
#include "stdexcept.hpp"
#include "tuple.hpp"
#include "utility.hpp"


LocalMarkerGraph2::LocalMarkerGraph2(
    size_t k,
    const LongBaseSequences& reads,
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers
    ) :
    k(k),
    reads(reads),
    markers(markers)
{

}


// Find out if a vertex with the given GlobalMarkerGraphVertexId exists.
// If it exists, return make_pair(true, v).
// Otherwise, return make_pair(false, null_vertex());
std::pair<bool, LocalMarkerGraph2::vertex_descriptor>
    LocalMarkerGraph2::findVertex(GlobalMarkerGraphVertexId vertexId) const
{
    const auto it = vertexMap.find(vertexId);
    if(it == vertexMap.end()) {
        return make_pair(false, null_vertex());
    } else {
        const vertex_descriptor v = it->second;
        return make_pair(true, v);
    }
}


// Add a vertex with the given GlobalMarkerGraphVertexId
// and return its vertex descriptor.
// A vertex with this GlobalMarkerGraphVertexId must not exist.
LocalMarkerGraph2::vertex_descriptor
    LocalMarkerGraph2::addVertex(
    GlobalMarkerGraphVertexId vertexId,
    int distance,
    MemoryAsContainer<MarkerId> vertexMarkers)
{
    // Check that the vertex does not already exist.
    CZI_ASSERT(vertexMap.find(vertexId) == vertexMap.end());

    // Add the vertex and store it in the vertex map.
    const vertex_descriptor v = add_vertex(LocalMarkerGraph2Vertex(vertexId, distance), *this);
    vertexMap.insert(make_pair(vertexId, v));

    // Fill in the marker information for this vertex.
    LocalMarkerGraph2Vertex& vertex = (*this)[v];
    vertex.markerInfos.reserve(vertexMarkers.size());
    for(const MarkerId markerId: vertexMarkers) {
        LocalMarkerGraph2Vertex::MarkerInfo markerInfo;
        markerInfo.markerId = markerId;
        tie(markerInfo.orientedReadId, markerInfo.ordinal) =
            findMarkerId(markerId, markers);
        vertex.markerInfos.push_back(markerInfo);
    }

    return v;
}



// Get the KmerId for a vertex.
KmerId LocalMarkerGraph2::getKmerId(vertex_descriptor v) const
{
    const LocalMarkerGraph2Vertex& vertex = (*this)[v];
    CZI_ASSERT(!vertex.markerInfos.empty());
    const MarkerId firstMarkerId = vertex.markerInfos.front().markerId;
    const CompressedMarker& firstMarker = markers.begin()[firstMarkerId];
    const KmerId kmerId = firstMarker.kmerId;

    // Sanity check that all markers have the same kmerId.
    // At some point this can be removed.
    for(const auto& markerInfo: vertex.markerInfos){
        const CompressedMarker& marker = markers.begin()[markerInfo.markerId];
        CZI_ASSERT(marker.kmerId == kmerId);
    }

    return kmerId;
}



// Write the graph in Graphviz format.
void LocalMarkerGraph2::write(
    const string& fileName,
    size_t minCoverage,
    size_t minConsensus,
    int maxDistance,
    bool detailed) const
{
    ofstream outputFileStream(fileName);
    if(!outputFileStream) {
        throw runtime_error("Error opening " + fileName);
    }
    write(outputFileStream, minCoverage, minConsensus, maxDistance, detailed);
}
void LocalMarkerGraph2::write(
    ostream& s,
    size_t minCoverage,
    size_t minConsensus,
    int maxDistance,
    bool detailed) const
{
    Writer writer(*this, minCoverage, minConsensus, maxDistance, detailed);
    boost::write_graphviz(s, *this, writer, writer, writer,
        boost::get(&LocalMarkerGraph2Vertex::vertexId, *this));
}

LocalMarkerGraph2::Writer::Writer(
    const LocalMarkerGraph2& graph,
    size_t minCoverage,
    size_t minConsensus,
    int maxDistance,
    bool detailed) :
    graph(graph),
    minCoverage(minCoverage),
    minConsensus(minConsensus),
    maxDistance(maxDistance),
    detailed(detailed)
{
}



void LocalMarkerGraph2::Writer::operator()(std::ostream& s) const
{
    if(detailed) {
        s << "layout=dot;\n";
        s << "ratio=expand;\n";
        s << "node [fontname = \"Courier New\" shape=rectangle];\n";
        s << "edge [fontname = \"Courier New\"];\n";
    } else {
        s << "layout=sfdp;\n";
        s << "ratio=expand;\n";
        s << "node [shape=point];\n";
    }
}



void LocalMarkerGraph2::Writer::operator()(std::ostream& s, vertex_descriptor v) const
{
    const LocalMarkerGraph2Vertex& vertex = graph[v];
    const auto coverage = vertex.markerInfos.size();
    CZI_ASSERT(coverage > 0);


    // For compact output, the node shape is already defaulted to point,
    // and we don't write a label. The tooltip contains the vertex id,
    // which can be used to create a local subgraph to be looked at
    // in detailed format (use scripts/CreateLocalSubgraph.py).
    if(!detailed) {

        // Compact output.

        // Begin vertex attributes.
        s << "[";

        // Tooltip.
        s << "tooltip=\"Marker " << vertex.vertexId;
        s << ", coverage " << coverage << ", distance " << vertex.distance << "\"";

        // Vertex size.
        s << " width=\"";
        const auto oldPrecision = s.precision(4);
        s << 0.05 * sqrt(double(coverage));
        s.precision(oldPrecision);
        s << "\"";

        // Color.
        string color;
        if(vertex.distance == maxDistance) {
            color = "cyan";
        } else if(vertex.distance == 0) {
            color = "lightGreen";
        } else  if(coverage >= minCoverage) {
            color = "black";
        } else if(coverage == 1) {
            color = "#ff000080";  // Red, half way transparent
        } else if(coverage == 2) {
            color = "#ff800080";  // Orange, half way transparent
        } else {
            color = "#ff80ff80";  // Purple, half way transparent
        }
        s << " fillcolor=\"" << color << "\" color=\"" << color << "\"";

        // End vertex attributes.
        s << "]";

    } else {

        // Detailed output.
        const size_t k = graph.k;
        const KmerId kmerId = graph.getKmerId(v);
        const Kmer kmer(kmerId, k);

        // Begin vertex attributes.
        s << "[";

        // Color.
        string color;
        if(vertex.distance == maxDistance) {
            color = "cyan";
        } else if(vertex.distance == 0) {
            color = "lightGreen";
        } else if(coverage >= minCoverage) {
            color = "green";
        } else if(coverage == 1) {
            color = "#ff0000";  // Red
        } else if(coverage == 2) {
            color = "#ff8000";  // Orange
        } else {
            color = "#ff80ff";  // Purple
        }
        s << " style=filled";
        s << " fillcolor=\"" << color << "\"";

        // Label.
        s << "label=\"Marker " << vertex.vertexId;
        s << "\\nCoverage " << coverage;
        s << "\\nDistance " << vertex.distance << "\"";

        // Write the label using Graphviz html-like functionality.
        s << " label=<<font><table border=\"0\">";

        /*
        // Vertex id.
        s << "<tr><td colspan=\"3\"><b>";
        s << "Vertex " << vertex.vertexId;
        s << "</b></td></tr>";
        */

        // Kmer.
        s << "<tr><td colspan=\"3\"><b>";
        kmer.write(s, k);
        s << "</b></td></tr>";

        // Coverage.
        s << "<tr><td colspan=\"3\"><b>";
        s << "Coverage " << coverage;
        s << "</b></td></tr>";

        // Distance.
        s << "<tr><td colspan=\"3\"><b>";
        s << "Distance " << vertex.distance;
        s << "</b></td></tr>";

        // Column headers.
        s << "<tr><td><b>Read</b></td><td><b>Ord</b></td><td><b>Pos</b></td></tr>";

        // A row for each marker of this vertex.
        for(const auto& markerInfo: vertex.markerInfos) {
            const CompressedMarker& marker = graph.markers.begin()[markerInfo.markerId];

            // OrientedReadId
            s << "<tr><td align=\"right\"><b>" << markerInfo.orientedReadId << "</b></td>";

            // Ordinal.
            s << "<td align=\"right\"><b>" << markerInfo.ordinal << "</b></td>";

            // Position.
            s << "<td align=\"right\"><b>" << marker.position << "</b></td></tr>";
        }


        // End the table.
        s << "</table></font>>";

        // End vertex attributes.
        s << "]";
    }
}



void LocalMarkerGraph2::Writer::operator()(std::ostream& s, edge_descriptor e) const
{

#if 0
    const LocalMarkerGraph2Edge& edge = graph[e];

    if(!detailed) {

        // Compact output.

        // Begin edge attributes.
        s << "[";

        // End edge attributes.
        s << "]";
    } else {

        // Detailed output.

        // If getting here, we are doing detailed output.

        // Begin edge attributes.
        s << "[";

        // End edge attributes.
        s << "]";
    }
#endif

}

