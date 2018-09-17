// shasta
#include "LocalMarkerGraph2.hpp"
#include "approximateTopologicalSort.hpp"
#include "CZI_ASSERT.hpp"
#include "findMarkerId.hpp"
#include "LongBaseSequence.hpp"
#include "Histogram.hpp"
#include "Marker.hpp"
#include "MemoryMappedVectorOfVectors.hpp"
#include "orderPairs.hpp"
#include "ReadId.hpp"
using namespace ChanZuckerberg;
using namespace shasta;

// SeqAn.
#include <seqan/graph_msa.h>
#include <seqan/version.h>

// Boost libraries.
// Due to a bug in boost graph include files, disjoint_sets.hpp
// must be included before graphviz.hpp
#include <boost/pending/disjoint_sets.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/topological_sort.hpp>

// Standard libraries.
#include "fstream.hpp"
#include "iterator.hpp"
#include "stdexcept.hpp"
#include "tuple.hpp"
#include "utility.hpp"


LocalMarkerGraph2::LocalMarkerGraph2(
    uint32_t k,
    LongBaseSequences& reads,
    bool useRunLengthReads,
    const MemoryMapped::VectorOfVectors<uint8_t, uint64_t>& readRepeatCounts,
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
    const MemoryMapped::Vector<CompressedGlobalMarkerGraphVertexId>& globalMarkerGraphVertex
    ) :
    k(k),
    reads(reads),
    useRunLengthReads(useRunLengthReads),
    readRepeatCounts(readRepeatCounts),
    markers(markers),
    globalMarkerGraphVertex(globalMarkerGraphVertex)
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



// Get the repeat counts for a MarkerInfo of a vertex.
vector<uint8_t> LocalMarkerGraph2::getRepeatCounts(
    const LocalMarkerGraph2Vertex::MarkerInfo& markerInfo) const
{
    CZI_ASSERT(useRunLengthReads);
    const OrientedReadId orientedReadId = markerInfo.orientedReadId;
    const ReadId readId = orientedReadId.getReadId();
    const Strand strand = orientedReadId.getStrand();
    const CompressedMarker& marker = markers.begin()[markerInfo.markerId];

    const auto& counts = readRepeatCounts[readId];

    vector<uint8_t> v(k);
    for(size_t i=0; i<k; i++) {
        if(strand == 0) {
            v[i] = counts[marker.position + i];
        } else {
            v[i] = counts[counts.size() - 1 - marker.position - i];
        }
    }

    return v;
}



// Store sequence information in the edge.
// This version takes as input a vector of the
// LocalMarkerGraph2Edge::Info that caused the edge to be created.
void LocalMarkerGraph2::storeEdgeInfo(
    edge_descriptor e,
    const vector<LocalMarkerGraph2Edge::Info>& infoVector)
{
    LocalMarkerGraph2& graph = *this;
    LocalMarkerGraph2Edge& edge = graph[e];

    // Map to store the oriented read ids and ordinals, grouped by sequence.
    std::map<LocalMarkerGraph2Edge::Sequence, vector<LocalMarkerGraph2Edge::InfoWithRepeatCounts> > sequenceTable;
    for(const LocalMarkerGraph2Edge::Info& info: infoVector) {
        const CompressedMarker& marker0 = markers.begin(info.orientedReadId.getValue())[info.ordinals[0]];
        const CompressedMarker& marker1 = markers.begin(info.orientedReadId.getValue())[info.ordinals[1]];

        // Fill in the sequence information and, if necessary, the base repeat counts.
        LocalMarkerGraph2Edge::Sequence sequence;
        LocalMarkerGraph2Edge::InfoWithRepeatCounts infoWithRepeatCounts(info);
        if(marker1.position <= marker0.position + k) {
            sequence.overlappingBaseCount = uint8_t(marker0.position + k - marker1.position);
            if(useRunLengthReads) {
                const auto repeatCounts = readRepeatCounts[info.orientedReadId.getReadId()];
                for(uint32_t i=0; i<sequence.overlappingBaseCount; i++) {
                    uint32_t position = marker1.position + i;
                    uint8_t repeatCount = 0;
                    if(info.orientedReadId.getStrand() == 0) {
                        repeatCount = repeatCounts[position];
                    } else {
                        repeatCount = repeatCounts[repeatCounts.size() - 1 - position];
                    }
                    infoWithRepeatCounts.repeatCounts.push_back(repeatCount);
                }
            }
        } else {
            sequence.overlappingBaseCount = 0;
            const auto read = reads[info.orientedReadId.getReadId()];
            const uint32_t readLength = uint32_t(read.baseCount);
            for(uint32_t position=marker0.position+k;  position!=marker1.position; position++) {
                shasta::Base base;
                if(info.orientedReadId.getStrand() == 0) {
                    base = read.get(position);
                } else {
                    base = read.get(readLength - 1 - position);
                    base.complementInPlace();
                }
                sequence.sequence.push_back(base);
            }
            if(useRunLengthReads) {
                const auto repeatCounts = readRepeatCounts[info.orientedReadId.getReadId()];
                for(uint32_t position=marker0.position+k;  position!=marker1.position; position++) {
                    uint8_t repeatCount;
                    if(info.orientedReadId.getStrand() == 0) {
                        repeatCount = repeatCounts[position];
                    } else {
                        repeatCount = repeatCounts[readLength - 1 - position];
                    }
                    infoWithRepeatCounts.repeatCounts.push_back(repeatCount);
                }
            }

        }

        // Store it.
        sequenceTable[sequence].push_back(infoWithRepeatCounts);

    }

    // Copy to the edge infos data structure.
    edge.infos.clear();
    copy(sequenceTable.begin(), sequenceTable.end(), back_inserter(edge.infos));

    // Sort by decreasing size of the infos vector.
    sort(edge.infos.begin(), edge.infos.end(),
        OrderPairsBySizeOfSecondGreater<
        LocalMarkerGraph2Edge::Sequence,
        vector<LocalMarkerGraph2Edge::InfoWithRepeatCounts> >());

}



