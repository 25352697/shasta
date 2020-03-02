// Shasta.
#include "AssemblyPathGraph.hpp"
#include "deduplicate.hpp"
#include "html.hpp"
using namespace shasta;

// Boost libraries.
#include <boost/graph/graphviz.hpp>
#include <boost/graph/iteration_macros.hpp>

// Standard library.
#include "fstream.hpp"
#include <set>



AssemblyPathGraph::AssemblyPathGraph(const AssemblyGraph& assemblyGraph)
{
    AssemblyPathGraph& graph = *this;

    // Create a vertex for each assembly graph vertex.
    vector<vertex_descriptor> vertexDescriptors;
    for(AssemblyGraph::VertexId vertexId=0; vertexId<assemblyGraph.vertices.size(); vertexId++) {
        const vertex_descriptor v = add_vertex(AssemblyPathGraphVertex(vertexId), graph);
        vertexDescriptors.push_back(v);
    }

    // Fill in the reverse complemented vertex.
    for(AssemblyGraph::VertexId vertexId=0; vertexId<assemblyGraph.vertices.size(); vertexId++) {
        vertex_descriptor v = vertexDescriptors[vertexId];
        graph[v].reverseComplementVertex = vertexDescriptors[assemblyGraph.reverseComplementVertex[vertexId]];
    }

    // Sanity check.
    BGL_FORALL_VERTICES(v, graph, AssemblyPathGraph) {
        SHASTA_ASSERT(graph[graph[v].reverseComplementVertex].reverseComplementVertex == v);
    }



    // Create an edge for each assembly graph edge.
    vector<edge_descriptor> edgeDescriptors;
    for(AssemblyGraph::EdgeId edgeId=0; edgeId<assemblyGraph.edges.size(); edgeId++) {
        const AssemblyGraph::Edge& edge = assemblyGraph.edges[edgeId];
        const AssemblyGraph::VertexId vertexId0 = edge.source;
        const AssemblyGraph::VertexId vertexId1 = edge.target;
        const vertex_descriptor v0 = vertexDescriptors[vertexId0];
        const vertex_descriptor v1 = vertexDescriptors[vertexId1];
        edge_descriptor e;
        tie(e, ignore) = add_edge(v0, v1, AssemblyPathGraphEdge(edgeId), graph);
        edgeDescriptors.push_back(e);
    }

    // Fill in the reverse complemented edge.
    for(AssemblyGraph::EdgeId edgeId=0; edgeId<assemblyGraph.edges.size(); edgeId++) {
        edge_descriptor e = edgeDescriptors[edgeId];
        graph[e].reverseComplementEdge = edgeDescriptors[assemblyGraph.reverseComplementEdge[edgeId]];
    }

    // Sanity check.
    BGL_FORALL_EDGES(e, graph, AssemblyPathGraph) {
        SHASTA_ASSERT(graph[graph[e].reverseComplementEdge].reverseComplementEdge == e);
    }
}



void AssemblyPathGraph::writeGraphviz(const string& fileName) const
{
    ofstream file(fileName);
    writeGraphviz(file);
}



