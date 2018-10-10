#ifndef CZI_SHASTA_LOCAL_ALIGNMENT_GRAPH_HPP
#define CZI_SHASTA_LOCAL_ALIGNMENT_GRAPH_HPP



/*******************************************************************************

The local alignment graph is a local subgraph of the global alignment graph,
starting at a given oriented read and going out up to a specified distance.
It is an undirected graph in which each vertex corresponds to a read.
An edge is created between two vertices if the corresponding
oriented reads have a stored alignment.

The alignment graph is being phased out in favor of the read graph
(see AssemblerReadGraph.cpp).

*******************************************************************************/

// Shasta.
#include "Alignment.hpp"
#include "OrientedReadPair.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard libraries.
#include <map>

namespace ChanZuckerberg {
    namespace shasta {

        // Forward declaration of types declared in this file.
        class LocalAlignmentGraphVertex;
        class LocalAlignmentGraphEdge;
        class LocalAlignmentGraph;
        using LocalAlignmentGraphBaseClass = boost::adjacency_list<
            boost::setS,
            boost::listS,
            boost::undirectedS,
            LocalAlignmentGraphVertex,
            LocalAlignmentGraphEdge
            >;

    }
}


class ChanZuckerberg::shasta::LocalAlignmentGraphVertex {
public:

    // The OrientedReadId that this vertex corresponds to.
    // We store it as OrientedRead::Int so we can also use
    // it as a vertex id for graphviz output.
    OrientedReadId::Int orientedReadId;

    // The number of bases in this read.
    uint32_t baseCount;

    // The distance of this vertex from the starting vertex.
    uint32_t distance;

    LocalAlignmentGraphVertex(
        OrientedReadId orientedReadId,
        uint32_t baseCount,
        uint32_t distance) :
        orientedReadId(orientedReadId.getValue()),
        baseCount(baseCount),
        distance(distance)
        {}

};



class ChanZuckerberg::shasta::LocalAlignmentGraphEdge {
public:

    // Copies of the AlignmentInfo that caused this edge to be created.
    AlignmentInfo alignmentInfo;

    LocalAlignmentGraphEdge(
        const AlignmentInfo& alignmentInfo) :
        alignmentInfo(alignmentInfo)
        {}
};



class ChanZuckerberg::shasta::LocalAlignmentGraph :
    public LocalAlignmentGraphBaseClass {
public:

    void addVertex(
        OrientedReadId orientedReadId,
        uint32_t baseCount,
        uint32_t distance);

    void addEdge(
        OrientedReadId orientedReadId0,
        OrientedReadId orientedReadId1,
        const AlignmentInfo&);

    // Find out if a vertex with a given OrientedId exists.
    bool vertexExists(OrientedReadId) const;

    // Get the distance of an existing vertex from the start vertex.
    uint32_t getDistance(OrientedReadId) const;

    // Write in Graphviz format.
    void write(ostream&, uint32_t maxDistance) const;
    void write(const string& fileName, uint32_t maxDistance) const;

private:

    // Map that gives the vertex corresponding to an OrientedReadId.
    std::map<OrientedReadId, vertex_descriptor> vertexMap;

    // Graphviz writer.
    class Writer {
    public:
        Writer(const LocalAlignmentGraph&, uint32_t maxDistance);
        void operator()(ostream&) const;
        void operator()(ostream&, vertex_descriptor) const;
        void operator()(ostream&, edge_descriptor) const;
        const LocalAlignmentGraph& graph;
        uint32_t maxDistance;
    };
};



#endif