// If using the run-length representation of reads,
// compute SeqAn alignments for all edges on the local assembly path.
void LocalMarkerGraph2::computeSeqanAlignments()
{
    CZI_ASSERT(useRunLengthReads);
    const bool debug = false;

    LocalMarkerGraph2& graph = *this;
    for(const edge_descriptor e: localAssemblyPath) {
        LocalMarkerGraph2Edge& edge = graph[e];
        CZI_ASSERT(!edge.infos.empty());

        // If the edge is not ambiguous, there is no need
        // to compute the alignment.
        if(edge.infos.size() == 1) {
            continue;
        }

        // SeqAn multiple sequence alignment does not support empty sequences.
        // Skip it if we have any empty sequences.
        bool emptySequencesFound = false;
        for(const auto& p: edge.infos) {
            if(p.first.sequence.empty()) {
                emptySequencesFound = true;
                break;
            }
        }
        if(emptySequencesFound) {
            continue;
        }

        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        if(debug) {
            cout << "Computing SeqAn alignment for edge ";
            cout << graph[v0].vertexId << "->" << graph[v1].vertexId << endl;
        }

        /*
        // Extract the marker sequences.
        const Kmer kmer0(getKmerId(v0), k);
        const Kmer kmer1(getKmerId(v1), k);
        if(debug) {
            cout << "Marker sequences ";
            kmer0.write(cout, k);
            cout << "->";
            kmer1.write(cout, k);
            cout << endl;
        }
        */


        // Write the sequences for this edge.
        if(false) {
            cout << "Sequences:" << endl;
            for(const auto& p: edge.infos) {
                const auto& sequence = p.first;
                const auto& infos = p.second;
                string sequenceString;
                if(sequence.sequence.empty()) {
                    sequenceString = to_string(sequence.overlappingBaseCount);
                } else {
                    for(const shasta::Base base: sequence.sequence) {
                        sequenceString.push_back(base.character());
                    }
                }
                for(auto it=infos.begin(); it!=infos.end(); ++it) {
                    const auto& info = *it;
                    cout << info.orientedReadId << " ";
                    cout << info.ordinals[0] << " " << info.ordinals[1] << " ";
                    cout << sequenceString << endl;
                }
            }
        }



        // Compute the alignmentInfos.
        edge.alignmentInfos.clear();
        for(const auto& p: edge.infos) {
            const auto& sequence = p.first;
            const auto& infos = p.second;

            // Loop over all infos for this sequence.
            for(auto it=infos.begin(); it!=infos.end(); ++it) {
                const auto& info = *it;
                LocalMarkerGraph2Edge::AlignmentInfo alignmentInfo;
                alignmentInfo.orientedReadId = info.orientedReadId;
                alignmentInfo.ordinals = info.ordinals;
                alignmentInfo.sequence = sequence.sequence;
                alignmentInfo.repeatCounts = info.repeatCounts;
                edge.alignmentInfos.push_back(alignmentInfo);
            }
        }


        // Write the alignmentInfos.
        if(debug) {
            cout << "Alignment infos:" << endl;
            for(const auto& info: edge.alignmentInfos) {
                cout << info.orientedReadId << " ";
                cout << info.ordinals[0] << " " << info.ordinals[1] << " ";
                copy(info.sequence.begin(), info.sequence.end(),
                    ostream_iterator<shasta::Base>(cout));
                cout << " ";
                for(const int count: info.repeatCounts) {
                    cout << " " << count;
                }
                cout << endl;
            }
        }



        // Now we can compute the multiple sequence alignment using SeqAn.
        static_assert(
            SEQAN_VERSION_MAJOR==2 &&
            SEQAN_VERSION_MINOR==4 &&
            SEQAN_VERSION_PATCH==0,
            "SeqAn version 2.4.0 is required.");
        resize(rows(edge.seqanAlignment), edge.alignmentInfos.size());
        for (size_t i = 0; i <edge.alignmentInfos.size(); i++) {
            const auto& info = edge.alignmentInfos[i];
            string sequenceString;
            for(const shasta::Base base: info.sequence) {
                sequenceString.push_back(base.character());
            }
            seqan::assignSource(seqan::row(edge.seqanAlignment, i), sequenceString);
        }
        seqan::globalMsaAlignment(edge.seqanAlignment, seqan::Score<int, seqan::Simple>(0, -1, -1));
        edge.seqanAlignmentWasComputed = true;

        if(debug) {
            cout << "SeqAn alignment:" << endl;
            for(size_t i=0; i<edge.alignmentInfos.size(); i++) {
                cout << seqan::row(edge.seqanAlignment, i) << " " << i << endl;
            }
            cout << "Repeat counts on SeqAn alignment:" << endl;
            for(size_t i=0; i<edge.alignmentInfos.size(); i++) {
                const seqan::Gaps< seqan::String<seqan::Dna> >& alignmentRow =
                    seqan::row(edge.seqanAlignment, i);
                const auto n = seqan::length(alignmentRow);
                size_t position = 0;
                for(size_t j=0; j<n; j++) {
                    if(seqan::isGap(alignmentRow, j)) {
                        cout << "-";
                    } else {
                        const int repeatCount = edge.alignmentInfos[i].repeatCounts[position++];
                        if(repeatCount < 10) {
                            cout << repeatCount;
                        } else {
                            cout << "*";
                        }
                    }
                }
                cout << " " << i << endl;
            }

            /*
            // See what we have at each position of the alignment.
            const size_t n = seqan::length(seqan::row(edge.seqanAlignment, 0));
            vector<size_t> positions(edge.alignmentInfos.size(), 0);
            for(size_t i=0; i<n; i++) {
                cout << "Alignment position " << i << endl;
                for(size_t j=0; j<edge.alignmentInfos.size(); j++) {
                    char baseCharacter = '-';
                    int repeatCount = 1;
                    if(!seqan::isGap(seqan::row(edge.seqanAlignment, j), i)) {
                        baseCharacter = edge.alignmentInfos[j].sequence[positions[j]].character();
                        repeatCount = edge.alignmentInfos[j].repeatCounts[positions[j]];
                        ++positions[j];
                    }
                    cout << baseCharacter << repeatCount << " ";
                }
                cout << endl;
            }
            */
        }



        // Loop over alignment positions.
        vector<shasta::Base> consensusSequence;
        const size_t n = seqan::length(seqan::row(edge.seqanAlignment, 0));
        vector<size_t> positions(edge.alignmentInfos.size(), 0);
        vector<size_t> baseInteger(edge.alignmentInfos.size()); // 0=C 1=C 2=G 3=T 4=-
        vector<size_t> repeatCount(edge.alignmentInfos.size());
        for(size_t i=0; i<n; i++) {

            // Fill in the base and repeat count at this position
            // of the alignment for each read that participates in the alignment.
            for(size_t j=0; j<edge.alignmentInfos.size(); j++) {
                baseInteger[j] = 4;
                repeatCount[j] = 1;
                if(!seqan::isGap(seqan::row(edge.seqanAlignment, j), i)) {
                    baseInteger[j] = edge.alignmentInfos[j].sequence[positions[j]].value;
                    repeatCount[j] = edge.alignmentInfos[j].repeatCounts[positions[j]];
                    ++positions[j];
                }
            }

            // Create a histogram of the base.
            array<int, 5> baseHistogram;
            fill(baseHistogram.begin(), baseHistogram.end(), 0);
            for(const size_t b: baseInteger) {
                ++baseHistogram[b];
            }

            // Find the most frequent base.
            const size_t bestBase = std::max_element(baseHistogram.begin(), baseHistogram.end())
                - baseHistogram.begin();
            // const size_t bestFrequency = baseHistogram[bestBase];
            using shasta::Base;
            /*
            cout << "Alignment position " << i << ": best base ";
            if(bestBase < 4) {
                cout << Base(uint8_t(bestBase), Base::FromInteger());
            } else {
                cout << "-";
            }
            cout << ", coverage " << bestFrequency;
            */

            if(bestBase < 4) {

                // Create a histogram of repeat counts for this base.
                vector<size_t> repeatCountHistogram;
                for(size_t j=0; j<baseInteger.size(); j++) {
                    if(baseInteger[j] == bestBase) {
                        if(repeatCount[j] >= repeatCountHistogram.size()) {
                            repeatCountHistogram.resize(repeatCount[j]+1, 0);
                        }
                        ++repeatCountHistogram[repeatCount[j]];
                    }
                }
                /*
                cout << "Repeat count histogram:";
                for(size_t r=0; r<repeatCountHistogram.size(); r++) {
                    if(repeatCountHistogram[r]) {
                        cout << " " << r << ":" << repeatCountHistogram[r];
                    }
                }
                */

                // Find the most frequent repeat count.
                const size_t bestRepeatCount = std::max_element(repeatCountHistogram.begin(), repeatCountHistogram.end())
                    - repeatCountHistogram.begin();
                // const size_t bestRepeatCountFrequency = repeatCountHistogram[bestRepeatCount];

                /*
                cout << ", best repeat count " << bestRepeatCount;
                cout << ", coverage " << bestRepeatCountFrequency;
                cout << endl;
                */
                for(size_t k=0; k<bestRepeatCount; k++) {
                    consensusSequence.push_back(Base(uint8_t(bestBase), Base::FromInteger()));
                }
            } else {
                // cout << endl;
            }

        }
        if(debug) {
            cout << "Consensus sequence:" << endl;
            copy(consensusSequence.begin(), consensusSequence.end(),
                ostream_iterator<shasta::Base>(cout));
            cout << endl;
        }


    }
}



// Create an optimal spanning tree and mark its edges.
void LocalMarkerGraph2::computeOptimalSpanningTree()
{
    LocalMarkerGraph2& graph = *this;

    // Mark all edges as initially not part of the optimal spanning tree.
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        graph[e].isSpanningTreeEdge = false;
    }

    // Gather all the edges and sort them by decreasing coverage.
    vector< pair<edge_descriptor, size_t> > edgeTable;
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        edgeTable.push_back(make_pair(e, graph[e].coverage()));
    }
    std::sort(edgeTable.begin(), edgeTable.end(),
        OrderPairsBySecondOnlyGreater<edge_descriptor, size_t>());

    // Map the vertices to integers in [0, number of vertices).
    const size_t n = boost::num_vertices(graph);
    std::map<vertex_descriptor, uint32_t> vertexMap;
    uint32_t vertexIndex = 0;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        vertexMap.insert(make_pair(v, vertexIndex++));
    }


    // Initialize the disjoint set data structures.
    vector<uint32_t> rank(n);
    vector<uint32_t> parent(n);
    boost::disjoint_sets<uint32_t*, uint32_t*> disjointSets(&rank[0], &parent[0]);
    for(size_t i=0; i<n; i++) {
        disjointSets.make_set(i);
    }

    // Process the edges in this order.
    // Only add each edge to the optimal spanning tree
    // if the two vertices are in two different connected components.
    for(const auto& p: edgeTable) {
        const edge_descriptor e = p.first;
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        const uint32_t i0 = vertexMap[v0];
        const uint32_t i1 = vertexMap[v1];

        // If v0 and v1 are in separate components,
        // add this edge to the optimal spanning tree.
        const uint32_t component0 = disjointSets.find_set(i0);
        const uint32_t component1 = disjointSets.find_set(i1);
        if(component0 != component1) {
            graph[e].isSpanningTreeEdge = true;
            disjointSets.union_set(i0, i1);
        }
    }
}