void AssemblyPathGraph::writeGraphviz(ostream& s) const
{
    const AssemblyPathGraph& graph = *this;

    s << "digraph G {\n";

    // Default attributes.
    s << "layout=sfdp;\n";
    s << "K=10;\n";
    s << "overlap=false;\n";
    s << "splines=true;\n";
    s << "smoothing=triangle;\n";
    s << "node [shape=point];\n";

    // This turns off the tooltip on the graph and the edges.
    s << "tooltip = \" \";\n";



    // Vertices.
    BGL_FORALL_VERTICES(v, graph, AssemblyPathGraph) {
        const AssemblyPathGraphVertex& vertex = graph[v];
        s << vertex.vertexId;

        s << " [";

        s << "tooltip=\"" << vertex.vertexId << "\"";

        s << "]";

        s << "\n";
    }



    // Edges. We write each edge as an additional pseudovertex.
    BGL_FORALL_EDGES(e, graph, AssemblyPathGraph) {
        const AssemblyPathGraphEdge& edge = graph[e];

        // Get the vertices.
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        const AssemblyPathGraphVertex& vertex0 = graph[v0];
        const AssemblyPathGraphVertex& vertex1 = graph[v1];


        // Write is as a pseudo vertex.
        const string pseudoVertexName =
            "\"" +
            to_string(vertex0.vertexId) +
            "to" +
            to_string(vertex1.vertexId) +
            "\"";
        s << pseudoVertexName << " [";
        s << "shape=rectangle label=\"" << edge << "\\n" << edge.pathLength << "";
        if(edge.tangle != invalidTangleId) {
            s << "\\n" << edge.tangle;
        }
        s << "\"";



        // Color.
        if(edge.tangle != invalidTangleId) {
            // Tangle edge.
            SHASTA_ASSERT(edge.inTangle == invalidTangleId);
            SHASTA_ASSERT(edge.outTangle == invalidTangleId);
            s << " style=filled fillcolor=pink";
        } else if(edge.inTangle != invalidTangleId and edge.outTangle != invalidTangleId) {
            // The edge is an in-edge of a tangle and an out-edge of another tangle.
            s << " style=filled fillcolor=purple";
        } else if(edge.inTangle != invalidTangleId) {
            // The edge has an in-tangle, so it is an out-edge of a tangle.
            s << " style=filled fillcolor=red";
        } else if(edge.outTangle != invalidTangleId) {
            // The edge has an out-tangle, so it is an in-edge of a tangle.
            s << " style=filled fillcolor=green";
        }
        s << "];\n";



        // Write the arrows to/from the pseudovertex.
        s << vertex0.vertexId << "->" << pseudoVertexName << ";\n";
        s << pseudoVertexName << "->" << vertex1.vertexId << ";\n";
    }





    s << "}\n";
}



// Initial creation of all tangles.
void AssemblyPathGraph::createTangles()
{
    AssemblyPathGraph& graph = *this;

    // Just in case, clean up.
    BGL_FORALL_EDGES(e, graph, AssemblyPathGraph) {
        graph[e].clearTangles();
    }
    tangles.clear();
    nextTangleId = 0;

    // Create the tangles.
    BGL_FORALL_EDGES(e, graph, AssemblyPathGraph) {
        createTangleAtEdge(e);
    }
    cout << "Found " << tangles.size() << " tangles." << endl;
}



// Create a new tangle that has the specified edge
// as the tangle edge, if such a tangle is valid
// and does not already exist.
// Return true if the new tangle was created.
bool AssemblyPathGraph::createTangleAtEdge(edge_descriptor e01)
{
    AssemblyPathGraph& graph = *this;

    if(graph[e01].tangle != invalidTangleId) {
        return false;
    }

    const vertex_descriptor v0 = source(e01, graph);
    const vertex_descriptor v1 = target(e01, graph);

    // If the in-degree and out-degree are not at least 2, this edge
    // does not generate a tangle.
    if(in_degree(v0, graph) <2) {
        return false;
    }
    if(out_degree(v1, graph) <2) {
        return false;
    }

    const auto inDegree = in_degree(v0, graph);
    const auto outDegree = out_degree(v1, graph);

    Tangle tangle;
    tangle.edge = e01;
    SHASTA_ASSERT(graph[e01].tangle == invalidTangleId);
    graph[e01].tangle = nextTangleId;

    // Gather the in-edges and out-edges.
    BGL_FORALL_INEDGES(v0, e, graph, AssemblyPathGraph) {
        tangle.inEdges.push_back(e);
        SHASTA_ASSERT(graph[e].outTangle == invalidTangleId);
        graph[e].outTangle = nextTangleId;
    }
    BGL_FORALL_OUTEDGES(v1, e, graph, AssemblyPathGraph) {
        tangle.outEdges.push_back(e);
        SHASTA_ASSERT(graph[e].inTangle == invalidTangleId);
        graph[e].inTangle = nextTangleId;
    }



    // Compute the tangle matrix, which contains the number of common oriented reads
    // for each pair of in-edges and out-edges.
    vector<OrientedReadId> commonOrientedReadIds;
    tangle.matrix.resize(inDegree, vector<uint64_t>(outDegree));
    for(uint64_t inEdgeIndex=0; inEdgeIndex<inDegree; inEdgeIndex++) {
        const AssemblyPathGraphEdge& inEdge = graph[tangle.inEdges[inEdgeIndex]];
        for(uint64_t outEdgeIndex=0; outEdgeIndex<outDegree; outEdgeIndex++) {
            const AssemblyPathGraphEdge& outEdge = graph[tangle.outEdges[outEdgeIndex]];
            commonOrientedReadIds.clear();
            std::set_intersection(
                inEdge.orientedReadIds.begin(), inEdge.orientedReadIds.end(),
                outEdge.orientedReadIds.begin(), outEdge.orientedReadIds.end(),
                back_inserter(commonOrientedReadIds));
            tangle.matrix[inEdgeIndex][outEdgeIndex] = commonOrientedReadIds.size();
        }
    }

    tangle.tangleId = nextTangleId;
    tangles.insert(make_pair(nextTangleId++, tangle));
    cout << "Created tangle " << tangle.tangleId << " at " << graph[e01] << endl;

    return true;
}



