// Shasta.
#include "Assembler.hpp"
#include "LocalAssemblyGraph.hpp"
using namespace ChanZuckerberg;
using namespace shasta;

// Boost libraries.
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

// Standard library.
#include "chrono.hpp"



void Assembler::exploreAssemblyGraph(
    const vector<string>& request,
    ostream& html)
{
    // Get the request parameters.
    LocalAssemblyGraphRequestParameters requestParameters;
    getLocalAssemblyGraphRequestParameters(request, requestParameters);

    // Write the form.
    requestParameters.writeForm(html, assemblyGraph.vertices.size());

    // If any required values are missing, stop here.
    if(requestParameters.hasMissingRequiredParameters()) {
        return;
    }

    // Validity check.
    if(requestParameters.vertexId > assemblyGraph.vertices.size()) {
        html << "<p>Invalid vertex id " << requestParameters.vertexId;
        html << ". Must be between 0 and " << assemblyGraph.vertices.size()-1 << " inclusive.";
        return;
    }



    // Create the local assembly graph.
    html << "<h1>Local assembly graph</h1>";
    LocalAssemblyGraph graph(assemblyGraph);
    const auto createStartTime = steady_clock::now();
    if(!extractLocalAssemblyGraph(
        requestParameters.vertexId,
        requestParameters.maxDistance,
        requestParameters.timeout,
        graph)) {
        html << "<p>Timeout for graph creation exceeded. Increase the timeout or reduce the maximum distance from the start vertex.";
        return;
    }
    html << "<p>The local assembly graph has " << num_vertices(graph);
    html << " vertices and " << num_edges(graph) << " edges.";

    const auto createFinishTime = steady_clock::now();
    if(seconds(createFinishTime - createStartTime) > requestParameters.timeout) {
        html << "<p>Timeout for graph creation exceeded. Increase the timeout or reduce the maximum distance from the start vertex.";
        return;
    }

    // Write it out in graphviz format.
    const string uuid = to_string(boost::uuids::random_generator()());
    const string dotFileName = "/dev/shm/" + uuid + ".dot";
    graph.write(
        dotFileName,
        requestParameters.maxDistance,
        requestParameters.detailed);



    // Compute graph layout in svg format.
    const string command =
        "timeout " + to_string(requestParameters.timeout - seconds(createFinishTime - createStartTime)) +
        " dot -O -T svg " + dotFileName +
        " -Gsize=" + to_string(requestParameters.sizePixels/72.);
    const int commandStatus = ::system(command.c_str());
    if(WIFEXITED(commandStatus)) {
        const int exitStatus = WEXITSTATUS(commandStatus);
        if(exitStatus == 124) {
            html << "<p>Timeout for graph layout exceeded. Increase the timeout or reduce the maximum distance from the start vertex.";
            filesystem::remove(dotFileName);
            return;
        }
        else if(exitStatus!=0 && exitStatus!=1) {    // sfdp returns 1 all the time just because of the message about missing triangulation.
            filesystem::remove(dotFileName);
            throw runtime_error("Error " + to_string(exitStatus) + " running graph layout command: " + command);
        }
    } else if(WIFSIGNALED(commandStatus)) {
        const int signalNumber = WTERMSIG(commandStatus);
        throw runtime_error("Signal " + to_string(signalNumber) + " while running graph layout command: " + command);
    } else {
        throw runtime_error("Abnormal status " + to_string(commandStatus) + " while running graph layout command: " + command);

    }

    // Remove the .dot file.
    // filesystem::remove(dotFileName);

    // Copy the svg to html.
    const string svgFileName = dotFileName + ".svg";
    ifstream svgFile(svgFileName);
    html << svgFile.rdbuf();
    svgFile.close();

    // Remove the .svg file.
    filesystem::remove(svgFileName);

}



// Extract  from the request the parameters for the display
// of the local assembly graph.
void Assembler::getLocalAssemblyGraphRequestParameters(
    const vector<string>& request,
    LocalAssemblyGraphRequestParameters& parameters) const
{
    parameters.vertexId = 0;
    parameters.vertexIdIsPresent = getParameterValue(
        request, "vertexId", parameters.vertexId);

    parameters.maxDistance = 0;
    parameters.maxDistanceIsPresent = getParameterValue(
        request, "maxDistance", parameters.maxDistance);

    string detailedString;
    parameters.detailed = getParameterValue(
        request, "detailed", detailedString);

    parameters.sizePixels = 800;
    parameters.sizePixelsIsPresent = getParameterValue(
        request, "sizePixels", parameters.sizePixels);

    parameters.timeout = 30;
    parameters.timeoutIsPresent = getParameterValue(
        request, "timeout", parameters.timeout);

}



void Assembler::LocalAssemblyGraphRequestParameters::writeForm(
    ostream& html,
    AssemblyGraph::VertexId vertexCount) const
{
    html <<
        "<h3>Display a local subgraph of the global assembly graph</h3>"
        "<form>"

        "<table>"

        "<tr title='Vertex id between 0 and " << vertexCount << "'>"
        "<td>Vertex id"
        "<td><input type=text required name=vertexId size=8 style='text-align:center'"
        << (vertexIdIsPresent ? ("value='"+to_string(vertexId)+"'") : "") <<
        ">"

        "<tr title='Maximum distance from start vertex (number of edges)'>"
        "<td>Maximum distance"
        "<td><input type=text required name=maxDistance size=8 style='text-align:center'"
        << (maxDistanceIsPresent ? ("value='" + to_string(maxDistance)+"'") : " value='6'") <<
        ">"

        "<tr title='Check for detailed graph with labels'>"
        "<td>Detailed"
        "<td class=centered><input type=checkbox name=detailed"
        << (detailed ? " checked=checked" : "") <<
        ">"


        "<tr title='Graphics size in pixels. "
        "Changing this works better than zooming. Make it larger if the graph is too crowded."
        " Ok to make it much larger than screen size.'>"
        "<td>Graphics size in pixels"
        "<td><input type=text required name=sizePixels size=8 style='text-align:center'"
        << (sizePixelsIsPresent ? (" value='" + to_string(sizePixels)+"'") : " value='1600'") <<
        ">"

        "<tr>"
        "<td>Timeout (seconds) for graph creation and layout"
        "<td><input type=text required name=timeout size=8 style='text-align:center'"
        << (timeoutIsPresent ? (" value='" + to_string(timeout)+"'") : " value='30'") <<
        ">"
        "</table>"

        "<br><input type=submit value='Display'>"
        "</form>";
}