// Compute the best path in the optimal spanning tree.
// The optimal spanning tree must have already been computed.
void LocalMarkerGraph2::computeOptimalSpanningTreeBestPath()
{
    LocalMarkerGraph2& graph = *this;

    // Created a filtered graph that contains only the spanning tree edges.
    SpanningTreeFilter filter(graph);
    using FilteredGraph = boost::filtered_graph<LocalMarkerGraph2, SpanningTreeFilter>;
    FilteredGraph spanningTree(graph, filter);

    // Compute a topological sort of the spanning tree.
    // This always succeeds as the spanning tree is acyclic.
    vector<vertex_descriptor> topologicallySortedVertices;
    std::map<vertex_descriptor, uint32_t> colorMap;
    boost::topological_sort(spanningTree, back_inserter(topologicallySortedVertices),
        boost::color_map(boost::make_assoc_property_map(colorMap)));

    // Boost ::topological_sort returns the vertices in reverse topological.
    std::reverse(topologicallySortedVertices.begin(), topologicallySortedVertices.end());

    // In topological order, compute for each vertex a
    // pair(predecessor, distance),
    // where predecessor is the predecessor with the maximum distance.
    // See https://en.wikipedia.org/wiki/Longest_path_problem#Acyclic_graphs_and_critical_paths
    using Pair = pair<vertex_descriptor, uint32_t>;
    std::map<vertex_descriptor, Pair> vertexTable;
    for(const vertex_descriptor v0: topologicallySortedVertices) {
        Pair p = make_pair(null_vertex(), 0);
        BGL_FORALL_INEDGES(v0, e, spanningTree, FilteredGraph) {
            const vertex_descriptor v1 = source(e, graph);
            const auto it = vertexTable.find(v1);
            CZI_ASSERT(it != vertexTable.end());
            if(it->second.second+1 > p.second) {
                p.first = v1;
                p.second = it->second.second + 1;
            }
        }
        vertexTable.insert(make_pair(v0, p));
    }

    // Find the vertex with maximum distance. This is where the longest path ends.
    vertex_descriptor lastPathVertex = null_vertex();
    uint32_t lastPathVertexDistance = 0;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        const auto it = vertexTable.find(v);
        CZI_ASSERT(it != vertexTable.end());
        if(it->second.second > lastPathVertexDistance) {
            lastPathVertexDistance = it->second.second;
            lastPathVertex = v;
        }
    }


    // Construct the longest path, beginning at the end.
    optimalSpanningTreeBestPath.clear();
    vertex_descriptor v = lastPathVertex;
    while(v != null_vertex()) {
        const vertex_descriptor v1 = v;
        const vertex_descriptor v0 = vertexTable[v1].first;
        if(v0 == null_vertex()) {
            break;
        }
        edge_descriptor e;
        bool edgeWasFound;
        tie(e, edgeWasFound) = boost::edge(v0, v1, graph);
        CZI_ASSERT(edgeWasFound);
        optimalSpanningTreeBestPath.push_back(e);
        v = v0;
    }
    std::reverse(optimalSpanningTreeBestPath.begin(), optimalSpanningTreeBestPath.end());
    /*
    cout << "Optimal spanning tree best path:" << endl;
    for(const edge_descriptor e: optimalSpanningTreeBestPath) {
        cout << graph[source(e, graph)].vertexId << " " << graph[target(e, graph)].vertexId << endl;
    }
    */

    // Mark edges in the longest path.
    for(const edge_descriptor e: optimalSpanningTreeBestPath) {
        graph[e].isSpanningTreeBestPathEdge = true;
    }
}



// Given a path, find the longest subset that contains no vertices
// with distance equal to the specified maxDistance.
void LocalMarkerGraph2::clipPath(
    int maxDistance,
    const vector<edge_descriptor>& fullPath,
    vector<edge_descriptor>& clippedPath) const
{
    CZI_ASSERT(!fullPath.empty());
    const LocalMarkerGraph2& graph = *this;
    clippedPath.clear();

    // Create an interval_set containing the indexes
    // of the edges that don't use vertices at maxDistance.
    boost::icl::interval_set<size_t> s;
    for(size_t i=0; i<fullPath.size(); i++) {
        const edge_descriptor e = fullPath[i];
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        if(graph[v0].distance != maxDistance && graph[v1].distance != maxDistance) {
            s.add(i);
        }
    }
    if(s.empty()) {
        return;
    }



    // Find the largest interval in the interval set.
    size_t i0;
    size_t i1;
    size_t bestPathLength = 0;
    for(const auto x: s) {
        const size_t j0 = x.lower();
        const size_t j1 = x.upper();
        const size_t length = j1 + 1 - j0;
        if(length > bestPathLength) {
            bestPathLength = length;
            i0 = j0;
            i1 = j1;
        }

    }

    if(bestPathLength > 0) {
        copy(
            fullPath.begin() + i0,
            fullPath.begin() + i1 + 1,
            back_inserter(clippedPath));
    }
    CZI_ASSERT(clippedPath.size() == bestPathLength);

}



// The local assembly path is a clipped version of optimalSpanningTreeBestPath,
// in which vertices at maximum distance are removed.
void LocalMarkerGraph2::computeLocalAssemblyPath(int maxDistance)
{
    clipPath(
        maxDistance,
        optimalSpanningTreeBestPath,
        localAssemblyPath);

    // Mark the edges on the local assembly path.
    LocalMarkerGraph2& graph = *this;
    for(const edge_descriptor e: localAssemblyPath) {
        graph[e].isLocalAssemblyPathEdge = true;
    }
}



// Compute the clipped optimal spanning tree path
// and use it to assemble its dominant sequence.
void LocalMarkerGraph2::assembleDominantSequence(
    int maxDistance,
    vector< pair<shasta::Base, int> >& sequence)
{
    CZI_ASSERT(!optimalSpanningTreeBestPath.empty());
    computeLocalAssemblyPath(maxDistance);
    assembleDominantSequence(localAssemblyPath, sequence);

}


// Assemble the dominant sequence for a given path.
void LocalMarkerGraph2::assembleDominantSequence(
    const vector<edge_descriptor>& path,
    vector< pair<shasta::Base, int> >& sequence)
{
    CZI_ASSERT(!path.empty());
    const LocalMarkerGraph2& graph = *this;

    // This is only used with the run-length representation of reads.
    CZI_ASSERT(useRunLengthReads);

    // Compute SeqAn alignments for all edges on the local assembly path.
    computeSeqanAlignments();

    // Start with empty sequence.
    sequence.clear();

    // Add the sequence of the first vertex, with its coverage
    const vertex_descriptor v0 = source(path.front(), graph);
    const KmerId kmerId0 = getKmerId(v0);
    const Kmer kmer0(kmerId0, k);
    const LocalMarkerGraph2Vertex& vertex0 = graph[v0];
    const auto coverage0 = vertex0.markerInfos.size();
    for(size_t i=0; i<k; i++) {
        sequence.push_back(make_pair(kmer0[i], coverage0));
    }

    // Loop over all edges in the path.
    for(const edge_descriptor e: path) {

        // Add the edge sequence, if any.
        const LocalMarkerGraph2Edge& edge = graph[e];
        CZI_ASSERT(!edge.infos.empty());
        const auto& p = edge.infos.front();
        const auto coverage = p.second.size();
        const auto& edgeSequence = p.first.sequence;
        for(const shasta::Base base: edgeSequence) {
            sequence.push_back(make_pair(base, coverage));
        }

        // Add the sequence of the target vertex of this edge.
        const vertex_descriptor v1 = target(e, graph);
        const KmerId kmerId1 = getKmerId(v1);
        const Kmer kmer1(kmerId1, k);
        const LocalMarkerGraph2Vertex& vertex1 = graph[v1];
        const auto coverage1 = vertex1.markerInfos.size();

        // First, handle any overlapping bases.
        for(size_t i=0; i<size_t(p.first.overlappingBaseCount); i++) {
            const size_t positionInAssembledSequence = sequence.size() - p.first.overlappingBaseCount + i;
            CZI_ASSERT(sequence[positionInAssembledSequence].first == kmer1[i]);
            sequence[positionInAssembledSequence].second =
                max(sequence[positionInAssembledSequence].second, int(coverage1));
        }

        // Handle the non-overlapping bases.
        for(size_t i=size_t(p.first.overlappingBaseCount); i<k; i++) {
            sequence.push_back(make_pair(kmer1[i], coverage1));
        }
#if 0
        cout << "After processing edge ";
        cout << graph[source(e, graph)].vertexId << " ";
        cout << graph[target(e, graph)].vertexId << " ";
        cout << sequence.size() << endl;
        for(const auto& p: sequence) {
            const auto coverage = p.second;
            if(coverage < 10) {
                cout << char(std::tolower(p.first.character()));
            } else {
                cout << p.first;
            }
        }
        cout << endl;
        for(const auto& p: sequence) {
            const auto coverage = p.second;
            if(coverage<10) {
                cout << coverage;
            } else {
                cout << " ";
            }
        }
        cout << endl;
#endif
    }

}