// Create tangles involving a given edge.
// This can create up to two tangles involving
// the given edge as an in-edge, out-edge, or tangle edge.
// This is used for incrementally create new tangles as
// edges are created during detangling.
void AssemblyPathGraph::createTanglesInvolvingEdge(edge_descriptor e)
{
    AssemblyPathGraph& graph = *this;
    const vertex_descriptor v0 = source(e, graph);
    const vertex_descriptor v1 = target(e, graph);

    createTangleAtEdge(e);

    BGL_FORALL_INEDGES(v0, e, graph, AssemblyPathGraph) {
        createTangleAtEdge(e);
    }
    BGL_FORALL_OUTEDGES(v1, e, graph, AssemblyPathGraph) {
        createTangleAtEdge(e);
    }
}



Tangle& AssemblyPathGraph::getTangle(TangleId tangleId)
{
    auto it = tangles.find(tangleId);
    SHASTA_ASSERT(it != tangles.end());
    Tangle& tangle = it->second;
    SHASTA_ASSERT(tangle.tangleId == tangleId);
    return tangle;
}

// Const version.
const Tangle& AssemblyPathGraph::getTangle(TangleId tangleId) const
{
    auto it = tangles.find(tangleId);
    SHASTA_ASSERT(it != tangles.end());
    const Tangle& tangle = it->second;
    SHASTA_ASSERT(tangle.tangleId == tangleId);
    return tangle;
}



TangleId AssemblyPathGraph::getReverseComplementTangle(
    TangleId tangleId) const
{
    const AssemblyPathGraph& graph = *this;

    // Get the edge of this tangle.
    const edge_descriptor e = getTangle(tangleId).edge;
    const AssemblyPathGraphEdge& edge = graph[e];

    // Get the reverse complement edge.
    const edge_descriptor eReverseComplement = edge.reverseComplementEdge;
    const AssemblyPathGraphEdge& reverseComplementEdge = graph[eReverseComplement];

    // Return its tangle.
    const TangleId reverseComplementTangleId = reverseComplementEdge.tangle;
    SHASTA_ASSERT(reverseComplementTangleId != invalidTangleId);
    return reverseComplementTangleId;
}



