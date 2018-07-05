// shasta.
#include "LocalReadGraph.hpp"
using namespace ChanZuckerberg;
using namespace shasta;

// Boost libraries.
#include <boost/graph/graphviz.hpp>

// Standard library.
#include "fstream.hpp"
#include "stdexcept.hpp"



void LocalReadGraph::addVertex(
    OrientedReadId orientedReadId,
    uint32_t baseCount,
    uint32_t distance)
{
    // Check that we don't altready have a vertex with this OrientedReadId.
    CZI_ASSERT(vertexMap.find(orientedReadId) == vertexMap.end());

    // Create the vertex.
    const vertex_descriptor v = add_vertex(LocalReadGraphVertex(orientedReadId, baseCount, distance), *this);

    // Store it in the vertex map.
    vertexMap.insert(make_pair(orientedReadId, v));
}



void LocalReadGraph::addEdge(
    OrientedReadId orientedReadId0,
    OrientedReadId orientedReadId1,
    const AlignmentInfo& alignmentInfo)
{
    // Find the vertices corresponding to these two OrientedReadId.
    const auto it0 = vertexMap.find(orientedReadId0);
    CZI_ASSERT(it0 != vertexMap.end());
    const vertex_descriptor v0 = it0->second;
    const auto it1 = vertexMap.find(orientedReadId1);
    CZI_ASSERT(it1 != vertexMap.end());
    const vertex_descriptor v1 = it1->second;

    // Add the edge.
    add_edge(v0, v1, LocalReadGraphEdge(alignmentInfo), *this);
}



uint32_t LocalReadGraph::getDistance(OrientedReadId orientedReadId) const
{
    const auto it = vertexMap.find(orientedReadId);
    CZI_ASSERT(it != vertexMap.end());
    const vertex_descriptor v = it->second;
    return (*this)[v].distance;
}



bool LocalReadGraph::vertexExists(OrientedReadId orientedReadId) const
{
   return vertexMap.find(orientedReadId) != vertexMap.end();
}



// Write the graph in Graphviz format.
void LocalReadGraph::write(const string& fileName, uint32_t maxDistance) const
{
    ofstream outputFileStream(fileName);
    if(!outputFileStream) {
        throw runtime_error("Error opening " + fileName);
    }
    write(outputFileStream, maxDistance);
}
void LocalReadGraph::write(ostream& s, uint32_t maxDistance) const
{
    Writer writer(*this, maxDistance);
    boost::write_graphviz(s, *this, writer, writer, writer,
        boost::get(&LocalReadGraphVertex::orientedReadId, *this));
}

LocalReadGraph::Writer::Writer(
    const LocalReadGraph& graph,
    uint32_t maxDistance) :
    graph(graph),
    maxDistance(maxDistance)
{
}



void LocalReadGraph::Writer::operator()(std::ostream& s) const
{
    s << "layout=sfdp;\n";
    s << "ratio=expand;\n";
    s << "node [shape=point];\n";
    s << "edge [penwidth=\"0.2\"];\n";

    // This turns off the tooltip on the graph.
    s << "tooltip = \" \";\n";
}


void LocalReadGraph::Writer::operator()(std::ostream& s, vertex_descriptor v) const
{
    const LocalReadGraphVertex& vertex = graph[v];
    const OrientedReadId orientedReadId(vertex.orientedReadId);

    s << "[";
    s << " tooltip=\"" << orientedReadId << " length " << vertex.baseCount << " distance " << vertex.distance << "\"";
    s << " URL=\"exploreRead?readId=" << orientedReadId.getReadId();
    s << "&strand=" << orientedReadId.getStrand() << "\"";
    s << " width=" << sqrt(1.e-6 * vertex.baseCount);
    if(vertex.distance == 0) {
        s << " color=lightGreen fillcolor=lightGreen";
    } else if(vertex.distance == maxDistance) {
            s << " color=cyan fillcolor=cyan";
    }
    s << "]";
}



void LocalReadGraph::Writer::operator()(std::ostream& s, edge_descriptor e) const
{
    const LocalReadGraphEdge& edge = graph[e];
    const vertex_descriptor v0 = source(e, graph);
    const vertex_descriptor v1 = target(e, graph);
    const LocalReadGraphVertex& vertex0 = graph[v0];
    const LocalReadGraphVertex& vertex1 = graph[v1];

    s << "[";
    s << "tooltip=\"" << OrientedReadId(vertex0.orientedReadId) << " ";
    s << OrientedReadId(vertex1.orientedReadId) << " ";
    s << edge.alignmentInfo.markerCount << "\"";
    s << "]";
}