// Version of assembleDominantSequence for the case where
// we use a run-length representation of the reads.
// This  assumes that clippedOptimalSpanningTreeBestPath was
// already computed and only creates html output.
void LocalMarkerGraph2::assembleDominantSequence(ostream& html) const
{
    // Shorthands.
    const LocalMarkerGraph2& graph = *this;
    const vector<edge_descriptor>& path = localAssemblyPath;
    using MarkerInfo = LocalMarkerGraph2Vertex::MarkerInfo;
    using InfoWithRepeatCounts = LocalMarkerGraph2Edge::InfoWithRepeatCounts;
    using Sequence = LocalMarkerGraph2Edge::Sequence;
    using shasta::Base;

    // If the path is empty, do nothing.
    if(path.empty()) {
        html << "<p>The assembly path is empty.";
        return;
    }



    // Create two vectors with the vertices and edges
    // contributing to assembled sequence and their positions.
    // Edges with overlapping sequence are not included.
    vector< pair<vertex_descriptor, uint32_t> > pathVertices;
    vector< pair<edge_descriptor, uint32_t> > pathEdges;
    uint32_t assembledPosition = 0;
    for(const edge_descriptor e: path) {
        const vertex_descriptor v0 = source(e, graph);
        pathVertices.push_back(make_pair(v0, assembledPosition));
        assembledPosition += k;
        const LocalMarkerGraph2Edge& edge = graph[e];
        CZI_ASSERT(!edge.infos.empty());
        const pair<Sequence, vector<InfoWithRepeatCounts> > p = edge.infos.front();
        const Sequence& sequence = p.first;
        if(sequence.sequence.size() == 0) {
            // This edge does not contribute
            assembledPosition -= sequence.overlappingBaseCount;
        } else {
            pathEdges.push_back(make_pair(e, assembledPosition));
            assembledPosition += uint32_t(sequence.sequence.size());
        }
    }
    const vertex_descriptor vLast = target(path.back(), graph);
    pathVertices.push_back(make_pair(vLast, assembledPosition));
    const uint32_t assembledLength = assembledPosition + k;


    // Write out the contributing vertices and edges.
    if(false) {
        cout << "Vertices contributing to assembled sequence:" << endl;
        for(const auto& p: pathVertices) {
            const vertex_descriptor v = p.first;
            const uint32_t position = p.second;
            const LocalMarkerGraph2Vertex& vertex = graph[v];
            cout << "Vertex " << vertex.vertexId;
            cout << " rank " << vertex.rank;
            cout << " position " << position << endl;
        }
        cout << "Edges contributing to assembled sequence:" << endl;
        for(const auto& p: pathEdges) {
            const edge_descriptor e = p.first;
            const uint32_t position = p.second;
            const vertex_descriptor v0 = source(e, graph);
            const vertex_descriptor v1 = target(e, graph);
            const LocalMarkerGraph2Vertex& vertex0 = graph[v0];
            const LocalMarkerGraph2Vertex& vertex1 = graph[v1];
            cout << "Edge " << vertex0.vertexId << "->";
            cout << vertex1.vertexId << "->";
            cout << " ranks " << vertex0.rank << " " << vertex1.rank;
            cout << " position " << position << endl;
        }
        cout << "Assembled length " << assembledLength << endl;
    }


    // Assemble sequence.
    vector<Base> sequence(assembledLength);
    vector<bool> sequenceFlag(assembledLength, false);
    for(const auto& p: pathVertices) {
        const vertex_descriptor v = p.first;
        const uint32_t position = p.second;
        const KmerId kmerId = graph.getKmerId(v);
        const Kmer kmer(kmerId, k);
        for(size_t i=0; i<k; i++) {
            const Base base = kmer[i];
            if(sequenceFlag[position+i]) {
                CZI_ASSERT(sequence[position+i] == base);
            } else {
                sequence[position+i] = base;
                sequenceFlag[position+i] = true;
            }
        }
    }
    for(const auto& p: pathEdges) {
        const edge_descriptor e = p.first;
        const uint32_t position = p.second;
        const LocalMarkerGraph2Edge& edge = graph[e];
        CZI_ASSERT(!edge.infos.empty());
        const pair<Sequence, vector<InfoWithRepeatCounts> > q = edge.infos.front();
        const vector<Base>& edgeSequence = q.first.sequence;
        for(size_t i=0; i<edgeSequence.size(); i++) {
            const Base base = edgeSequence[i];
            CZI_ASSERT(!sequenceFlag[position+i]) ;
            sequence[position+i] = base;
            sequenceFlag[position+i] = true;
        }
    }
    CZI_ASSERT(find(sequenceFlag.begin(), sequenceFlag.end(), false) == sequenceFlag.end());
    if(false) {
        cout << "Assembled sequence:" << endl;
        copy(sequence.begin(), sequence.end(), ostream_iterator<Base>(cout));
        cout << endl;
    }


    // For each position in assembled sequence, store unique
    // combinations OrientedReadId/position/repeatCount.
    // Indexed by position in assembled sequence.
    vector< std::set< tuple<OrientedReadId, uint32_t, uint8_t> > > repeatCountTable(assembledLength);
    for(const auto& p: pathVertices) {
        const vertex_descriptor v = p.first;
        const uint32_t position = p.second;
        const LocalMarkerGraph2Vertex& vertex = graph[v];
        for(const MarkerInfo& markerInfo: vertex.markerInfos) {
            const MarkerId markerId = markerInfo.markerId;
            const CompressedMarker& marker = markers.begin()[markerId];
            const vector<uint8_t> counts = getRepeatCounts(markerInfo);
            for(uint32_t i=0; i<k; i++) {
                repeatCountTable[position+i].insert(
                    make_tuple(markerInfo.orientedReadId, marker.position+i, counts[i]));
            }
        }
    }
    for(const auto& p: pathEdges) {
        const edge_descriptor e = p.first;
        const uint32_t position = p.second;
        const LocalMarkerGraph2Edge& edge = graph[e];
        CZI_ASSERT(!edge.infos.empty());
        const pair<Sequence, vector<InfoWithRepeatCounts> > q = edge.infos.front();
        const Sequence& sequence = q.first;
        const vector<InfoWithRepeatCounts>& infos = q.second;
        CZI_ASSERT(sequence.sequence.size() > 0);
        for(const InfoWithRepeatCounts& info: infos) {
            CZI_ASSERT(info.repeatCounts.size() == sequence.sequence.size());
            const CompressedMarker& marker = markers[info.orientedReadId.getValue()][info.ordinals[0]];
            for(uint32_t i=0; i<info.repeatCounts.size(); i++) {
                repeatCountTable[position+i].insert(
                    make_tuple(info.orientedReadId, marker.position+k+i, info.repeatCounts[i]));

            }

        }
    }
    if(false) {
        for(uint32_t position=0; position<assembledLength; position++) {
            const auto & s = repeatCountTable[position];
            for(const auto& x: s) {
                cout << "Assembled position " << position;
                cout << ": " << get<0>(x) << " " << get<1>(x) << " " << int(get<2>(x)) << endl;
            }
        }
    }



    // For each assembled position, create a histogram of repeat counts.
    vector<Histogram> histogram(assembledLength);
    for(uint32_t position=0; position<assembledLength; position++) {
        std::set< tuple<OrientedReadId, uint32_t, uint8_t> >& s = repeatCountTable[position];
        Histogram& h = histogram[position];
        for(const tuple<OrientedReadId, uint32_t, uint8_t>& t: s) {
            h.increment(get<2>(t));
        }
    }
    if(false) {
        for(uint32_t position=0; position<assembledLength; position++) {
            const Histogram& h = histogram[position];
            for(size_t repeatCount=0; repeatCount<h.size(); repeatCount++) {
                const size_t frequency = h[repeatCount];
                if(frequency) {
                    cout << position << " " << repeatCount << " " << frequency << endl;
                }
            }
        }
    }



    // Begin the big table describing assembled sequence.
    html << "<table style='font-family:monospace;font-size:10px'>";



    // Assembled sequence in run-length representation.
    html << "<tr title='Position on assembled sequence in run-length representation'>"
        "<th class=left style='white-space: nowrap'>Position (run-length)";
    for(uint32_t position=0; position<assembledLength; position++) {
        html << "<td class=centered>" << position;
    }
    html << "<tr title='Assembled sequence in run-length representation'>"
        "<th class=left style='white-space:nowrap'>Assembled sequence (run-length)";
    for(uint32_t position=0; position<assembledLength; position++) {
        const Base base = sequence[position];
        const size_t bestCoverage = histogram[position].bestFrequency();
        char c = base.character();
        if(bestCoverage < 10) {
            c = char(tolower(c));
        }
        html << "<td class=centered>" << c;
    }



    // Best repeat count.
    html << "<tr title='Most frequent repeat count'>"
        "<th class=left style='white-space:nowrap'>Most frequent repeat count";
    for(uint32_t position=0; position<assembledLength; position++) {
        html << "<td  class=centered>" << histogram[position].bestValue();
    }

    // Coverage for best repeat count.
    html << "<tr title='Coverage for most frequent repeat count'>"
        "<th class=left style='white-space:nowrap'>Coverage for most frequent repeat count";
    for(uint32_t position=0; position<assembledLength; position++) {
        html << "<td  class=centered>" << histogram[position].bestFrequency();
    }


    // Total coverage.
    html << "<tr title='Total coverage for all repeat counts'>"
        "<th class=left style='white-space:nowrap'>Total coverage for all repeat counts";
    for(uint32_t position=0; position<assembledLength; position++) {
        const size_t totalCoverage = histogram[position].sum();
        html << "<td  class=centered>" << totalCoverage;
    }


    // Coverage for each repeat count.
    std::set<size_t> activeRepeatCounts;
    for(uint32_t position=0; position<assembledLength; position++) {
        const Histogram& h = histogram[position];
        for(size_t repeatCount=0; repeatCount<h.size(); repeatCount++) {
            if(h[repeatCount]) {
                activeRepeatCounts.insert(repeatCount);
            }
        }
    }
    for(const size_t repeatCount: activeRepeatCounts) {
        html << "<tr title='Coverage for repeat count " << repeatCount << "'>"
            "<th class=left style='white-space:nowrap'>Coverage for repeat count " << repeatCount;
        for(uint32_t position=0; position<assembledLength; position++) {
            const Histogram& h = histogram[position];
            html << "<td  class=centered>";
            if(repeatCount < h.size()) {
                const size_t coverage = h[repeatCount];
                if(coverage) {
                    html << coverage;
                }
            }
        }
    }



    html << "<tr title='Assembled sequence'>"
        "<th class=left style='white-space:nowrap'>Assembled sequence";
    string rawAssembledSequence;
    for(uint32_t position=0; position<assembledLength; position++) {
        const Base base = sequence[position];
        const size_t bestCoverage = histogram[position].bestFrequency();
        const size_t bestRepeatCount = histogram[position].bestValue();
        char c = base.character();
        if(bestCoverage < 10) {
            c = char(tolower(c));
        }
        html << "<td class=centered>";
        for(size_t i=0; i<bestRepeatCount; i++) {
            html << c;
            rawAssembledSequence.push_back(c);
        }
    }



    html << "<tr title='Position in assembled sequence'>"
        "<th class=left style='white-space:nowrap'>Position in assembled sequence";
    size_t totalBaseCount = 0;
    for(uint32_t position=0; position<assembledLength; position++) {
        const size_t bestRepeatCount = histogram[position].bestValue();
        html << "<td class=centered>" << totalBaseCount;
        totalBaseCount += bestRepeatCount;
    }

    // End the table.
    html << "</table>";
    html << "Assembled " << assembledLength << " bases in run-length representation "
        "corresponding to a total " << totalBaseCount << " bases:";




    // Write assembled sequence outside the table.
    html << "<pre style='margin:0px'>";
    for(size_t position=0; position<rawAssembledSequence.size(); position+=10) {
        const string label = to_string(position);
        html << label;
        for(size_t i=0; i<10-label.size(); i++) {
            html << " ";
        }
    }
    html << "</pre>";
    html << "<pre style='margin:0px'>";
    for(size_t position=0; position<rawAssembledSequence.size(); position++) {
        if((position%10)==0) {
            html << "|";
        } else if((position%5)==0) {
            html << "+";
        } else {
            html << ".";
        }
    }
    html << "</pre>";
    html << "<pre style='margin:0px'>" << rawAssembledSequence << "</pre>";

    html <<
        "<a id=fastaDownload>Download in FASTA format</a><br>"
        "<script>"
        "var element = document.getElementById('fastaDownload');"
        "element.setAttribute('href', 'data:text/plain;charset=utf-8,' +"
        "encodeURIComponent('>AssembledSequence " << rawAssembledSequence.size() <<
        "\\n" <<  rawAssembledSequence << "\\n'));"
        "element.setAttribute('download', 'AssembledSequence.fa');"
        "</script>";
}



