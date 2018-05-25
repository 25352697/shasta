#ifndef CZI_NANOPORE2_LOCAL_READ_GRAPH_HPP
#define CZI_NANOPORE2_LOCAL_READ_GRAPH_HPP


/*******************************************************************************

The local read graph is a local subgraph of the global read graph,
starting at a given oriented read and going out up to a specified distance.
It is an undirected graph in which each vertex corresponds to a read.
An edge is created between two vertices if the corresponding
oriented reads have sufficiently good overlap/alignment.

*******************************************************************************/

// Nanoppore2.
#include "Alignment.hpp"
#include "Overlap.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard libraries.
#include <map>

namespace ChanZuckerberg {
    namespace Nanopore2 {

        // Forward declaration of types declared in this file.
        class LocalReadGraphVertex;
        class LocalReadGraphEdge;
        class LocalReadGraph;
        using LocalReadGraphBaseClass = boost::adjacency_list<
            boost::setS,
            boost::listS,
            boost::undirectedS,
            LocalReadGraphVertex,
            LocalReadGraphEdge
            >;

    }
}


class ChanZuckerberg::Nanopore2::LocalReadGraphVertex {
public:

    // The OrientedReadId that this vertex corresponds to.
    // We store it as OrientedRead::Int so we can also use
    // it as a vertex id for graphviz output.
    OrientedReadId::Int orientedReadId;

    // The distance of this vertex from the starting vertex.
    size_t distance;

    LocalReadGraphVertex(
        OrientedReadId orientedReadId,
        size_t distance) :
        orientedReadId(orientedReadId.getValue()),
        distance(distance)
        {}

};



class ChanZuckerberg::Nanopore2::LocalReadGraphEdge {
public:

    // Copies of the Overlap and AlignmentInfo
    // that caused this edge to be created.
    Overlap overlap;
    AlignmentInfo alignmentInfo;

    LocalReadGraphEdge(
        const Overlap& overlap,
        const AlignmentInfo& alignmentInfo) :
        overlap(overlap),
        alignmentInfo(alignmentInfo)
        {}
};



class ChanZuckerberg::Nanopore2::LocalReadGraph :
    public LocalReadGraphBaseClass {
public:

    void addVertex(
        OrientedReadId orientedReadId,
        size_t distance);

    void addEdge(
        OrientedReadId orientedReadId0,
        OrientedReadId orientedReadId1,
        const Overlap&,
        const AlignmentInfo&);

    // Find out if a vertex with a given OrientedId exists.
    bool vertexExists(OrientedReadId) const;

    // Get the distance of an existing vertex from the start vertex.
    size_t getDistance(OrientedReadId) const;

    // Write in Graphviz format.
    void write(ostream&) const;
    void write(const string& fileName) const;

private:

    // Map that gives the vertex corresponding to an OrientedReadId.
    std::map<OrientedReadId, vertex_descriptor> vertexMap;

    // Graphviz writer.
    class Writer {
    public:
        Writer(const LocalReadGraph&);
        void operator()(ostream&) const;
        void operator()(ostream&, vertex_descriptor) const;
        void operator()(ostream&, edge_descriptor) const;
        const LocalReadGraph& graph;
    };
};



#endif