void AssemblyPathGraph::detangle()
{
    AssemblyPathGraph& graph = *this;

    // Detangle iteration.
    for(int iteration=0; ; ++iteration) {

        const TangleId tangleId = findNextTangle();
        if(tangleId == invalidTangleId) {
            break;
        }
        Tangle& tangle = getTangle(tangleId);
        const TangleId reverseComplementTangleId = getReverseComplementTangle(tangleId);
        cout << "Detangle iteration " << iteration <<
            " begins, working on tangle " << tangleId <<
            " and its reverse complement tangle " <<
            reverseComplementTangleId << endl;


        // If the tangle collides with its reverse complement,
        // mark it as unsolvable, for now. This is restriction is
        // not fundamental - it just requires more coding.
        if(collidesWithReverseComplement(tangleId)) {
            tangle.unsolvable = true;
            getTangle(reverseComplementTangleId).unsolvable = true;
            cout << "Reverse complement tangles " << tangleId <<
                " and " << reverseComplementTangleId <<
                " marked unsolvable because they collide." << endl;
            continue;
        }

        // Write the graph at the beginning of this iteration.
        graph.writeGraphviz("AssemblyPathGraph-" + to_string(iteration) + ".dot");
        graph.writeHtml("AssemblyPathGraph-" + to_string(iteration) + ".html");

        // Detangle this tangle and its reverse complement.
        vector<edge_descriptor> newEdges;
        if(detangle(tangleId, newEdges)) {
            SHASTA_ASSERT(detangle(reverseComplementTangleId, newEdges));
        } else {
            tangle.unsolvable = true;
            getTangle(reverseComplementTangleId).unsolvable = true;
            cout << "Reverse complement tangles " << tangleId <<
                " and " << reverseComplementTangleId <<
                " marked unsolvable because they could not be removed." << endl;
        }

        // Fill in the reverseComplementEdge for the edges we just created.
        for(const edge_descriptor e: newEdges) {
            fillReverseComplementEdge(e);
        }


        // Create tangles involving the newly created edges.
        for(const edge_descriptor e: newEdges) {
            createTanglesInvolvingEdge(e);
        }
    }

    graph.writeGraphviz("AssemblyPathGraph-Final.dot");
    graph.writeHtml("AssemblyPathGraph-Final.html");
}


void AssemblyPathGraph::fillReverseComplementEdge(edge_descriptor e)
{
    AssemblyPathGraph& graph = *this;

    const vertex_descriptor v0 = source(e, graph);
    const vertex_descriptor v1 = target(e, graph);

    const vertex_descriptor v0rc = graph[v0].reverseComplementVertex;
    const vertex_descriptor v1rc = graph[v1].reverseComplementVertex;

    edge_descriptor erc;
    bool exists;
    tie(erc, exists) =edge(v1rc, v0rc, graph);
    SHASTA_ASSERT(exists);
    graph[e].reverseComplementEdge = erc;
}