// Approximate topological sort, adding edges
// in order of decreasing coverage. The topological sort
// stored in LocalMarkerGrapg2Vertex::rank.
void LocalMarkerGraph2::approximateTopologicalSort()
{
    LocalMarkerGraph2& graph = *this;

    vector<pair<uint32_t, edge_descriptor> > edgeTable;
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        edgeTable.push_back(make_pair(graph[e].coverage(), e));
    }
    sort(edgeTable.begin(), edgeTable.end(),
        std::greater< pair<uint32_t, edge_descriptor> >());

    vector<edge_descriptor> sortedEdges;
    for(const auto& p: edgeTable) {
        sortedEdges.push_back(p.second);
    }

    shasta::approximateTopologicalSort(graph, sortedEdges);


    // Also store the vertices in topological sort order.
    vector< pair<size_t, vertex_descriptor> > vertexTable;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        vertexTable.push_back(make_pair(graph[v].rank, v));
    }
    sort(vertexTable.begin(), vertexTable.end());
    topologicallySortedVertices.clear();
    for(const auto& p: vertexTable) {
        topologicallySortedVertices.push_back(p.second);
    }

}


// Remove edges that are not on the spanning tree.
// This does not remove any vertices, as the spanning
// tree by definition covers all vertices.
void LocalMarkerGraph2::removeNonSpanningTreeEdges()
{
    LocalMarkerGraph2& graph = *this;

    vector<edge_descriptor> edgesToBeRemoved;
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        if(!graph[e].isSpanningTreeEdge) {
            edgesToBeRemoved.push_back(e);
        }
    }

    for(const edge_descriptor e: edgesToBeRemoved) {
        boost::remove_edge(e, graph);
    }
}



// Remove vertices and edges that are not on the optimal path.
void LocalMarkerGraph2::removeAllExceptOptimalPath()
{
    LocalMarkerGraph2& graph = *this;

    std::set<vertex_descriptor> verticesToBeKept;
    vector<edge_descriptor> edgesToBeRemoved;
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        if(graph[e].isSpanningTreeBestPathEdge) {
            verticesToBeKept.insert(source(e, graph));
            verticesToBeKept.insert(target(e, graph));
        } else {
            edgesToBeRemoved.push_back(e);
        }
    }

    vector<vertex_descriptor> verticesToBeRemoved;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        if(verticesToBeKept.find(v) == verticesToBeKept.end()) {
            verticesToBeRemoved.push_back(v);
        }
    }

    // Remove the edges first!
    for(const edge_descriptor e: edgesToBeRemoved) {
        boost::remove_edge(e, graph);
    }

    for(const vertex_descriptor v: verticesToBeRemoved) {
        boost::clear_vertex(v, graph);
        boost::remove_vertex(v, graph);
    }
}



// Remove vertices and edges that are not on the clipped optimal path.
void LocalMarkerGraph2::removeAllExceptClippedOptimalPath()
{
    LocalMarkerGraph2& graph= *this;

    // Find the vertices and edges to be kept.
    std::set<vertex_descriptor> verticesToBeKept;
    std::set<edge_descriptor> edgesToBeKept;
    for(const edge_descriptor e: localAssemblyPath) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        verticesToBeKept.insert(v0);
        verticesToBeKept.insert(v1);
        edgesToBeKept.insert(e);
    }

    // Find the vertices and edges to be removed.
    vector<vertex_descriptor> verticesToBeRemoved;
    vector<edge_descriptor> edgesToBeRemoved;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        if(verticesToBeKept.find(v) == verticesToBeKept.end()) {
            verticesToBeRemoved.push_back(v);
        }
    }
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        if(edgesToBeKept.find(e) == edgesToBeKept.end()) {
            edgesToBeRemoved.push_back(e);
        }
    }

    // Remove the edges first!
    // Doing the opposite causes a crash when attempting
    // to remove inexistent edges.
    for(const edge_descriptor e: edgesToBeRemoved) {
        boost::remove_edge(e, graph);
    }

    // Now remove the vertices.
    for(const vertex_descriptor v: verticesToBeRemoved) {
        clear_vertex(v, graph);
        remove_vertex(v, graph);
    }
}



// Fill in the oriented reads represented in the local marker graph.
void LocalMarkerGraph2::findOrientedReadIds()
{
    LocalMarkerGraph2& graph = *this;
    std::set<OrientedReadId> orientedReadIdsSet;
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        for(const auto& markerInfo: graph[v].markerInfos) {
            orientedReadIdsSet.insert(markerInfo.orientedReadId);
        }
    }

    orientedReadIds.clear();
    orientedReadIds.insert(
        orientedReadIds.end(),
        orientedReadIdsSet.begin(),
        orientedReadIdsSet.end());
}