bool Assembler::LocalAssemblyGraphRequestParameters::hasMissingRequiredParameters() const
{
    return
        !vertexIdIsPresent ||
        !maxDistanceIsPresent ||
        !timeoutIsPresent;
}



void Assembler::exploreAssemblyGraphVertex(const vector<string>& request, ostream& html)
{
    html << "<h2>Show details about a vertex of the assembly graph</h2>";

    // Get the request parameters.
    AssemblyGraph::VertexId vertexId = 0;
    const bool vertexIdIsPresent = getParameterValue(
        request, "vertexId", vertexId);
    string showDetailsString;
    getParameterValue(request, "showDetails", showDetailsString);
    const bool showDetails = (showDetailsString == "on");
    cout << "showDetailsString " << showDetailsString << " " << int(showDetails) << endl;

    // Write the form to get the vertex id.
    html <<
        "<form>"
        "<br>Assembly graph vertex id: <input type=text name=vertexId" <<
        (vertexIdIsPresent ? (" value='" + to_string(vertexId)) + "'" : "") <<
        " title='Enter an assembly graph vertex id between 0 and " << assemblyGraph.vertices.size()-1 << " inclusive'"
        "><br>Show assembly details <input type=checkbox name=showDetails" <<
        (showDetails ? " checked=checked" : "") <<
        ">(this can be slow)<br><input type=submit value='Go'>"
        "</form>";

    // If the vertex id is missing or invalid, don't do anything.
    if(!vertexIdIsPresent) {
        return;
    }
    if(vertexId >= assemblyGraph.vertices.size()) {
        html <<
            "<p>Invalid vertex id " << vertexId <<
            ". Enter an assembly graph vertex id between 0 and " <<
            assemblyGraph.vertices.size()-1 << " inclusive." << endl;
        return;
    }



    // Assemble the sequence and output detailed information to html.
    if(showDetails) {
        vector<Base> sequence;
        vector<uint32_t> repeatCounts;
        assembleAssemblyGraphVertex(vertexId, sequence, repeatCounts, &html);
    } else {

        // Assembly details were not requested but a global assembly
        // is not available.
        if( !assemblyGraph.sequences.isOpen() ||
            !assemblyGraph.repeatCounts.isOpen()) {
            html << "<p>A global assembly is not available. You can check "
                "the showDetails checkbox to have the sequence computed on the fly. "
                "This could be slow for long sequence.";
            return;
        } else {

            // Assembly details were not requested and a global assembly
            // is available. Get the sequence from the global assembly.

            html << "<p>Add here the code to write assembled sequence.";
            // Write a title.
            html <<
                "<h1>Assembly graph vertex <a href="
                "'exploreAssemblyGraph?vertexId=" << vertexId <<
                "&maxDistance=6&detailed=on&sizePixels=1600&timeout=30'>" <<
                vertexId << "</a></h1>";

            // Write parent and child vertices in the assembly graph.
            html << "<p>Parent vertices in the assembly graph:";
            for(const auto parentEdge: assemblyGraph.edgesByTarget[vertexId]) {
                const AssemblyGraph::VertexId parent = assemblyGraph.edges[parentEdge].source;
                html <<
                    " <a href='exploreAssemblyGraphVertex?vertexId=" << parent << "'>"
                    << parent << "</a>";
            }

            html << "<p>Child vertices in the assembly graph:";
            for(const auto childEdge: assemblyGraph.edgesBySource[vertexId]) {
                const AssemblyGraph::VertexId child = assemblyGraph.edges[childEdge].target;
                html <<
                    " <a href='exploreAssemblyGraphVertex?vertexId=" << child << "'>"
                    << child << "</a>";
            }

            // Assembled run-length sequence.
            const LongBaseSequenceView runLengthSequence = assemblyGraph.sequences[vertexId];
            const MemoryAsContainer<uint8_t> repeatCounts = assemblyGraph.repeatCounts[vertexId];
            CZI_ASSERT(repeatCounts.size() == runLengthSequence.baseCount);
            html << "<p>Assembled run-length sequence (" << runLengthSequence.baseCount <<
                " bases):<br><span style='font-family:courier'>";
            html << runLengthSequence;
            html << "<br>";
            size_t rawSequenceSize = 0;
            for(size_t j=0; j<repeatCounts.size(); j++) {
                const uint32_t repeatCount = repeatCounts[j];
                rawSequenceSize += repeatCount;
                CZI_ASSERT(repeatCount < 10);  // Fix when it fails.
                html << repeatCount%10;
            }
            html << "</span>";

            // Assembled raw sequence.
            html << "<p>Assembled raw sequence (" << rawSequenceSize <<
                " bases):<br><span style='font-family:courier'>";
            for(size_t i=0; i<runLengthSequence.baseCount; i++) {
                const Base base = runLengthSequence[i];
                const uint8_t repeatCount = repeatCounts[i];
                for(uint8_t k=0; k<repeatCount; k++) {
                    html << base;
                }
            }
            html << "</span>";
        }
    }

}