// Detangle a single tangle.
// Return true if successful.
// This does not fill in the reverseComplementEdge of newly created edges,
// and does not create new tangles involving those edges.
bool AssemblyPathGraph::detangle(
    TangleId tangleId,
    vector<edge_descriptor>& newEdges)
{
    cout << "Detangling tangle " << tangleId << endl;
    AssemblyPathGraph& graph = *this;

    // If the tangle matrix does not have any zeros, we cannot detangle.
    Tangle& tangle = getTangle(tangleId);
    if(not tangle.hasNonZeroMatrixElements()) {
        return false;
    }



    // Create the new edges.
    // We loop over all pairs of in-edges and out-edges that have read support
    // (that is, a non-zero element in the tangle matrix).
    const AssemblyPathGraphEdge& tangleEdge = graph[tangle.edge];
    for(uint64_t i=0; i<tangle.inEdges.size(); i++) {
        const edge_descriptor eIn = tangle.inEdges[i];
        const AssemblyPathGraphEdge& inEdge = graph[eIn];
        const vertex_descriptor vA = source(eIn, graph);

        for(uint64_t j=0; j<tangle.outEdges.size(); j++) {
            const edge_descriptor eOut = tangle.outEdges[j];
            const AssemblyPathGraphEdge& outEdge = graph[eOut];
            const vertex_descriptor vB = target(eOut, graph);

            if(tangle.matrix[i][j] == 0) {
                continue;
            }

            // Add the new edge and fill in what we can now.
            edge_descriptor eNew;
            tie(eNew, ignore) = add_edge(vA, vB, graph);
            newEdges.push_back(eNew);
            AssemblyPathGraphEdge& newEdge = graph[eNew];
            newEdge.pathLength =
                inEdge.pathLength + tangleEdge.pathLength + outEdge.pathLength;
            // Don't include the reads of the tangle edge in the new edge!
            newEdge.mergeOrientedReadIds(
                inEdge.orientedReadIds,
                outEdge.orientedReadIds
            );
            newEdge.path = inEdge.path;
            copy(tangleEdge.path.begin(), tangleEdge.path.end(),
                back_inserter(newEdge.path));
            copy(outEdge.path.begin(), outEdge.path.end(),
                back_inserter(newEdge.path));
        }
    }


    // Remove other tangles that the in-edges and out-edges
    // of this tangle are involved in.
    // Those will be recreated later, using the combined edges.
    vector<TangleId> tanglesToBeRemoved;
    for(const edge_descriptor e: tangle.inEdges) {
        const AssemblyPathGraphEdge& edge = graph[e];
        SHASTA_ASSERT(edge.outTangle == tangleId);
        SHASTA_ASSERT(edge.tangle == invalidTangleId);
        if(edge.inTangle != invalidTangleId) {
            tanglesToBeRemoved.push_back(edge.inTangle);
            cout << "Will remove preceding tangle " << edge.inTangle <<
                " due to tangle in-edge " << edge << endl;
        }
    }
    for(const edge_descriptor e: tangle.outEdges) {
        const AssemblyPathGraphEdge& edge = graph[e];
        SHASTA_ASSERT(edge.tangle == invalidTangleId);
        SHASTA_ASSERT(edge.inTangle == tangleId);
        if(edge.outTangle != invalidTangleId) {
            tanglesToBeRemoved.push_back(edge.outTangle);
            cout << "Will remove following tangle " << edge.outTangle <<
                " due to tangle out-edge " << edge << endl;
        }
    }
    deduplicate(tanglesToBeRemoved);
    for(const TangleId tangleId: tanglesToBeRemoved) {
        removeTangle(tangleId);
        cout << "Removed adjacent tangle " << tangleId << endl;
    }


    // Remove all the edges involved in the tangle we are detangling,
    // as well as the source and target vertices of the tangle edge.
    for(const edge_descriptor e: tangle.inEdges) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        boost::remove_edge(v0, v1, graph);
    }
    for(const edge_descriptor e: tangle.outEdges) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        boost::remove_edge(v0, v1, graph);
    }
    const vertex_descriptor v0 = source(tangle.edge, graph);
    const vertex_descriptor v1 = target(tangle.edge, graph);
    boost::remove_edge(v0, v1, graph);
    SHASTA_ASSERT(in_degree(v0, graph) == 0);
    SHASTA_ASSERT(out_degree(v0, graph) == 0);
    SHASTA_ASSERT(in_degree(v1, graph) == 0);
    SHASTA_ASSERT(out_degree(v1, graph) == 0);
    remove_vertex(v0, graph);
    remove_vertex(v1, graph);

    // Finally we can remove this tangle.
    tangles.erase(tangleId);

    return true;
}


void AssemblyPathGraph::removeTangle(TangleId tangleId)
{
    AssemblyPathGraph& graph = *this;
    const Tangle& tangle = getTangle(tangleId);

    // Remove all references to this tangle.
    graph[tangle.edge].tangle = invalidTangleId;

    for(const edge_descriptor e: tangle.inEdges) {
        AssemblyPathGraphEdge& edge = graph[e];
        SHASTA_ASSERT(edge.tangle == invalidTangleId);
        SHASTA_ASSERT(edge.outTangle == tangleId);
        edge.outTangle = invalidTangleId;
    }

    for(const edge_descriptor e: tangle.outEdges) {
        AssemblyPathGraphEdge& edge = graph[e];
        SHASTA_ASSERT(edge.tangle == invalidTangleId);
        SHASTA_ASSERT(edge.inTangle == tangleId);
        edge.inTangle = invalidTangleId;
    }

    // Now we can remove the tangle.
    tangles.erase(tangleId);
}