// Compute the set of vertices that corresponds to a given oriented read.
// Vertices are returned in a pair with the corresponding ordinal,
// sorted by the ordinal.
void LocalMarkerGraph2::getOrientedReadVertices(
    OrientedReadId orientedReadId,
    vector< pair<uint32_t, vertex_descriptor> >& orientedReadVertices) const
{
    const LocalMarkerGraph2& graph = *this;

    orientedReadVertices.clear();
    BGL_FORALL_VERTICES(v, graph, LocalMarkerGraph2) {
        bool found = true;
        uint32_t ordinal;
        tie(found, ordinal) = graph[v].getOrdinal(orientedReadId);
        if(found) {
            orientedReadVertices.push_back(make_pair(ordinal, v));
        }
    }
    sort(orientedReadVertices.begin(), orientedReadVertices.end());

}



// Given a vector of vertices returned by getOrientedReadVertices,
// return a subset that does not break rank ordering.
void LocalMarkerGraph2::enforceRankOrder(
    const vector< pair<uint32_t, vertex_descriptor> >& v,
    vector< pair<uint32_t, vertex_descriptor> >& u) const
{
    const LocalMarkerGraph2& graph = *this;

    // Create an interval_set of rank values that should be excluded.
    boost::icl::interval_set<size_t> inconsistentRanks;
    for(size_t i=1; i<v.size(); i++) {
        const vertex_descriptor v0 = v[i-1].second;
        const vertex_descriptor v1 = v[i].second;
        const LocalMarkerGraph2Vertex& vertex0 = graph[v0];
        const LocalMarkerGraph2Vertex& vertex1 = graph[v1];
        const size_t rank0 = vertex0.rank;
        const size_t rank1 = vertex1.rank;
        if(rank1 <= rank0) {
            inconsistentRanks += boost::icl::interval<size_t>::closed(rank1, rank0);
        }
    }

    /*
    cout << "Inconsistent ranks:" << endl;
    for(const auto& x: inconsistentRanks) {
        cout << x << endl;
    }
    */

    // Now copy v to u, but exclude vertices with rank
    // in the forbidden ranges.
    u.clear();
    for(const auto& p: v) {
        const vertex_descriptor v = p.second;
        const LocalMarkerGraph2Vertex& vertex = graph[v];
        if(inconsistentRanks.find(vertex.rank) == inconsistentRanks.end()) {
            u.push_back(p);
        } else {
            // cout << "Skipped " << vertex.vertexId << " rank " << vertex.rank << endl;
        }

    }

}



// Compute the set of edges that corresponds to a given oriented read.
// Each edge is returned in a tuple containing the two ordinals
// for the given oriented read.
// The edges are computed sorted by the ordinals.
void LocalMarkerGraph2::getOrientedReadEdges(
    OrientedReadId orientedReadId,
    vector< pair< array<uint32_t, 2>, edge_descriptor> >& orientedReadEdges) const
{
    const LocalMarkerGraph2& graph = *this;

    orientedReadEdges.clear();
    BGL_FORALL_EDGES(e, graph, LocalMarkerGraph2) {
        array<uint32_t, 2> ordinals;
        if(graph[e].getOrdinals(orientedReadId, ordinals)) {
            // const vertex_descriptor v0 = source(e, graph);
            // const vertex_descriptor v1 = target(e, graph);
            // const LocalMarkerGraph2Vertex& vertex0 = graph[v0];
            // const LocalMarkerGraph2Vertex& vertex1 = graph[v1];
            orientedReadEdges.push_back(make_pair(ordinals, e));
        }
    }
    sort(orientedReadEdges.begin(), orientedReadEdges.end());

}



// Look for the ordinal for a given oriented read id.
// If found, returns pair(true, ordinal).
// Otherwise, returns pair(false, don't care).
// If more than an ordinal is found, the first one is returned.
pair<bool, uint32_t> LocalMarkerGraph2Vertex::getOrdinal(
    OrientedReadId orientedReadId) const
{
    for(const MarkerInfo& markerInfo: markerInfos) {
        if(markerInfo.orientedReadId == orientedReadId) {
            return make_pair(true, markerInfo.ordinal);
        }
    }
    return make_pair(false, std::numeric_limits<uint32_t>::max());
}



// Look for the ordinals for a given oriented read id.
// If found, returns true.
// If more than an ordinal pairs is found, the first one is returned.
bool LocalMarkerGraph2Edge::getOrdinals(
    OrientedReadId orientedReadId,
    array<uint32_t, 2>& ordinals) const
{
    for(const pair<Sequence, vector<InfoWithRepeatCounts> >& p: infos) {
        for(const Info& info: p.second) {
            if(info.orientedReadId == orientedReadId) {
                ordinals = info.ordinals;
                return true;
            }
        }
    }

    // If getting here, we did not find it.
    return false;
}


// Write the graph in Graphviz format.
void LocalMarkerGraph2::write(
    const string& fileName,
    size_t minCoverage,
    int maxDistance,
    bool detailed,
    bool showVertexId) const
{
    ofstream outputFileStream(fileName);
    if(!outputFileStream) {
        throw runtime_error("Error opening " + fileName);
    }
    write(outputFileStream, minCoverage, maxDistance, detailed, showVertexId);
}
void LocalMarkerGraph2::write(
    ostream& s,
    size_t minCoverage,
    int maxDistance,
    bool detailed,
    bool showVertexId) const
{
    Writer writer(*this, minCoverage, maxDistance, detailed, showVertexId);
    boost::write_graphviz(s, *this, writer, writer, writer,
        boost::get(&LocalMarkerGraph2Vertex::vertexId, *this));
}

LocalMarkerGraph2::Writer::Writer(
    const LocalMarkerGraph2& graph,
    size_t minCoverage,
    int maxDistance,
    bool detailed,
    bool showVertexId) :
    graph(graph),
    minCoverage(minCoverage),
    maxDistance(maxDistance),
    detailed(detailed),
    showVertexId(showVertexId)
{
}



void LocalMarkerGraph2::Writer::operator()(std::ostream& s) const
{
    // This turns off the tooltip on the graph and the edges.
    s << "tooltip = \" \";\n";

    if(detailed) {
        s << "layout=dot;\n";
        s << "rankdir=LR;\n";
        s << "ratio=expand;\n";
        s << "node [fontname = \"Courier New\" shape=rectangle];\n";
        s << "edge [fontname = \"Courier New\"];\n";
    } else {
        s << "layout=sfdp;\n";
        s << "smoothing=triangle;\n";
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

        // Id, so we can use JavaScript code to manipulate the vertex.
        s << "id=vertex" << vertex.vertexId;

        // Tooltip.
        s << " tooltip=\"";
        if(showVertexId) {
            s << "Vertex " << vertex.vertexId << ", coverage ";
        } else {
            s << "Coverage ";
        }
        s << coverage << ", distance " << vertex.distance << ", rank " << vertex.rank;
        s << ", click to recenter graph here, right click for detail\"";

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
            color = "#90ee90";
        } else  if(coverage >= minCoverage) {
            color = "black";
        } else {
            color = "red";
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
            color = "#90ee90";
        } else if(coverage >= minCoverage) {
            color = "green";
        } else {
            color = "red";
        }
        s << " style=filled";
        s << " fillcolor=\"" << color << "\"";

        // Id, so we can use JavaScript code to manipulate the vertex.
        s << " id=vertex" << vertex.vertexId;

        // Tooltip.
        s << " tooltip=\"";
        if(showVertexId) {
            s << "Vertex " << vertex.vertexId << ", coverage ";
        } else {
            s << "Coverage ";
        }
        s << coverage << ", distance " << vertex.distance << ", rank "  << vertex.rank << "\"";

        // Write the label using Graphviz html-like functionality.
        s << " label=<<font><table border=\"0\">";
        const int columnCount = graph.useRunLengthReads ? 4 : 3;

        // Vertex id.
        if(showVertexId) {
            s << "<tr><td colspan=\"" << columnCount << "\"><b>";
            s << "Vertex " << vertex.vertexId;
            s << "</b></td></tr>";
        }

        // Kmer.
        s << "<tr><td colspan=\"" << columnCount << "\"><b>";
        kmer.write(s, k);
        s << "</b></td></tr>";

        // Coverage.
        s << "<tr><td colspan=\"" << columnCount << "\"><b>";
        s << "Coverage " << coverage;
        s << "</b></td></tr>";

        // Distance.
        s << "<tr><td colspan=\"" << columnCount << "\" ";
        s << " href=\"\"";  // Necessary to activate tooltip.
        s << " id=\"vertexDistance" << vertex.vertexId << "\" tooltip=\"Click to recenter graph here\">";
        s << "<font color=\"blue\"><b><u>Distance " << vertex.distance;
        s << "</u></b></font></td></tr>";

        // Rank.
        s << "<tr><td colspan=\"" << columnCount << "\">";
        s << "<b>Rank " << vertex.rank;
        s << "</b></td></tr>";

        // Column headers.
        s << "<tr><td><b>Read</b></td><td><b>Ord</b></td><td><b>Pos</b></td>";
        if(graph.useRunLengthReads) {
            s << "<td><b>Repeat</b></td>";
        }
        s << "</tr>";

        // A row for each marker of this vertex.
        for(const auto& markerInfo: vertex.markerInfos) {
            const CompressedMarker& marker = graph.markers.begin()[markerInfo.markerId];

            // OrientedReadId
            s << "<tr><td align=\"right\"";
            s << " href=\"exploreRead?readId&amp;" << markerInfo.orientedReadId.getReadId();
            s << "&amp;strand=" << markerInfo.orientedReadId.getStrand() << "\"";
            s << "><font color=\"blue\"><b><u>" << markerInfo.orientedReadId << "</u></b></font></td>";

            // Ordinal.
            s << "<td align=\"right\"";
            s << " href=\"exploreRead?readId=" << markerInfo.orientedReadId.getReadId();
            s << "&amp;strand=" << markerInfo.orientedReadId.getStrand();
            s << "&amp;highlightMarker=" << markerInfo.ordinal;
            s << "\"";
            s << "><font color=\"blue\"><b><u>" << markerInfo.ordinal << "</u></b></font></td>";

            // Position.
            s << "<td align=\"right\"><b>" << marker.position << "</b></td>";

            // Repeat counts.
            if(graph.useRunLengthReads) {
                const vector<uint8_t> counts = graph.getRepeatCounts(markerInfo);
                s << "<td><b>";
                for(size_t i=0; i<k; i++) {
                    if(counts[i] < 10) {
                        s << int(counts[i]);
                    } else {
                        s << "*";
                    }
                }
                s << "</b></td>";
            }

            s << "</tr>";
        }


        // End the table.
        s << "</table></font>>";

        // End vertex attributes.
        s << "]";
    }
}



void LocalMarkerGraph2::Writer::operator()(std::ostream& s, edge_descriptor e) const
{

    const LocalMarkerGraph2Edge& edge = graph[e];
    const size_t coverage = edge.coverage();
    CZI_ASSERT(coverage > 0);
    const size_t consensus = edge.consensus();

    if(!detailed) {

        // Compact output.

        // Begin edge attributes.
        s << "[";

        // Tooltip.
        s << "tooltip=\"Coverage " << coverage << ", consensus " << consensus << "\"";

        // Color.
        string color;
        if(edge.isSpanningTreeEdge) {
            color = "violet";
        } else if(coverage >= minCoverage) {
            color = "black";
        } else {
            color = "red";
        }
        s << " fillcolor=\"" << color << "\"";
        s << " color=\"" << color << "\"";

        // Thickness is determined by coverage.
        const double thickness = 0.2 * double(coverage==0 ? 1 : coverage);
        s << " penwidth=";
        const auto oldPrecision = s.precision(4);
        s <<  thickness;
        s.precision(oldPrecision);

        // Style.
        if(edge.isSpanningTreeEdge && !edge.isSpanningTreeBestPathEdge) {
            s << " style=dashed";
        }

        // Weight;
        s << " weight=" << coverage;

        // End edge attributes.
        s << "]";

    } else {

        // Detailed output.

        // Begin edge attributes.
        s << "[";

        const string tooltipText = "Coverage " + to_string(coverage) + ", consensus " +to_string(consensus);
        s << " tooltip=\"" << tooltipText << "\"";
        s << " labeltooltip=\"" << tooltipText << "\"";
        // s << " URL=\"#abcdef\"";   // Hack to convince graphviz to not ignore the labeltooltip.

        // Thickness is determined by coverage.
        const double thickness = 0.5 * double(coverage==0 ? 1 : coverage);
        s << " penwidth=";
        const auto oldPrecision = s.precision(4);
        s <<  thickness;
        s.precision(oldPrecision);

        // Style.
        if(edge.isSpanningTreeEdge && !edge.isSpanningTreeBestPathEdge) {
            s << " style=dashed";
        }

        // Color.
        string color;
        if(edge.isSpanningTreeEdge) {
            color = "violet";
        } else if(coverage >= minCoverage) {
            color = "black";
        } else {
            color = "red";
        }
        s << " fillcolor=\"" << color << "\"";
        s << " color=\"" << color << "\"";

        // Label color (used below).
        string labelColor;
        if(color == "black") {
            labelColor = "green";
        } else {
            labelColor = color;
        }


        // Weight;
        s << " weight=" << coverage;

        // If the edge was not marked as a DAG edge during approximate topological sort,
        // tell graphviz not to use it in constraint assignment.
        if(!edge.isDagEdge) {
            s << " constraint=false";
        }

        // Label.
        s << " label=<<font color=\"black\">";
        s << "<table";
        s << " color=\"black\"";
        s << " bgcolor=\"" << labelColor << "\"";
        s << " border=\"0\"";
        s << " cellborder=\"1\"";
        s << " cellspacing=\"1\"";
        s << ">";

        // Consensus and coverage.
        const int columnCount = graph.useRunLengthReads ? 5 : 4;
        s << "<tr><td colspan=\"" << columnCount << "\"><b>Coverage " << coverage << "</b></td></tr>";
        s << "<tr><td colspan=\"" << columnCount << "\"><b>Consensus " << consensus << "</b></td></tr>";

        // Header row.
        s <<
            "<tr>"
            "<td align=\"center\"><b>Read</b></td>"
            "<td align=\"center\"><b>Ord0</b></td>"
            "<td align=\"center\"><b>Ord1</b></td>"
            "<td align=\"center\"><b>Seq</b></td>";
        if(graph.useRunLengthReads) {
            s << "<td align=\"center\"><b>Repeat</b></td>";
        }
        s << "</tr>";

        // Loop over the infos table for this edge.
        for(const auto& p: edge.infos) {
            const auto& sequence = p.first;
            const auto& infos = p.second;

            // Construct the string representing this sequence.
            string sequenceString;
            if(sequence.sequence.empty()) {
                sequenceString = to_string(sequence.overlappingBaseCount);
            } else {
                for(const shasta::Base base: sequence.sequence) {
                    sequenceString.push_back(base.character());
                }
            }



            for(auto it=infos.begin(); it!=infos.end(); ++it) {
                const auto& info = *it;
                s << "<tr><td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand() << "\"";
                s << "><font color=\"blue\"><b><u>" << info.orientedReadId << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << info.ordinals[0];
                s << "&amp;highlightMarker=" << info.ordinals[1];
                s << "\"";
                s << "><font color=\"blue\"><b><u>" << info.ordinals[0] << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << info.ordinals[0];
                s << "&amp;highlightMarker=" << info.ordinals[1];
                s << "\"";
                s << "><font color=\"blue\"><b><u>" << info.ordinals[1] << "</u></b></font></td>";

                s << "<td align=\"center\"><b>";
                if(it == infos.begin()) {
                    if(sequenceString.size() > 100) {
                        s << "Too long";
                    } else {
                        s << sequenceString;
                    }
                } else {
                    s << "=";
                }
                s << "</b></td>";

                // Write out the repeat counts, if necessary.
                if(graph.useRunLengthReads && !info.repeatCounts.empty()) {
                    s << "<td align=\"center\"><b>";
                    for(const uint8_t repeatCount: info.repeatCounts) {
                        if(repeatCount < 10) {
                            s << int(repeatCount);
                        } else {
                            s << "*";
                        }
                    }
                    s << "</b></td>";
                }

                s << "</tr>";
            }
        }



        // If the SeqAn alignment was computed, also write it to the table.
        if(edge.seqanAlignmentWasComputed) {
            s << "<tr><td colspan=\"" << columnCount << "\"><b>SeqAn alignment</b></td></tr>";

            // Add one row to the table for each read.
            for(size_t i=0; i<edge.alignmentInfos.size(); i++) {
                const auto& alignmentInfo = edge.alignmentInfos[i];

                // Begin a new row of the table.
                s << "<tr>";

                // Read id and ordinals.
                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << alignmentInfo.orientedReadId.getReadId();
                s << "&amp;strand=" << alignmentInfo.orientedReadId.getStrand() << "\"";
                s << "><font color=\"blue\"><b><u>" << alignmentInfo.orientedReadId << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << alignmentInfo.orientedReadId.getReadId();
                s << "&amp;strand=" << alignmentInfo.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << alignmentInfo.ordinals[0];
                s << "&amp;highlightMarker=" << alignmentInfo.ordinals[1];
                s << "\"";
                s << "><font color=\"blue\"><b><u>" << alignmentInfo.ordinals[0] << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << alignmentInfo.orientedReadId.getReadId();
                s << "&amp;strand=" << alignmentInfo.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << alignmentInfo.ordinals[0];
                s << "&amp;highlightMarker=" << alignmentInfo.ordinals[1];
                s << "\"";
                s << "><font color=\"blue\"><b><u>" << alignmentInfo.ordinals[1] << "</u></b></font></td>";

                // SeqAn alignment.
                const seqan::Gaps< seqan::String<seqan::Dna> >& alignmentRow =
                    seqan::row(edge.seqanAlignment, i);
                s << "<td><b>" << alignmentRow << "</b></td>";

                // Repeat counts on SeqAn alignment.
                s << "<td><b>";
                const auto n = seqan::length(alignmentRow);
                size_t position = 0;
                for(size_t j=0; j<n; j++) {
                    if(seqan::isGap(alignmentRow, j)) {
                        s << "-";
                    } else {
                        const int repeatCount = edge.alignmentInfos[i].repeatCounts[position++];
                        if(repeatCount < 10) {
                            s << repeatCount;
                        } else {
                            s << "*";
                        }
                    }
                }
                s << "</b></td>";

                // End this row of the table.
                s << "</tr>";
            }

        }



        // End the label.
        s << "</table></font>> decorate=true";


        // End edge attributes.
        s << "]";
    }

}