void AssemblyPathGraphEdge::mergeOrientedReadIds(
    const vector<OrientedReadId>& in,
    const vector<OrientedReadId>& out
    )
{
    orientedReadIds.clear();
    std::set_union(
        in.begin(), in.end(),
        out.begin(), out.end(),
        back_inserter(orientedReadIds));
}



bool Tangle::hasNonZeroMatrixElements() const
{
    for(const auto& v: matrix) {
        for(const auto x: v) {
            if(x == 0) {
                return true;
            }
        }
    }

    // If getting here, we did not find any non-zero matrix elements.
    return false;
}



// Return the next tangle to work on.
// This does a linear search, which coudl be eliminated
// with appropriated data structures if it becomes a
// performance problem.
// It currently returns the tangle with the shortest path
// on the tangle edge.
TangleId AssemblyPathGraph::findNextTangle() const
{
    const AssemblyPathGraph& graph = *this;

    TangleId bestTangleId = invalidTangleId;
    uint64_t bestTanglePathLength = std::numeric_limits<uint64_t>::max();
    for(const auto& p: tangles) {
        const Tangle& tangle = p.second;
        if(tangle.unsolvable) {
            continue;   // We gave up on this one.
        }
        const uint64_t pathLength = graph[tangle.edge].pathLength;
        if(pathLength < bestTanglePathLength) {
            bestTanglePathLength = pathLength;
            bestTangleId = tangle.tangleId;
        }
    }
    return bestTangleId;
}



// Return true if a tangle collides with its reverse complement.
bool AssemblyPathGraph::collidesWithReverseComplement(TangleId tangleId) const
{
    const AssemblyPathGraph& graph = *this;
    const Tangle& tangle = getTangle(tangleId);
    const TangleId reverseComplementTangleId = getReverseComplementTangle(tangleId);

    // If the tangle is the same as its reverse complement, we have a collision.
    // This is unusual but possible.
    if(reverseComplementTangleId == tangleId) {
        return true;
    }

    // Check the in-edges.
    for(const edge_descriptor e: tangle.inEdges) {
        if(graph[e].inTangle == reverseComplementTangleId) {
            return true;
        }
    }

    // Check the out-edges.
    for(const edge_descriptor e: tangle.outEdges) {
        if(graph[e].outTangle == reverseComplementTangleId) {
            return true;
        }
    }

    // If getting here, we did not find a collision between this tangle
    // and its reverse complement.
    return false;
}



void AssemblyPathGraph::writeHtml(const string& fileName) const
{
    ofstream html(fileName);
    writeHtml(html);
}



void AssemblyPathGraph::writeHtml(ostream& html) const
{
    writeHtmlBegin(html, "Assembly path graph");
    html << "<body>"
        "<h1>Assembly path graph</h1>";
    writeVerticesHtml(html);
    writeEdgesHtml(html);
    writeTanglesHtml(html);
    html << "</body>";
}



void AssemblyPathGraph::writeVerticesHtml(ostream& html) const
{
    const AssemblyPathGraph& graph = *this;

    html << "<h2>Vertices</h2>"
        "<p>Each vertex corresponds to a vertex of the assembly graph."
        "<p><table><tr>"
        "<th>Id"
        "<th>Id of<br>reverse<br>complement<br>vertex";

    BGL_FORALL_VERTICES(v, graph, AssemblyPathGraph) {
        const AssemblyPathGraphVertex& vertex = graph[v];
        const vertex_descriptor vReverseComplement = vertex.reverseComplementVertex;
        html <<
            "<tr id=v" << vertex.vertexId << ">" <<
            "<td class=centered>" << vertex.vertexId <<
            "<td class=centered>" << graph[vReverseComplement].vertexId;
    }

    html << "</table>";
}