#if 0
// OLD CODE WITH MORE COMPLICATED COLORING.
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

        // Id, so we can use JavaScript code to manipulate the vertex.
        s << "id=vertex" << vertex.vertexId;

        // Tooltip.
        s << " tooltip=\"Coverage " << coverage << ", distance " << vertex.distance;
        s << ", click to recenter graph here, right click for detail\"";

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
            color = "#90ee90";
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
            color = "#7cfc00";  // Bright green
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

        // Id, so we can use JavaScript code to manipulate the vertex.
        s << " id=vertex" << vertex.vertexId;

        // Tooltip.
        s << " tooltip=\"Coverage " << coverage << ", distance " << vertex.distance << "\"";

        // Write the label using Graphviz html-like functionality.
        s << " label=<<font><table border=\"0\">";

        // Kmer.
        s << "<tr><td colspan=\"3\"><b>";
        kmer.write(s, k);
        s << "</b></td></tr>";

        // Coverage.
        s << "<tr><td colspan=\"3\"><b>";
        s << "Coverage " << coverage;
        s << "</b></td></tr>";

        // Distance.
        s << "<tr><td colspan=\"3\" ";
        s << " href=\"\"";  // Necessary to activate tooltip.
        s << " id=\"vertexDistance" << vertex.vertexId << "\" tooltip=\"Click to recenter graph here\">";
        s << "<font color=\"blue\"><b><u>Distance " << vertex.distance;
        s << "</u></b></font></td></tr>";

        // Column headers.
        s << "<tr><td><b>Read</b></td><td><b>Ord</b></td><td><b>Pos</b></td></tr>";

        // A row for each marker of this vertex.
        for(const auto& markerInfo: vertex.markerInfos) {
            const CompressedMarker& marker = graph.markers.begin()[markerInfo.markerId];

            // OrientedReadId
            s << "<tr><td align=\"right\"";
            s << " href=\"exploreRead?readId&amp;" << markerInfo.orientedReadId.getReadId();
            s << "&amp;strand=" << markerInfo.orientedReadId.getStrand() << "\"";
            s << "><font color=\"blue\"><b><u>" << markerInfo.orientedReadId << "</u></b></font></td>";

            // Ordinal.
            s << "<td align=\"right\"";
            s << " href=\"exploreRead?readId=" << markerInfo.orientedReadId.getReadId();
            s << "&amp;strand=" << markerInfo.orientedReadId.getStrand();
            s << "&amp;highlightMarker=" << markerInfo.ordinal;
            s << "\"";
            s << "><font color=\"blue\"><b><u>" << markerInfo.ordinal << "</u></b></font></td>";

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

    const LocalMarkerGraph2Edge& edge = graph[e];
    const size_t coverage = edge.coverage();
    const size_t consensus = edge.consensus();

    if(!detailed) {

        // Compact output.

        // Begin edge attributes.
        s << "[";

        s << "tooltip=\"Consensus " << consensus << ", coverage " << coverage << "\"";

        // Color is determined by consensus.
        string color;
        if(consensus >= minCoverage) {
            color = "black";
        } else if(consensus == 1) {
            color = "#ff000080";  // Red, half way transparent
        } else if(consensus == 2) {
            color = "#ff800080";  // Orange, half way transparent
        } else {
            color = "#ff80ff80";  // Purple, half way transparent
        }
        s << " fillcolor=\"" << color << "\"";
        s << " color=\"" << color << "\"";

        // Thickness is determined by coverage.
        s << " penwidth=";
        const auto oldPrecision = s.precision(4);
        s <<  0.5 * double(coverage);
        s.precision(oldPrecision);

        // End edge attributes.
        s << "]";

    } else {

        // Detailed output.

        // Begin edge attributes.
        s << "[";

        // const string tooltipText = "Consensus " + to_string(consensus) + ", coverage " +to_string(coverage);
        s << " tooltip=\" \"";
        s << " labeltooltip=\" \"";
        // s << " URL=\"#abcdef\"";   // Hack to convince graphviz to not ignore the labeltooltip.

        // Thickness is determined by coverage.
        s << " penwidth=";
        const auto oldPrecision = s.precision(4);
        s <<  0.5 * double(coverage);
        s.precision(oldPrecision);

        // Color is determined by consensus.
        string color;
        string fillColor;
        if(consensus >= minConsensus) {
            color = "black";
            fillColor = "green";
        } else if(consensus == 1) {
            color = "red";
            fillColor = color;
        } else if(consensus == 2) {
            color = "#ff8000";  // Orange
            fillColor = color;
        } else {
            color = "#ff80ff";  // Purple
            fillColor = color;
        }
        s << " fillcolor=\"" << fillColor << "\"";
        s << " color=\"" << color << "\"";

        // Label.
        s << " label=<<font color=\"black\">";
        s << "<table";
        s << " color=\"black\"";
        s << " bgcolor=\"" << fillColor << "\"";
        s << " border=\"0\"";
        s << " cellborder=\"1\"";
        s << " cellspacing=\"0\"";
        s << ">";

        // Consensus and coverage.
        s << "<tr><td colspan=\"4\"><b>Consensus " << consensus << "</b></td></tr>";
        s << "<tr><td colspan=\"4\"><b>Coverage " << coverage << "</b></td></tr>";

        // Header row.
        s <<
            "<tr>"
            "<td align=\"center\"><b>Read</b></td>"
            "<td align=\"center\"><b>Ord0</b></td>"
            "<td align=\"center\"><b>Ord1</b></td>"
            "<td align=\"center\"><b>Seq</b></td>"
            "</tr>";

        // Loop over the infos table for this edge.
        for(const auto& p: edge.infos) {
            const auto& sequence = p.first;
            const auto& infos = p.second;

            // Construct the string representing this sequence.
            string sequenceString;
            if(sequence.sequence.empty()) {
                sequenceString = to_string(sequence.overlappingBaseCount);
            } else {
                for(const shasta::Base base: sequence.sequence) {
                    sequenceString.push_back(base.character());
                }
            }



            for(auto it=infos.begin(); it!=infos.end(); ++it) {
                const auto& info = *it;
                s << "<tr><td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand() << "\"";
                s << "><font color=\"blue\"><b><u>" << info.orientedReadId << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << info.ordinals[0];
                s << "&amp;highlightMarker=" << info.ordinals[1];
                s << "#" << info.ordinals[1] << "\"";
                s << "><font color=\"blue\"><b><u>" << info.ordinals[0] << "</u></b></font></td>";

                s << "<td align=\"right\"";
                s << " href=\"exploreRead?readId&amp;" << info.orientedReadId.getReadId();
                s << "&amp;strand=" << info.orientedReadId.getStrand();
                s << "&amp;highlightMarker=" << info.ordinals[0];
                s << "&amp;highlightMarker=" << info.ordinals[1];
                s << "\"";
                s << "><font color=\"blue\"><b><u>" << info.ordinals[1] << "</u></b></font></td>";

                s << "<td align=\"center\"><b>";
                if(it == infos.begin()) {
                    s << sequenceString;
                } else {
                    s << "=";
                }
                s << "</b></td></tr>";
            }
        }

        s << "</table></font>> decorate=true";


        // End edge attributes.
        s << "]";
    }

}
#endif