void AssemblyPathGraph::writeEdgesHtml(ostream& html) const
{
    const AssemblyPathGraph& graph = *this;

    html << "<h2>Edges</h2>"
        "<p>Each edge corresponds to a path in the assembly graph vertex."
        "<p><table><tr>"
        "<th>Path"
        "<th>Path of<br>reverse<br>complement<br>edge"
        "<th>Source<br>vertex"
        "<th>Target<br>vertex"
        "<th>Path<br>length<br>(markers)"
        "<th>In-tangle"
        "<th>Tangle"
        "<th>Out-tangle";



    BGL_FORALL_EDGES(e, graph, AssemblyPathGraph) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        const AssemblyGraph::VertexId vertexId0 = graph[v0].vertexId;
        const AssemblyGraph::VertexId vertexId1 = graph[v1].vertexId;
        const AssemblyPathGraphEdge& edge = graph[e];
        const edge_descriptor eReverseComplement = edge.reverseComplementEdge;
        html <<
            "<tr id='e" << edge << "'>"
            "<td class=centered>" << edge <<
            "<td class=centered>" << graph[eReverseComplement] <<
            "<td class=centered><a href='#v" << vertexId0 << "'>" << vertexId0 << "</a>" <<
            "<td class=centered><a href='#v" << vertexId1 << "'>" << vertexId1 << "</a>" <<
            "<td class=centered>" << edge.pathLength;

        html <<  "<td class=centered>";
        if(edge.inTangle != invalidTangleId) {
            html << "<a href='#t" << edge.inTangle << "'>" << edge.inTangle << "</a>";
        }

        html <<  "<td class=centered>";
        if(edge.tangle != invalidTangleId) {
            html << "<a href='#t" << edge.tangle << "'>" << edge.tangle << "</a>";
        }

        html <<  "<td class=centered>";
        if(edge.outTangle != invalidTangleId) {
            html << "<a href='#t" << edge.outTangle << "'>" << edge.outTangle << "</a>";
        }
    }

    html << "</table>";

}



void AssemblyPathGraph::writeTanglesHtml(ostream& html) const
{
    const AssemblyPathGraph& graph = *this;

    html << "<h2>Tangle</h2>"
        "A tangle is generated by each edge v<sub>0</sub>&rarr;v<sub>1</sub> "
        "for which the source vertex v<sub>0</sub> has in-degree greater than 1 "
        "and the target vertex v<sub>1</sub> has out-degree "
        "greater than 1."
        "<p><table><tr>"
        "<th>Id"
        "<th>In-edges"
        "<th>Tangle<br>edge"
        "<th>Out-edges"
        "<th>Tangle<br>matrix";

    for(const auto& p: tangles) {
        const Tangle& tangle = p.second;
        html <<
            "<tr id=t" << tangle.tangleId << ">"
            "<td class=centered>" << tangle.tangleId;

        // In-edges.
        html << "<td class=centered>";
        for(const edge_descriptor e: tangle.inEdges) {
            html << "<a href='#e" << graph[e] << "'>" <<
                graph[e] << "</a>" << " ";
        }

        // Tangle edge.
        html << "<td class=centered><a href='#e" << graph[tangle.edge] << "'>" <<
            graph[tangle.edge] << "</a>";

        // Out-edges.
        html << "<td class=centered>";
        for(const edge_descriptor e: tangle.outEdges) {
            html << "<a href='#e" << graph[e] << "'>" <<
                graph[e] << "</a>" << " ";
        }



        // Tangle matrix.
        html << "<td class=centered>";
        html << "<table style='margin-left:auto;margin-right:auto;'>";
        html << "<tr><td class=centered>";
        for(uint64_t j=0; j<tangle.outEdges.size(); j++) {
            const edge_descriptor e = tangle.outEdges[j];
            html << "<td class=centered>" << graph[e];
        }
        for(uint64_t i=0; i<tangle.inEdges.size(); i++) {
            const edge_descriptor e = tangle.inEdges[i];
            html << "<tr><td class=centered>" << graph[e];
            for(uint64_t j=0; j<tangle.outEdges.size(); j++) {
                const uint64_t value = tangle.matrix[i][j];
                html << "<td class=centered>";
                if(value) {
                    html << value;
                }
            }
        }
        html << "</table>";




        if(tangle.unsolvable) {
            html << "<td class=centered>Unsolvable";
        }

    }


    html << "</table>";

}
