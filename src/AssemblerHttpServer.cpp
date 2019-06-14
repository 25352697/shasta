#ifndef SHASTA_STATIC_EXECUTABLE

// Boost gil library.
// The boost gil library includes png.h,
// then uses int_p_NULL which is not defined in
// all versions of boost (see Boost bug 3908,
// flaged as fixed but it is not obvious that that
// is the case). To deal with this, we defensively
// include png.h, then define int_p_NULL if necessary.
#include <png.h>
#ifndef int_p_NULL
#define int_p_NULL (int *)NULL
#endif
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/png_dynamic_io.hpp>


// Shasta.
#include "Assembler.hpp"
#include "AlignmentGraph.hpp"
#include "LocalAlignmentGraph.hpp"
#include "LocalReadGraph.hpp"
#include "timestamp.hpp"
using namespace ChanZuckerberg;
using namespace shasta;

// Boost libraries.
#include <boost/tokenizer.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

// Seqan
#include <seqan/align.h>



// Standard library.
#include "chrono.hpp"
#include <iomanip>
#include "iterator.hpp"



#define CZI_ADD_TO_FUNCTION_TABLE(name) httpServerData.functionTable[string("/") + #name ] = &Assembler::name



// Associate http keywords wth member functions.
void Assembler::fillServerFunctionTable()
{
    httpServerData.functionTable[""]        = &Assembler::exploreSummary;
    httpServerData.functionTable["/"]       = &Assembler::exploreSummary;
    httpServerData.functionTable["/index"]  = &Assembler::exploreSummary;

    CZI_ADD_TO_FUNCTION_TABLE(exploreSummary);
    CZI_ADD_TO_FUNCTION_TABLE(exploreRead);
    CZI_ADD_TO_FUNCTION_TABLE(blastRead);
    CZI_ADD_TO_FUNCTION_TABLE(exploreAlignments);
    CZI_ADD_TO_FUNCTION_TABLE(exploreAlignment);
    CZI_ADD_TO_FUNCTION_TABLE(computeAllAlignments);
    CZI_ADD_TO_FUNCTION_TABLE(exploreAlignmentGraph);
    CZI_ADD_TO_FUNCTION_TABLE(displayAlignmentMatrix);
    CZI_ADD_TO_FUNCTION_TABLE(exploreReadGraph);
    CZI_ADD_TO_FUNCTION_TABLE(exploreMarkerGraph);
    CZI_ADD_TO_FUNCTION_TABLE(exploreMarkerGraphVertex);
    CZI_ADD_TO_FUNCTION_TABLE(exploreMarkerGraphEdge);
    CZI_ADD_TO_FUNCTION_TABLE(exploreAssemblyGraph);
    CZI_ADD_TO_FUNCTION_TABLE(exploreAssemblyGraphEdge);

}
#undef CZI_ADD_TO_FUNCTION_TABLE

void Assembler::setDocsDirectory(const string& docsDirectoryArgument)
{
    httpServerData.docsDirectory = docsDirectoryArgument;
}

// Call this before explore to specify the name of the fasta
// file containing the reference to be used with Blast commands.
void Assembler::setReferenceFastaFileName(const string& referenceFastaFileName)
{
    httpServerData.referenceFastaFileName = referenceFastaFileName;
}


void Assembler::processRequest(
    const vector<string>& request,
    ostream& html,
    const BrowserInformation&)
{
    // Process a documentation request.
    const string& keyword = request.front();
    if(keyword.size()>6 && keyword.substr(0, 6)=="/docs/") {

        // Extract the file name.
        const string name = keyword.substr(6);

        // If it contains "/", reject it.
        if(name.find('/') != string::npos) {
            writeHtmlBegin(html);
            html << "Unknown documentation file " << name;
            writeHtmlEnd(html);
            return;
        }

        // Construct the full file name and open it.
        const string fileName = httpServerData.docsDirectory + "/" + name;
        ifstream file(fileName);
        if(!file) {
            writeHtmlBegin(html);
            html << "Could not open " << fileName;
            writeHtmlEnd(html);
        }

        // Send it to html.
        html << "\r\n" << file.rdbuf();
        return;
    }



    // Look up the keyword to find the function that will process this request.
    // Note that the keyword includes the initial "/".
    const auto it = httpServerData.functionTable.find(keyword);
    if(it == httpServerData.functionTable.end()) {
        writeHtmlBegin(html);
        html << "Unsupported keyword " << keyword;
        writeHtmlEnd(html);
        return;
    }


    // We found the keyword. Call the function that processes this keyword.
    // The processing function is only responsible for writing the html body.
    writeHtmlBegin(html);
    try {
        const auto function = it->second;
        (this->*function)(request, html);
    } catch(std::exception& e) {
        html << "<br><br><span style='color:purple'>" << e.what() << "</span>";
    }
    writeHtmlEnd(html);
}



void Assembler::writeHtmlBegin(ostream& html) const
{
    html <<
        "\r\n"
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<link rel=icon href=docs/CZI-new-logo.png />"
        "<meta charset='UTF-8'>"
        "<title>Shasta assembler</title>";
    writeStyle(html);
    // writeMakeAllTablesSelectable(html);
    html <<
        "</head>"
        ;// "<body onload='makeAllTablesSelectableByDoubleClick()'>";
    writeNavigation(html);
}



void Assembler::writeHtmlEnd(ostream& html) const
{
    html << "</body>";
    html << "</html>";
}




void Assembler::writeMakeAllTablesSelectable(ostream& html) const
{
    html << R"###(
<script>

// Make all tables selectable by double click.
// This must be called after all tables have
// already been created, so it can be called during onload.

// This function is called when the user double clicks on a table.
function selectElement(table)
{
    var selection = window.getSelection();
    selection.removeAllRanges();
    var range = document.createRange();
    range.selectNode(table);
    selection.addRange(range);
}

// Attach the above function to the double click event
// for all tables in the document.
// Also add to each table a title that displays a tooltip 
// explaining that the table can be selected via double click.
function makeAllTablesSelectableByDoubleClick()
{
    var allTables = document.getElementsByTagName("table");
    for (var i=0; i<allTables.length; i++) {
        var table = allTables[i];
        table.ondblclick = function() {selectElement(this);};
        table.setAttribute("title", 
        "Double click to select the entire table. You can then paste it into a spreadsheet.");
    }
}
</script>
    )###";
}



void Assembler::writeNavigation(ostream& html) const
{
    html << "<ul class=navigationMenu>";

    writeNavigation(html, "Assembly information", {
        {"Summary", "exploreSummary"},
        });
    writeNavigation(html, "Reads", {
        {"Reads", "exploreRead"},
        });
    writeNavigation(html, "Alignments", {
        {"Stored alignments", "exploreAlignments"},
        {"Align two reads", "exploreAlignment"},
        {"Align one read with all", "computeAllAlignments"},
        {"Alignment graph", "exploreAlignmentGraph"},
        {"Alignment matrix", "displayAlignmentMatrix"},
        });
    writeNavigation(html, "Read graph", {
        {"Read graph", "exploreReadGraph"},
        });
    writeNavigation(html, "Marker graph", {
        {"Local marker graph", "exploreMarkerGraph?useBubbleReplacementEdges=on"},
        {"Marker graph vertices", "exploreMarkerGraphVertex"},
        {"Marker graph edges", "exploreMarkerGraphEdge"},
        });
    writeNavigation(html, "Assembly graph", {
        {"Local assembly graph", "exploreAssemblyGraph"},
        {"Assembly graph edges", "exploreAssemblyGraphEdge"},
        });
    writeNavigation(html, "Help", {
        {"Documentation", "docs/index.html"},
        });

    html << "</ul>";
}



void Assembler::writeNavigation(
    ostream& html,
    const string& title,
    const vector<pair <string, string> >& items) const
{
    html <<
        "<li class=navigationMenuEntry>"
        "<div class=navigationButton>" << title << "</div>"
        "<div class=navigationItems>";

    for(const auto& item: items) {
        html << "<a class=navigationItem href=" << item.second << ">" << item.first << "</a>";
    }

    html << "</div></li>";

}



// Access all available assembly data, without throwing exceptions
void Assembler::accessAllSoft()
{

    bool allDataAreAvailable = true;

    try {
        accessReadFlags(false);
    } catch(exception e) {
        cout << "Read flags are not accessible." << endl;
        allDataAreAvailable = false;
    }


    try {
        accessKmers();
    } catch(exception e) {
        cout << "K-mers are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessMarkers();
    } catch(exception e) {
        cout << "Markers are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAlignmentCandidates();
    } catch(exception e) {
        cout << "Alignment candidates are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAlignmentData();
    } catch(exception e) {
        cout << "Alignments are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessReadGraph();
    } catch(exception e) {
        cout << "The read graph is not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessMarkerGraphVertices();
    } catch(exception e) {
        cout << "Marker graph vertices are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessMarkerGraphEdges(false);
    } catch(exception e) {
        cout << "Marker graph edges are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAssemblyGraphVertices();
    } catch(exception e) {
        cout << "Assembly graph vertices are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAssemblyGraphEdges();
    } catch(exception e) {
        cout << "Assembly graph edges are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAssemblyGraphEdgeLists();
    } catch(exception e) {
        cout << "Assembly graph edge lists are not accessible." << endl;
        allDataAreAvailable = false;
    }

    try {
        accessAssemblyGraphSequences();
    } catch(exception e) {
        cout << "Assembly graph sequences are not accessible." << endl;
        allDataAreAvailable = false;
    }

    if(!allDataAreAvailable) {
        cout << "Not all assembly data are accessible." << endl;
        cout << "Some functionality is not available." << endl;
    }
}



void Assembler::exploreSummary(
    const vector<string>& request,
    ostream& html)
{
    using std::setprecision;


    // Compute the number of run-length k-mers used as markers.
    uint64_t totalRleKmerCount = 0;
    uint64_t markerRleKmerCount = 0;
    for(const auto& tableEntry: kmerTable) {
        if(tableEntry.isRleKmer) {
            ++totalRleKmerCount;
            if(tableEntry.isMarker) {
                ++markerRleKmerCount;
            }
        }
    }


    html <<
        "<h1>Assembly summary</h1>"



        "<h3>Reads used in this assembly</h3>"
        "<table>"
        "<tr><td>Number of reads"
        "<td class=right>" << assemblerInfo->readCount <<
        "<tr><td>Number of raw sequence bases"
        "<td class=right>" << assemblerInfo->baseCount <<
        "<tr><td>Average read length (for raw read sequence)"
        "<td class=right>" << assemblerInfo->baseCount / assemblerInfo->readCount <<
        "<tr><td>Read N50 (for raw read sequence)"
        "<td class=right>" << assemblerInfo->readN50 <<
        "<tr><td>Number of run-length encoded bases"
        "<td class=right>" << readRepeatCounts.totalSize() <<
        "<tr><td>Average length ratio of run-length encoded sequence over raw sequence"
        "<td class=right>" << setprecision(4) << double(readRepeatCounts.totalSize()) / double(assemblerInfo->baseCount) <<
        "</table>"
        "<ul>"
        "<li>Here and elsewhere, &quot;raw&quot; refers to the original read sequence, "
        "as opposed to run-length encoded sequence."
        "<li>Reads discarded on input are not included in the above table (see "
        "<a href='#discarded'>below</a>)."
        "<li>See ReadLengthHistogram.csv and Binned-ReadLengthHistogram.csv "
        "for details of the read length distribution of reads used in this assembly.</ul>"


        "<h3 id=discarded>Reads discarded on input</h3>"
        "<table>"
        "<tr><th><th>Reads<th>Bases"
        "<tr><td>Reads discarded on input because they were too short"
        "<td class=right>" << assemblerInfo->discardedShortReadReadCount <<
        "<td class=right>" << assemblerInfo->discardedShortReadBaseCount <<
        "<tr><td>Reads discarded on input because they contained repeat counts greater than 255"
        "<td class=right>" << assemblerInfo->discardedBadRepeatCountReadCount <<
        "<td class=right>" << assemblerInfo->discardedBadRepeatCountBaseCount <<
        "<tr><td>Reads discarded on input, total"
        "<td class=right>" <<
        assemblerInfo->discardedShortReadReadCount+assemblerInfo->discardedBadRepeatCountReadCount <<
        "<td class=right>" <<
        assemblerInfo->discardedShortReadBaseCount+assemblerInfo->discardedBadRepeatCountBaseCount <<
        "<tr><td>Fraction of reads discarded on input over total present in input files"
        "<td class=right>" <<
        double(assemblerInfo->discardedShortReadReadCount+assemblerInfo->discardedBadRepeatCountReadCount) /
        double(assemblerInfo->discardedShortReadReadCount+assemblerInfo->discardedBadRepeatCountReadCount+assemblerInfo->readCount)
        <<
        "<td class=right>" <<
        double(assemblerInfo->discardedShortReadBaseCount+assemblerInfo->discardedBadRepeatCountBaseCount) /
        double(assemblerInfo->discardedShortReadBaseCount+assemblerInfo->discardedBadRepeatCountBaseCount+assemblerInfo->baseCount)
        <<
        "</table>"
        "<ul><li>Base counts in the above table are raw sequence bases."
        "<li>Here and elsewhere, &quot;raw&quot; refers to the original read sequence, "
        "as opposed to run-length encoded sequence.</ul>"


        "<h3>Marker <i>k</i>-mers</h3>"
        "<table>"
        "<tr><td>Length <i>k</i> of <i>k</i>-mers used as markers"
        "<td class=right>" << assemblerInfo->k <<
        "<tr><td>Total number of <i>k</i>-mers"
        "<td class=right>" << totalRleKmerCount <<
        "<tr><td>Number of <i>k</i>-mers used as markers"
        "<td class=right>" << markerRleKmerCount <<
        "<tr><td>Fraction of <i>k</i>-mers used as markers"
        "<td class=right>" << setprecision(3) << double(markerRleKmerCount) / double(totalRleKmerCount) <<
        "</table>"
        "<ul><li>In the above table, all <i>k</i>-mer counts only include run-length encoded <i>k</i>-mers, "
        "that is, <i>k</i>-mers without repeated bases.</ul>"



        "<h3>Markers</h3>"
        "<table>"
        "<tr><td>Total number of markers on all reads, one strand"
        "<td class=right>" << markers.totalSize()/2 <<
        "<tr><td>Total number of markers on all reads, both strands"
        "<td class=right>" << markers.totalSize() <<
        "<tr><td>Average number of markers per raw base"
        "<td class=right>" << setprecision(4) << double(markers.totalSize()/2)/double(assemblerInfo->baseCount) <<
        "<tr><td>Average number of markers per run-length encoded base"
        "<td class=right>" << setprecision(4) << double(markers.totalSize()/2)/double(readRepeatCounts.totalSize()) <<
        "<tr><td>Average spacing between markers in raw sequence"
        "<td class=right>" << setprecision(4) << double(assemblerInfo->baseCount)/double(markers.totalSize()/2) <<
        "<tr><td>Average spacing beteen markers in run-length encoded sequence"
        "<td class=right>" << setprecision(4) << double(readRepeatCounts.totalSize())/double(markers.totalSize()/2) <<
        "</table>"
        "<ul><li>Here and elsewhere, &quot;raw&quot; refers to the original read sequence, "
        "as opposed to run-length encoded sequence.</ul>"



        "<h3>Alignments</h3>"
        "<table>"
        "<tr><td>Number of alignment candidates found by the LowHash algorithm"
        "<td class=right>" << alignmentCandidates.size() <<
        "<tr><td>Number of good alignments"
        "<td class=right>" << alignmentData.size() <<
        "</table>"



        "<h3>Read graph</h3>"
        "<table>"
        "<tr><td>Number of vertices"
        "<td class=right>" << readGraph.connectivity.size() <<
        "<tr><td>Number of edges"
        "<td class=right>" << readGraph.edges.size() <<
        "</table>"



        "<h3>Marker graph</h3>"
        "<table>"
        "<tr><td>Number of vertices"
        "<td class=right>" << markerGraph.vertices.size() <<
        "<tr><td>Initial number of edges"
        "<td class=right>" << markerGraph.edges.size() <<
        "<tr><td>Final number of edges"
        "<td class=right>" << "" <<
        "</table>"



        "<h3>Assembly graph</h3>"
        "<table>"
        "<tr><td>Number of vertices"
        "<td class=right>" << assemblyGraph.vertices.size() <<
        "<tr><td>Number of edges"
        "<td class=right>" << assemblyGraph.edges.size() <<
        "</table>";
}



void Assembler::exploreRead(
    const vector<string>& request,
    ostream& html)
{
    // Get the ReadId and Strand from the request.
    ReadId readId = 0;
    const bool readIdIsPresent = getParameterValue(request, "readId", readId);
    Strand strand = 0;
    const bool strandIsPresent = getParameterValue(request, "strand", strand);

    // Get the begin and end position.
    uint32_t beginPosition = 0;
    const bool beginPositionIsPresent = getParameterValue(request, "beginPosition", beginPosition);
    uint32_t endPosition = 0;
    const bool endPositionIsPresent = getParameterValue(request, "endPosition", endPosition);

    // Get the set of ordinal for markers that should be highlighted.
    vector<string> highlightedMarkerStrings;
    getParameterValues(request, "highlightMarker", highlightedMarkerStrings);
    std::set<uint32_t> highlightedMarkers;
    for(const string& s: highlightedMarkerStrings) {
        try {
            highlightedMarkers.insert(boost::lexical_cast<uint32_t>(s));
        } catch(std::exception&) {
            // Ignore.
        }
    }

    // Write the form.
    html <<
        "<form>"
        "<input type=submit value='Show read'> "
        "<input type=text name=readId required" <<
        (readIdIsPresent ? (" value=" + to_string(readId)) : "") <<
        " size=8 title='Enter a read id between 0 and " << reads.size()-1 << "'>"
        " on strand ";
    writeStrandSelection(html, "strand", strandIsPresent && strand==0, strandIsPresent && strand==1);
    html << "<br><input type=text name=beginPosition size=8";
    if(beginPositionIsPresent) {
        html << " value=" << beginPosition;
    }
    html <<
        ">Begin display of raw sequence at this base position (leave blank to begin at beginning of read)."
        "<br><input type=text name=endPosition size=8";
    if(endPositionIsPresent) {
        html << " value=" << endPosition;
    }
    html <<
        ">End display of raw sequence at this base position (leave blank to end at end of read)."
        "</form>";

    // If the readId or strand are missing, stop here.
    if(!readIdIsPresent || !strandIsPresent) {
        return;
    }

    // Access the read.
    if(readId >= reads.size()) {
        html << "<p>Invalid read id.";
        return;
    }
    if(strand!=0 && strand!=1) {
        html << "<p>Invalid strand.";
        return;
    }
    const OrientedReadId orientedReadId(readId, strand);
    const vector<Base> rawOrientedReadSequence = getOrientedReadRawSequence(orientedReadId);
    const auto readStoredSequence = reads[readId];
    const auto readName = readNames[readId];
    const auto orientedReadMarkers = markers[orientedReadId.getValue()];
    if(!beginPositionIsPresent) {
        beginPosition = 0;
    }
    if(!endPositionIsPresent) {
        endPosition = uint32_t(rawOrientedReadSequence.size());
    }
    if(endPosition <= beginPosition) {
        html << "<p>Invalid choice of begin and end position.";
        return;
    }



    // Page title.
    html << "<h1 title='Read " << readId << " on strand " << strand;
    if(strand == 0) {
        html << " (input read without reverse complementing)";
    } else {
        html << " (reverse complement of input read)";
    }
    html << "'>Oriented read " << orientedReadId << "</h1>";

    // Read name.
    html << "<p>Read name on input: ";
    copy(readName.begin(), readName.end(), ostream_iterator<char>(html));

    // Read length.
    html << "<p>This read is " << rawOrientedReadSequence.size() << " bases long";
    html << " (" << readStoredSequence.baseCount << " bases in run-length representation)";
    html << " and has " << orientedReadMarkers.size() << " markers.";

    // Begin/end position (in raw sequence).
    if(beginPositionIsPresent || endPositionIsPresent) {
        html <<
            " Displaying only " << endPosition-beginPosition << " bases";
        html << " of raw read sequences";
        html << " beginning at base position " << beginPosition <<
            " and ending at base position " << endPosition <<
            " .";
        html << " For sequence in run-length representation see below.";
    }



    // Button to Blat this read or portion of a read.
    // We cannot use a simple <a> because we need to do a POST
    // (the GET request fails when the read is too long).
    html <<
        "<p><form action='https://genome.ucsc.edu/cgi-bin/hgBlat' method=post>"
        "<input type=submit value='Blat ";
    if(beginPositionIsPresent || endPositionIsPresent) {
        html << "this portion of ";
    }
    html <<
        "this read in the UCSC browser'>"
        "<input type=text hidden name=type value=DNA>"
        // Don't specify the genome.
        // UCSC browser will Blat again last used genome (stored in cookies).
        // "<input type=text hidden name=type value=DNA>"
        // "<input type=text hidden name=name value=Human>"
        // "<input type=text hidden name=db value=hg38>"
        "<input type=text hidden name=userSeq value=";
    copy(
        rawOrientedReadSequence.begin() + beginPosition,
        rawOrientedReadSequence.begin() + endPosition,
        ostream_iterator<Base>(html));
    html << "></form>";



    // Button to Blast this read or portion of a read.
    html <<
        "<p><form action='blastRead'>"
        "<input type=submit value='Blast ";
    if(beginPositionIsPresent || endPositionIsPresent) {
        html << "this portion of ";
    }
    html <<
        "this read against " << httpServerData.referenceFastaFileName << " using Blast options: '>"
        "<input type=text hidden name=readId value=" << readId << ">" <<
        "<input type=text hidden name=strand value=" << strand << ">" <<
        "<input type=text hidden name=beginPosition value=" << beginPosition << ">" <<
        "<input type=text hidden name=endPosition value=" << endPosition << ">"
        "<input type=text size=80 name=blastOptions>"
        "</form>";



    // Button to Blast this read or portion of a read (summary output).
    html <<
        "<p><form action='blastRead'>"
        "<input type=submit value='Blast ";
    if(beginPositionIsPresent || endPositionIsPresent) {
        html << "this portion of ";
    }
    html <<
        "this read against " << httpServerData.referenceFastaFileName << " (summary output)'>"
        "<input type=text hidden name=readId value=" << readId << ">" <<
        "<input type=text hidden name=strand value=" << strand << ">" <<
        "<input type=text hidden name=beginPosition value=" << beginPosition << ">" <<
        "<input type=text hidden name=endPosition value=" << endPosition << ">"
        "<input type=checkbox checked hidden name=summary>"
        "</form>";



    // Link to align this read against another read.
    html <<
        "<p><a href='exploreAlignment?readId0=" << readId << "&strand0=" << strand <<
        "'>Compute a marker alignment of this read with another read.</a>";

    // Link to show overlapping reads.
    html <<
        "<p><a href='exploreOverlappingReads?readId=" << readId << "&strand=" << strand <<
        "'>Find other reads that overlap this read.</a>";



    // Display the selected portion of raw sequence.
    const bool partialSequenceRequested =  beginPositionIsPresent || endPositionIsPresent;
    if(true) {
        html << "<h3>";
        if(partialSequenceRequested) {
            html << "Selected portion of raw sequence of this oriented read";
        } else {
            html << "Raw sequence of this oriented read";
        }
        html << "</h3>";

        // Here we don't have to worry about using an svg object like we do below,
        // because we are just writing text without html, and so there will
        // be no alignment problems.

        // Labels for position scale.
        html << "<pre style='font-family:monospace;margin:0'";
        html << " title='Position in raw read sequence'";
        html<< ">";
        for(size_t position=beginPosition; position<endPosition; ) {
            if((position%10)==0) {
                const string label = to_string(position);
                html << label;
                for(size_t i=0; i<10-label.size(); i++) {
                    html << " ";
                }
                position += 10;
            } else {
                html << " ";
                ++position;
            }
        }
        html<< "\n";

        // Position scale.
        for(size_t position=beginPosition; position!=endPosition; position++) {
            if((position%10)==0) {
                html << "|";
            } else if((position%5)==0) {
                html << "+";
            } else {
                html << ".";
            }
        }
        html << "</pre>";



        // Sequence.
        html << "<pre style='font-family:monospace;margin:0'>";
        for(uint32_t position=beginPosition; position!=endPosition; position++) {
            html << rawOrientedReadSequence[position];
        }
        html << "</pre>";



        // Also write a position scale for positions in the run-length representation.
        if(true) {
            html << "<pre style='font-family:monospace;margin:0'";
            html << " title='Position in run-length read sequence'>";

            const vector<uint32_t> rawPositions = getRawPositions(orientedReadId);

            // Scale.
            bool firstTime = true;
            for(int runLengthPosition=0; runLengthPosition<int(rawPositions.size()); runLengthPosition++) {
                const int rawPosition = rawPositions[runLengthPosition];
                // cout << runLengthPosition << " " << rawPosition << endl;
                if(rawPosition >= int(endPosition)) {
                    break;
                }
                if(rawPosition < int(beginPosition)) {
                    continue;
                }
                uint32_t skip;
                if(firstTime) {
                    skip = rawPosition - beginPosition;
                } else {
                    skip = rawPosition - rawPositions[runLengthPosition-1] - 1;
                }
                for(uint32_t i=0; i<skip; i++) {
                    html << " ";
                }
                firstTime = false;
                if((runLengthPosition % 10) == 0) {
                    html << "|";
                    //cout << "|";
                } else if((runLengthPosition % 5) == 0) {
                    html << "+";
                    //cout << "+";
                } else {
                    html << ".";
                    //cout << ".";
                }
            }
            html << "\n";

            // Labels.
            firstTime = true;
            for(int runLengthPosition=0; runLengthPosition<int(rawPositions.size()); runLengthPosition+=10) {
                const int rawPosition = rawPositions[runLengthPosition];
                if(rawPosition >= int(endPosition)) {
                    break;
                }
                if(rawPosition < int(beginPosition)) {
                    continue;
                }

                uint32_t skip;
                if(firstTime) {
                    skip = rawPosition - beginPosition;
                } else {
                    skip = rawPosition - rawPositions[runLengthPosition-10] - 10;
                }
                for(uint32_t i=0; i<skip; i++) {
                    html << " ";
                }
                firstTime = false;

                const string label = to_string(runLengthPosition);
                html << label;
                for(size_t i=0; i<10-label.size(); i++) {
                    html << " ";
                }

            }


            html << "</pre>";
            //cout << endl;
        }


        // Button to download the sequence to a fasta file
        html <<
            "<a id=fastaDownload>Download in FASTA format</a><br>"
            "<script>"
            "var element = document.getElementById('fastaDownload');"
            "element.setAttribute('href', 'data:text/plain;charset=utf-8,' +"
            "encodeURIComponent('>" << orientedReadId <<
            "-" << beginPosition << "-" << endPosition << " " << endPosition-beginPosition <<
            " ";
        copy(readName.begin(), readName.end(), ostream_iterator<char>(html));
        html << "\\n";
        for(uint32_t position=beginPosition; position!=endPosition; position++) {
            html << rawOrientedReadSequence[position];
        }
        html << "\\n'));"
            "element.setAttribute('download', '" << orientedReadId << "-" <<
            beginPosition << "-" << endPosition <<
            ".fa');"
            "</script>";
    }



    // If there are no markers, stop here.
    if(orientedReadMarkers.empty()) {
        html << "<p>This read has no markers.";
        return;
    }



    // Decide on which row each marker gets displayed
    // (first row of markers is row 0).
    const size_t k = assemblerInfo->k;
    vector<int> markerRow(orientedReadMarkers.size(), -1);
    vector<uint32_t> nextAvailableCharacterPosition(k, 0);
    for(uint32_t ordinal=0; ordinal<orientedReadMarkers.size(); ordinal++) {
        const uint32_t position = orientedReadMarkers[ordinal].position;
        for(int row=0; row<int(k); row++) {
            if(position >= nextAvailableCharacterPosition[row]) {
                markerRow[ordinal] = row;
                // Require one character space to next marker.
                nextAvailableCharacterPosition[row] = position + uint32_t(k) + 1;
                // cout << "Marker " << ordinal << " at position " << position << " placed on row " << row << endl;
                break;
            }
        }
        /*
        if(markerRow[ordinal] == -1) {
            cout << "Marker " << ordinal << " at position " << position << " could not be placed." << endl;
            cout << "nextAvailableCharacterPosition: ";
            for(const auto rowNext: nextAvailableCharacterPosition) {
                cout << " " << rowNext;
            }
            cout << endl;
        }
        */
        CZI_ASSERT(markerRow[ordinal] != -1);
    }
    const int markerRowCount = *std::max_element(markerRow.begin(), markerRow.end());


    // Title for the next portion of the display, which shows the markers.
    html << "<h3>Run-length representation of oriented read sequence and its markers</h3>";



    // Use an svg object to display the read sequence as stored and the markers.
    // To ensure correct positioning and alignment, we use
    // a textLength attribute on every <text> element.
    // (The older code, ifdef'ed out, uses a separate <text>
    // element for each character).
    // Note that here we display the entire read, regardless of beginPosition and endPosition.
    const int monospaceFontSize = 12;
    const int horizontalSpacing = 7;
    const int verticalSpacing = 13;
    const int charactersPerLine = int(readStoredSequence.baseCount) + 10; // Add space for labels
    int svgLineCount = int(3 + markerRowCount); // Labels, scale, sequence, markers.
    svgLineCount++;     // Add a line with the repeat counts.
    const int svgWidth = horizontalSpacing * charactersPerLine;
    const int svgHeight = verticalSpacing * svgLineCount;
    const int highlightedMarkerVerticalOffset = 2;
    html <<
        "<p><svg width=" << svgWidth << " height=" << svgHeight << ">"
        "<style>"
        ".mono{font-family:monospace; font-size:" << monospaceFontSize << "px;}"
        ".blueMono{font-family:monospace; font-size:" << monospaceFontSize << "px; fill:blue;}"
        "</style>";



    // Labels for position scale.
    for(uint32_t position=0; position<readStoredSequence.baseCount; position+=10) {
        const string label = to_string(position);

        // Use a single <text> element with a textLength attribute for exact alignment.
        html <<
            "<text class='mono'" <<
            " x='" << position * horizontalSpacing << "'" <<
            " y='" << verticalSpacing << "'" <<
            " textLength='" << label.size() * horizontalSpacing << "px'>" <<
            label << "</text>";
    }



    // Position scale.
    // This code uses one <text> element for every blockSize characters.
    // This way you can select sequence text without getting a
    // new line after each character, while still achieving good
    // alignment.
    const size_t blockSize = 100;
    for(size_t blockBegin=0; blockBegin<readStoredSequence.baseCount; blockBegin+=blockSize) {
        const size_t blockEnd = min(blockBegin+blockSize, readStoredSequence.baseCount);
        html <<
            "<text class='mono'" <<
            " x='" << blockBegin*horizontalSpacing << "'" <<
            " y='" << 2*verticalSpacing << "'"
            " textLength='" << (blockEnd-blockBegin) * horizontalSpacing<< "'>";
        for(size_t position=blockBegin; position!=blockEnd; position++) {
            if((position%10)==0) {
                html << "|";
            } else if((position%5)==0) {
                html << "+";
            } else {
                html << ".";
            }
        }
        html << "</text>";
    }



    // Repeat counts.
    for(size_t position=0; position!=readStoredSequence.baseCount; position++) {
        Base base;
        uint8_t repeatCount;
        tie(base, repeatCount) = getOrientedReadBaseAndRepeatCount(orientedReadId, uint32_t(position));
        html <<
            "<text class='mono'" <<
            " x='" << position*horizontalSpacing << "'" <<
            " y='" << 3*verticalSpacing << "'"
            " textLength='" << horizontalSpacing<< "'>";
        if(repeatCount < 10) {
            html << int(repeatCount);
        } else {
            html << "*";
        }
        html << "<title>" << base << " at run-length position " << position <<
            " is repeated " << int(repeatCount) << " times</title>";
        html << "</text>";
    }



    // Raw read sequence.
    // This code uses one <text> element for every blockSize characters.
    // This way you can select sequence text without getting a
    // new line after each character, while still achieving good
    // alignment.
    const uint32_t readSequenceLine = 4;
    for(size_t blockBegin=0; blockBegin<readStoredSequence.baseCount; blockBegin+=blockSize) {
        const size_t blockEnd = min(blockBegin+blockSize, readStoredSequence.baseCount);
        html <<
            "<text class='mono'" <<
            " x='" << blockBegin*horizontalSpacing << "'" <<
            " y='" << readSequenceLine*verticalSpacing << "'"
            " textLength='" << (blockEnd-blockBegin) * horizontalSpacing<< "'>";
        for(size_t position=blockBegin; position!=blockEnd; position++) {
            html << getOrientedReadBase(orientedReadId, uint32_t(position));
        }
        html << "</text>";
    }



    // Draw a rectangle for each highlighted marker.
    for(const uint32_t ordinal: highlightedMarkers) {
        const CompressedMarker& marker = orientedReadMarkers[ordinal];
        html <<
            "<rect" <<
            " x='" << (marker.position-beginPosition)*horizontalSpacing << "'"
            " y='" << (readSequenceLine + markerRow[ordinal])*verticalSpacing + highlightedMarkerVerticalOffset << "'"
            " height='" << verticalSpacing << "'"
            " width='" << k * horizontalSpacing << "'"
            " style='fill:pink; stroke:none;'"
            "/>";
    }



    // Markers.
    for(uint32_t ordinal=0; ordinal<uint32_t(orientedReadMarkers.size()); ordinal++) {
        const CompressedMarker& marker = orientedReadMarkers[ordinal];

        // See if this marker is contained in a vertex of the marker graph.
        const MarkerGraph::VertexId vertexId =
            getGlobalMarkerGraphVertex(orientedReadId, ordinal);
        const bool hasMarkerGraphVertex =
            (vertexId != MarkerGraph::invalidCompressedVertexId);



        // Write the k-mer of this marker.
        const Kmer kmer(marker.kmerId, k);
        html << "<a xlink:title='Marker " << ordinal << ", position " << marker.position;
        if(hasMarkerGraphVertex) {
            html << ", coverage " << markerGraph.vertices.size(vertexId);
        }
        html << "' id='" << ordinal << "'";
        if(hasMarkerGraphVertex) {
            // Add a hyperlink to the marker graph vertex
            // that contains this marker.
            const string url = "exploreMarkerGraph?vertexId=" + to_string(vertexId) +
                "&maxDistance=2&detailed=on&minCoverage=3&minConsensus=3&sizePixels=3200&timeout=30";
            html << " xlink:href='" << url << "' style='cursor:pointer'";
        }
        html << ">";

        // This code uses one <text> element per character.
        for(size_t positionInMarker=0; positionInMarker<k; positionInMarker++) {
            html << "<text class='";
            if(hasMarkerGraphVertex) {
                html << "blueMono";
            } else {
                html << "mono";
            }
            html << "'" <<
                " x='" << (marker.position+positionInMarker)*horizontalSpacing << "'" <<
                " y='" << (readSequenceLine+1+markerRow[ordinal])*verticalSpacing << "'>";
            html << kmer[positionInMarker];
            html << "</text>";
        }
        html << "</a>";

    }


    // Finish the svg object.
    html << "</svg>";

    // Scroll to the first highlighted marker.
    if(!highlightedMarkers.empty()) {
        const uint32_t ordinal = *highlightedMarkers.begin();
        html <<
            "<script>"
            "var element = document.getElementById('" << ordinal << "');"
            "var rectangle = element.getBoundingClientRect();"
            "window.scroll(rectangle.left-100, rectangle.top-100);"
            "</script>";
    }






    html <<
        "<p>You can click on a blue marker above "
        "to see the global marker graph around that marker. "
        "Black markers correspond to a vertex of the marker graph "
        "that was removed because of low coverage.";
}



void Assembler::blastRead(
    const vector<string>& request,
    ostream& html)
{

    if(!filesystem::isRegularFile(httpServerData.referenceFastaFileName)) {
        html << "<p>The fasta sequence " << httpServerData.referenceFastaFileName <<
            " to be used as the reference (Blast subject) does not exist.";
        return;
    }



    // Get the ReadId and Strand from the request.
    ReadId readId = 0;
    const bool readIdIsPresent = getParameterValue(request, "readId", readId);
    Strand strand = 0;
    const bool strandIsPresent = getParameterValue(request, "strand", strand);
    if(!(readIdIsPresent && strandIsPresent)) {
        return;
    }

    // Get the begin and end position.
    uint32_t beginPosition = 0;
    getParameterValue(request, "beginPosition", beginPosition);
    uint32_t endPosition = 0;
    const bool endPositionIsPresent = getParameterValue(request, "endPosition", endPosition);

    // Get blast options.
    string blastOptions;
    getParameterValue(request, "blastOptions", blastOptions);
    string summaryString;
    const bool isSummary = getParameterValue(request, "summary", summaryString);
    if(isSummary) {
        blastOptions =
            "-outfmt '10 bitscore qstart qend sseqid sstart send length pident' "
            "-evalue 1e-200";
            // The following is used to avoid breaking up alignments too much.
            // But it also slows down the search a lot.
            // "-reward 3 -penalty -2 -gapopen 5 -gapextend 5";
    }



    // Access the read.
    if(readId >= reads.size()) {
        html << "<p>Invalid read id.";
        return;
    }
    if(strand!=0 && strand!=1) {
        html << "<p>Invalid strand.";
        return;
    }
    const OrientedReadId orientedReadId(readId, strand);
    const vector<Base> rawOrientedReadSequence = getOrientedReadRawSequence(orientedReadId);
    if(!endPositionIsPresent) {
        endPosition = uint32_t(rawOrientedReadSequence.size());
    }
    if(endPosition <= beginPosition) {
        html << "<p>Invalid choice of begin and end position.";
        return;
    }



    // Write a title.
    html << "<h1>Blast results for oriented read " << orientedReadId;
    html << ", position range " << beginPosition << " " << endPosition;
    html << " (" << endPosition-beginPosition << " bases)</h1>";



    // Create a fasta file with this sequence.
    const string uuid = to_string(boost::uuids::random_generator()());
    const string fastaFileName = "/dev/shm/" + uuid + ".fa";
    ofstream fastaFile(fastaFileName);
    fastaFile << ">" << OrientedReadId(readId, strand);
    fastaFile << "-" << beginPosition << "-" << endPosition<< "\n";
    copy(rawOrientedReadSequence.begin() + beginPosition,
        rawOrientedReadSequence.begin() + endPosition,
        ostream_iterator<Base>(fastaFile));
    fastaFile << "\n";
    fastaFile.close();



    // Create the blast command and run it.
    const string blastOutputFileName = "/dev/shm/" + uuid + ".txt";
    const string blastErrFileName = "/dev/shm/" + uuid + ".errtxt";
    const string command = "blastn -task megablast -subject " + httpServerData.referenceFastaFileName +
        " -query " + fastaFileName + " 1>" + blastOutputFileName + " 2>" + blastErrFileName +
        " " + blastOptions;
    ::system(command.c_str());



    // Copy any error output to html.
    if(filesystem::fileSize(blastErrFileName)) {
        ifstream blastErrFile(blastErrFileName);
        html << "<pre style='font-size:10px'>";
        html << blastErrFile.rdbuf();
        html << "</pre>";
        blastErrFile.close();
    }



    // Output to html.
    if(isSummary) {

        html << "<br>Blast options used: " << blastOptions << "<p>";

        // Tokenize and gather the output, each line with its score.
        using Separator = boost::char_separator<char>;
        using Tokenizer = boost::tokenizer<Separator>;
        const Separator separator(",");
        vector< pair<double, vector<string> > > alignments;
        ifstream blastOutputFile(blastOutputFileName);
        string line;
        vector<string> tokens;
        while(true) {

            // Get a line.
            string line;
            std::getline(blastOutputFile, line);
            if(!blastOutputFile) {
                break;
            }

            // Tokenize it.
            Tokenizer tokenizer(line, separator);
            tokens.clear();
            tokens.insert(tokens.begin(), tokenizer.begin(), tokenizer.end());

            // Extract the score.
            CZI_ASSERT(!tokens.empty());
            const double score = std::stod(tokens.front());;

            // Store it.
            alignments.push_back(make_pair(score, tokens));
        }

        // Sort by score.
        sort(alignments.begin(), alignments.end(),
            std::greater< pair<double, vector<string> > >());

        // Write it out.
        html <<
            "<table><tr>"
            "<th rowspan=2>Bit<br>score"
            "<th colspan=3>In " << orientedReadId <<
            "<th colspan=5>In " << httpServerData.referenceFastaFileName <<
            "<th rowspan=2>Alignment<br>length"
            "<th rowspan=2>Identity<br>(%)"
            "<tr>"
            "<th>Begin"
            "<th>End"
            "<th>Length"
            "<th>Strand"
            "<th>Name"
            "<th>Begin"
            "<th>End"
            "<th>Length";
        for(const auto& p: alignments) {
            const auto& tokens = p.second;
            // bitscore qstart qend sseqid sstart send length pident
            CZI_ASSERT(tokens.size() == 8);
            const string& bitscore = tokens[0];
            const size_t qstart = std::stoi(tokens[1]) + beginPosition;
            const size_t qend = std::stoi(tokens[2]) + beginPosition;
            const string& sseqid = tokens[3];
            size_t sstart = std::stoi(tokens[4]);
            size_t send = std::stoi(tokens[5]);
            const string& length = tokens[6];
            const string& pident = tokens[7];
            Strand strand = 0;
            if(send < sstart) {
                swap(sstart, send);
                strand = 1;
            }
            html <<
                "<tr style='text-align:center'>"
                "<td>" << bitscore <<
                "<td>" << qstart <<
                "<td>" << qend <<
                "<td>" << qend-qstart <<
                "<td>" << (strand==0 ? "+" : "-") << " (" << strand << ")"
                "<td>" << sseqid <<
                "<td>" << sstart <<
                "<td>" << send <<
                "<td>" << send-sstart <<
                "<td>" << length <<
                "<td>" << pident;
        }
        html << "</table>";

    } else {

        // This is not summary output.
        // Just copy Blast output to html.
        ifstream blastOutputFile(blastOutputFileName);
        html << "<pre style='font-size:10px'>";
        html << blastOutputFile.rdbuf();
        html << "</pre>";
    }



    // Remove the files we created.
    filesystem::remove(fastaFileName);
    filesystem::remove(blastOutputFileName);
    filesystem::remove(blastErrFileName);
}



void Assembler::exploreAlignments(
    const vector<string>& request,
    ostream& html)
{
    // Get the ReadId and Strand from the request.
    ReadId readId0 = 0;
    const bool readId0IsPresent = getParameterValue(request, "readId", readId0);
    Strand strand0 = 0;
    const bool strand0IsPresent = getParameterValue(request, "strand", strand0);

    // Write the form.
    html <<
        "<form>"
        "<input type=submit value='Show alignments involving read'> "
        "<input type=text name=readId required" <<
        (readId0IsPresent ? (" value=" + to_string(readId0)) : "") <<
        " size=8 title='Enter a read id between 0 and " << reads.size()-1 << "'>"
        " on strand ";
    writeStrandSelection(html, "strand", strand0IsPresent && strand0==0, strand0IsPresent && strand0==1);
    html << "</form>";

    // If the readId or strand are missing, stop here.
    if(!readId0IsPresent || !strand0IsPresent) {
        return;
    }

    // Page title.
    const OrientedReadId orientedReadId0(readId0, strand0);
    html <<
        "<h1>Alignments involving oriented read "
        "<a href='exploreRead?readId=" << readId0  << "&strand=" << strand0 << "'>"
        << OrientedReadId(readId0, strand0) << "</a>"
        << " (" << markers[orientedReadId0.getValue()].size() << " markers)"
        "</h1>";


#if 0
    // Begin the table.
    html <<
        "<table><tr>"
        "<th rowspan=2>Other<br>oriented<br>read"
        "<th rowspan=2 title='The number of aligned markers. Click on a cell in this column to see more alignment details.'>Aligned<br>markers"
        "<th colspan=5>Markers on oriented read " << OrientedReadId(readId0, strand0) <<
        "<th colspan=5>Markers on other oriented read"
        "<tr>";
    for(int i=0; i<2; i++) {
        html <<
            "<th title='Number of aligned markers on the left of the alignment'>Left<br>unaligned"
            "<th title='Number of markers in the aligned range'>Alignment<br>range"
            "<th title='Number of aligned markers on the right of the alignment'>Right<br>unaligned"
            "<th title='Total number of markers on the oriented read'>Total"
            "<th title='Fraction of aligned markers in the alignment range'>Aligned<br>fraction";
    }
#endif


    // Loop over the alignments that this oriented read is involved in, with the proper orientation.
    const vector< pair<OrientedReadId, AlignmentInfo> > alignments =
        findOrientedAlignments(orientedReadId0);
    html << "<p>Found " << alignments.size() << " alignments.";
    displayAlignments(orientedReadId0, alignments, html);


}



// Display alignments in an html table.
void Assembler::displayAlignments(
    OrientedReadId orientedReadId0,
    const vector< pair<OrientedReadId, AlignmentInfo> >& alignments,
    ostream& html)
{
    const ReadId readId0 = orientedReadId0.getReadId();
    const Strand strand0 = orientedReadId0.getStrand();
    const uint32_t markerCount0 = uint32_t(markers[orientedReadId0.getValue()].size());


    // Compute the maximum number of markers that orientedReadId1
    // hangs out of orientedReadId0 on the left and right.
    uint32_t maxLeftHang = 0;
    uint32_t maxRightHang = 0;
    for(size_t i=0; i<alignments.size(); i++) {
        const auto& p = alignments[i];

        // Access information for this alignment.
        const AlignmentInfo& alignmentInfo = p.second;
        const uint32_t leftTrim0  = alignmentInfo.data[0].leftTrim ();
        const uint32_t leftTrim1  = alignmentInfo.data[1].leftTrim ();
        const uint32_t rightTrim0 = alignmentInfo.data[0].rightTrim();
        const uint32_t rightTrim1 = alignmentInfo.data[1].rightTrim();

        // Update the maximum left hang.
        if(leftTrim1 > leftTrim0) {
            maxLeftHang = max(maxLeftHang, leftTrim1 - leftTrim0);
        }

        // Update the maximum left hang.
        if(rightTrim1 > rightTrim0) {
            maxRightHang = max(maxRightHang, rightTrim1 - rightTrim0);
        }
    }


    // Begin the table.
    const int bitShift = 6; // Controls the scaling of the alignment sketch.
    html <<
        "<table>"
        "<tr>"
        "<th rowspan=2>Index"
        "<th rowspan=2>Other<br>oriented<br>read"
        "<th rowspan=2 title='The number of aligned markers. Click on a cell in this column to see more alignment details.'>Aligned<br>markers"
        "<th colspan=5>Markers on oriented read " << orientedReadId0 <<
        "<th colspan=5>Markers on other oriented read"
        "<th rowspan=2>Alignment sketch"
        "<tr>";
    for(int i=0; i<2; i++) {
        html <<
            "<th title='Number of aligned markers on the left of the alignment'>Left<br>unaligned"
            "<th title='Number of markers in the aligned range'>Alignment<br>range"
            "<th title='Number of aligned markers on the right of the alignment'>Right<br>unaligned"
            "<th title='Total number of markers on the oriented read'>Total"
            "<th title='Fraction of aligned markers in the alignment range'>Aligned<br>fraction";
    }



    // Loop over the alignments.
    for(size_t i=0; i<alignments.size(); i++) {
        const auto& p = alignments[i];

        // Access information for this alignment.
        const OrientedReadId orientedReadId1 = p.first;
        const AlignmentInfo& alignmentInfo = p.second;
        const ReadId readId1 = orientedReadId1.getReadId();
        const ReadId strand1 = orientedReadId1.getStrand();
        const uint32_t markerCount1 = uint32_t(markers[orientedReadId1.getValue()].size());

        const uint32_t leftTrim0 = alignmentInfo.data[0].leftTrim();
        const uint32_t leftTrim1 = alignmentInfo.data[1].leftTrim();
        const uint32_t rightTrim0 = alignmentInfo.data[0].rightTrim();
        const uint32_t rightTrim1 = alignmentInfo.data[1].rightTrim();

        // Write a row in the table for this alignment.
        html <<
            "<tr>"
            "<td class=centered>" << i <<
            "<td class=centered><a href='exploreRead?readId=" << readId1  << "&strand=" << strand1 <<
            "' title='Click to see this read'>" << orientedReadId1 << "</a>"
            "<td class=centered>"
            "<a href='exploreAlignment"
            "?readId0=" << readId0 << "&strand0=" << strand0 <<
            "&readId1=" << readId1 << "&strand1=" << strand1 <<
            "' title='Click to see the alignment'>" << alignmentInfo.markerCount << "</a>"
            "<td class=centered>" << alignmentInfo.leftTrim(0) <<
            "<td class=centered>" << alignmentInfo.range(0) <<
            "<td class=centered>" << alignmentInfo.rightTrim(0) <<
            "<td class=centered>" << markerCount0 <<
            "<td class=centered>" << std::setprecision(2) <<
            alignmentInfo.alignedFraction(0) <<
            "<td class=centered>" << alignmentInfo.leftTrim(1) <<
            "<td class=centered>" << alignmentInfo.range(1) <<
            "<td class=centered>" << alignmentInfo.rightTrim(1) <<
            "<td class=centered>" << markerCount1 <<
            "<td class=centered>" << std::setprecision(2) <<
            alignmentInfo.alignedFraction(1);



        // Write the alignment sketch.
        html <<
            "<td class=centered style='line-height:8px'>"

            // Oriented read 0.
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << (maxLeftHang>>bitShift) <<
            "px;'></div>"
            "<div title='Oriented read " << orientedReadId0 <<
            "' style='display:inline-block;margin:0px;padding:0px;"
            "background-color:blue;height:6px;width:" << (markerCount0>>bitShift) <<
            "px;'></div>"
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << (maxRightHang>>bitShift) <<
            "px;'></div>"

            // Aligned portion.
            "<br>"
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << ((maxLeftHang+leftTrim0)>>bitShift) <<
            "px;'></div>"
            "<div title='Aligned portion'"
            " style='display:inline-block;margin:0px;padding:0px;"
            "background-color:red;height:6px;width:" << ((markerCount0-leftTrim0-rightTrim0)>>bitShift) <<
            "px;'></div>"
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << ((maxRightHang+rightTrim0)>>bitShift) <<
            "px;'></div>"

            // Oriented read 1.
            "<br>"
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << ((maxLeftHang+leftTrim0-leftTrim1)>>bitShift) <<
            "px;'></div>"
            "<div title='Oriented read " << orientedReadId1 <<
            "' style='display:inline-block;margin:0px;padding:0px;"
            "background-color:green;height:6px;width:" << (markerCount1>>bitShift) <<
            "px;'></div>"
            "<div style='display:inline-block;margin:0px;padding:0px;"
            "background-color:white;height:6px;width:" << ((maxRightHang+rightTrim0-rightTrim1)>>bitShift) <<
            "px;'></div>"
             ;
    }

    html << "</table>";
}



void Assembler::exploreAlignment(
    const vector<string>& request,
    ostream& html)
{
    // Get the read ids and strands from the request.
    ReadId readId0 = 0;
    const bool readId0IsPresent = getParameterValue(request, "readId0", readId0);
    Strand strand0 = 0;
    const bool strand0IsPresent = getParameterValue(request, "strand0", strand0);
    ReadId readId1 = 0;
    const bool readId1IsPresent = getParameterValue(request, "readId1", readId1);
    Strand strand1 = 0;
    const bool strand1IsPresent = getParameterValue(request, "strand1", strand1);

    // Get alignment parameters.
    size_t maxSkip = 30;
    getParameterValue(request, "maxSkip", maxSkip);
    uint32_t maxMarkerFrequency = 10;
    getParameterValue(request, "maxMarkerFrequency", maxMarkerFrequency);

    // Write the form.
    html <<
        "<form>"
        "<input type=submit value='Compute a marker alignment'> of these two reads:"
        "<br><input type=text name=readId0 required size=8 " <<
        (readId0IsPresent ? "value="+to_string(readId0) : "") <<
        " title='Enter a read id between 0 and " << reads.size()-1 << "'>"
        " on strand ";
    writeStrandSelection(html, "strand0", strand0IsPresent && strand0==0, strand0IsPresent && strand0==1);
    html <<
         "<br><input type=text name=readId1 required size=8 " <<
         (readId1IsPresent ? "value="+to_string(readId1) : "") <<
         " title='Enter a read id between 0 and " << reads.size()-1 << "'>"
        " on strand ";
    writeStrandSelection(html, "strand1", strand1IsPresent && strand1==0, strand1IsPresent && strand1==1);
    html <<
        "<br>Maximum ordinal skip allowed: " <<
        "<input type=text name=maxSkip required size=8 value=" << maxSkip << ">";
    html <<
        "<br>Maximum k-mer frequency: " <<
        "<input type=text name=maxMarkerFrequency required size=8 value=" << maxMarkerFrequency << ">";
    html << "</form>";

    // If the readId's or strand's are missing, stop here.
    if(!readId0IsPresent || !strand0IsPresent || !readId1IsPresent || !strand1IsPresent) {
        return;
    }



    // Page title.
    const OrientedReadId orientedReadId0(readId0, strand0);
    const OrientedReadId orientedReadId1(readId1, strand1);
    html <<
        "<h1>Marker alignment of oriented reads " <<
        "<a href='exploreRead?readId=" << readId0 << "&strand=" << strand0 << "'>" << orientedReadId0 << "</a>" <<
        " and " <<
        "<a href='exploreRead?readId=" << readId1 << "&strand=" << strand1 << "'>" << orientedReadId1 << "</a>" <<
        "</h1>"
        "<p>This alignment was computed allowing a skip of up to " << maxSkip << " markers "
        "and considering only marker k-mers that appear up to " << maxMarkerFrequency <<
        " times in each oriented read.";



    // Compute the alignment.
    // This creates file Alignment.png.
    array<vector<MarkerWithOrdinal>, 2> markersSortedByKmerId;
    getMarkersSortedByKmerId(orientedReadId0, markersSortedByKmerId[0]);
    getMarkersSortedByKmerId(orientedReadId1, markersSortedByKmerId[1]);
    AlignmentGraph graph;
    Alignment alignment;
    AlignmentInfo alignmentInfo;
    const bool debug = true;
    alignOrientedReads(
        markersSortedByKmerId,
        maxSkip, maxMarkerFrequency, debug, graph, alignment, alignmentInfo);
    if(alignment.ordinals.empty()) {
        html << "<p>The alignment is empty (it has no markers).";
        return;
    }



    // Write out a table with some information on the alignment.
    const auto markers0 = markers[orientedReadId0.getValue()];
    const auto markers1 = markers[orientedReadId1.getValue()];
    const auto markerCount0 = markers0.size();
    const auto markerCount1 = markers1.size();
    const auto baseCount0 = reads[orientedReadId0.getReadId()].baseCount;
    const auto baseCount1 = reads[orientedReadId1.getReadId()].baseCount;
    const auto firstOrdinal0 = alignment.ordinals.front()[0];
    const auto firstOrdinal1 = alignment.ordinals.front()[1];
    const auto lastOrdinal0 = alignment.ordinals.back()[0];
    const auto lastOrdinal1 = alignment.ordinals.back()[1];
    const auto& firstMarker0 = markers0[firstOrdinal0];
    const auto& firstMarker1 = markers1[firstOrdinal1];
    const auto& lastMarker0 = markers0[lastOrdinal0];
    const auto& lastMarker1 = markers1[lastOrdinal1];
    html <<
        "<h3>Alignment summary</h3>"
        "<table>"
        "<tr>"
        "<th rowspan=2>"
        "<th colspan=2>Markers"
        "<th colspan=2>Bases"

        "<tr>"
        "<th>" << orientedReadId0 <<
        "<th>" << orientedReadId1 <<
        "<th>" << orientedReadId0 <<
        "<th>" << orientedReadId1 <<

        "<tr>"
        "<td title='Total number of markers or bases in this read'>Total"
        "<td class=centered>" << markerCount0 <<
        "<td class=centered>" << markerCount1 <<
        "<td class=centered>" << baseCount0 <<
        "<td class=centered>" << baseCount1 <<

        "<tr>"
        "<td title='Number of unaligned markers or bases to the left of the aligned portion'>Unaligned on left"
        "<td class=centered>" << firstOrdinal0 <<
        "<td class=centered>" << firstOrdinal1 <<
        "<td class=centered>" << firstMarker0.position <<
        "<td class=centered>" << firstMarker1.position <<

        "<tr>"
        "<td title='Number of unaligned markers or bases to the right of the aligned portion'>Unaligned on right"
        "<td class=centered>" << markerCount0 - 1 - lastOrdinal0 <<
        "<td class=centered>" << markerCount1 - 1 - lastOrdinal1 <<
        "<td class=centered>" << baseCount0 - 1 - lastMarker0.position <<
        "<td class=centered>" << baseCount1 - 1 - lastMarker1.position <<

        "<tr>"
        "<td title='Number of aligned markers or bases in the aligned portion'>Aligned range"
        "<td class=centered>" << lastOrdinal0 + 1 - firstOrdinal0 <<
        "<td class=centered>" << lastOrdinal1 + 1 - firstOrdinal1 <<
        "<td class=centered>" << lastMarker0.position + 1 - firstMarker0.position <<
        "<td class=centered>" << lastMarker1.position + 1 - firstMarker1.position <<

        "<tr>"
        "<td title='Number of aligned markers'>Aligned"
        "<td class=centered>" << alignment.ordinals.size() <<
        "<td class=centered>" << alignment.ordinals.size() <<
        "<td>"
        "<td>"

        "<tr>"
        "<td title='Fraction of aligned markers in the aligned portion'>Aligned fraction"
        "<td class=centered>" << std::setprecision(2) << double(alignment.ordinals.size()) / double(lastOrdinal0 + 1 - firstOrdinal0) <<
        "<td class=centered>" << std::setprecision(2) << double(alignment.ordinals.size()) / double(lastOrdinal1 + 1 - firstOrdinal1) <<
        "<td>"
        "<td>"

        "</table>"
        "<p>See bottom of this page for alignment details.";



    // Create a base64 version of the png file.
    const string command = "base64 Alignment.png > Alignment.png.base64";
    ::system(command.c_str());


    // Write out the picture with the alignment.
    html <<
        "<h3>Alignment matrix</h3>"
        "<p>In the picture, horizontal positions correspond to marker ordinals on " <<
        orientedReadId0 << " (marker 0 is on left) "
        "and vertical positions correspond to marker ordinals on " <<
        orientedReadId1 << " (marker 0 is on top). "
        "Each faint line corresponds to 10 markers.";
    html << "<p><img src=\"data:image/png;base64,";
    ifstream png("Alignment.png.base64");
    html << png.rdbuf();
    html << "\"/>";



    // Write out details of the alignment.
    html <<
        "<h3>Alignment details</h3>"
        "<table>"

        "<tr>"
        "<th rowspan=2>K-mer"
        "<th colspan=2>Ordinals"
        "<th colspan=2>Positions"

        "<tr>"
        "<th>" << orientedReadId0 <<
        "<th>" << orientedReadId1 <<
        "<th>" << orientedReadId0 <<
        "<th>" << orientedReadId1;

    for(const auto& ordinals: alignment.ordinals) {
        const auto ordinal0 = ordinals[0];
        const auto ordinal1 = ordinals[1];
        const auto& marker0 = markers0[ordinal0];
        const auto& marker1 = markers1[ordinal1];
        const auto kmerId = marker0.kmerId;
        CZI_ASSERT(marker1.kmerId == kmerId);
        const Kmer kmer(kmerId, assemblerInfo->k);

        html << "<tr><td style='font-family:monospace'>";
        kmer.write(html, assemblerInfo->k);
        html <<

            "<td class=centered>"
            "<a href=\"exploreRead?readId=" << orientedReadId0.getReadId() <<
            "&amp;strand=" << orientedReadId0.getStrand() <<
            "&amp;highlightMarker=" << ordinal0 <<
            "#" << ordinal0 << "\">" << ordinal0 << "</a>"

            "<td class=centered>"
            "<a href=\"exploreRead?readId=" << orientedReadId1.getReadId() <<
            "&amp;strand=" << orientedReadId1.getStrand() <<
            "&amp;highlightMarker=" << ordinal1 <<
            "#" << ordinal1 << "\">" << ordinal1 << "</a>"

            "<td class=centered>" << marker0.position <<
            "<td class=centered>" << marker1.position;

    }

    html << "</table>";
}



// Display a base-by-base alignment matrix between two given sequences.
void Assembler::displayAlignmentMatrix(
    const vector<string>& request,
    ostream& html)
{
    html << "<h1>Base-by-base alignment of two sequences</h1>"
        "<p>This page does not use run-length representation of sequences. "
        "It also does not use markers. "
        "Alignments computed and displayed here are standard "
        "base-by-base alignments.";

    // Get the request parameters.
    string sequenceString0;
    getParameterValue(request, "sequence0", sequenceString0);
    string sequenceString1;
    getParameterValue(request, "sequence1", sequenceString1);
    int zoom = 1;
    getParameterValue(request, "zoom", zoom);
    string clip0String;
    getParameterValue(request, "clip0", clip0String);
    const bool clip0 = (clip0String == "on");
    string clip1String;
    getParameterValue(request, "clip1", clip1String);
    const bool clip1 = (clip1String == "on");
    string showAlignmentString;
    getParameterValue(request, "showAlignment", showAlignmentString);
    const bool showAlignment = (showAlignmentString == "on");
    string showGridString;
    getParameterValue(request, "showGrid", showGridString);
    const bool showGrid = (showGridString == "on");


    // Get the zoom factor.

    // Write the form.
    html <<
        "<p>Display a base-by-base alignment of these two sequences:"
        "<form>"
        "<input style='font-family:monospace' type=text name=sequence0 required size=64 value='" << sequenceString0 << "'>"
        "<br><input style='font-family:monospace' type=text name=sequence1 required size=64 value='" << sequenceString1 << "'>"
        "<br><input type=checkbox name=clip0" << (clip0 ? " checked" : "") << "> Allow clipping on both ends of first sequence."
        "<br><input type=checkbox name=clip1" << (clip1 ? " checked" : "") << "> Allow clipping on both ends of second sequence."
        "<br><input type=checkbox name=showAlignment" << (showAlignment ? " checked" : "") << "> Show the alignment and highlight it in the alignment matrix."
        "<br><input type=checkbox name=showGrid" << (showGrid ? " checked" : "") << "> Show a grid on the alignment matrix."
        "<br>Zoom factor: <input type=text name=zoom required value=" << zoom << ">"
        "<br><input type=submit value='Display'>"
        "</form>";

    // If either sequence is empty, do nothing.
    if(sequenceString0.empty() || sequenceString1.empty()) {
        return;
    }

    // Convert to base sequences, discarding all characters that
    // don't represent a base.
    vector<Base> sequence0;
    for(const char c: sequenceString0) {
        try {
            const Base b = Base::fromCharacter(c);
            sequence0.push_back(b);
        } catch (std::exception) {
            // Just discard the character.
        }
    }
    vector<Base> sequence1;
    for(const char c: sequenceString1) {
        try {
            const Base b = Base::fromCharacter(c);
            sequence1.push_back(b);
        } catch (std::exception) {
            // Just discard the character.
        }
    }

    // If either sequence is empty, do nothing.
    if(sequenceString0.empty() || sequenceString1.empty()) {
        return;
    }
    if(sequence0.empty() || sequence1.empty()) {
        return;
    }



    // If getting here, we have two non-empty sequences and
    // we can display they alignment matrix.

    using namespace boost::gil;

    // Create the image and the view.
    const size_t n0 = sequence0.size();
    const size_t n1 = sequence1.size();
    rgb8_image_t image(n0*zoom, n1*zoom);
    rgb8_image_t::view_t imageView = view(image);

    // Initialize it to black.
    const rgb8_pixel_t black(0, 0, 0);
    for(size_t i0=0; i0<n0*zoom; i0++) {
        for(size_t i1=0; i1<n1*zoom; i1++) {
            imageView(i0, i1) = black;
        }
    }



    // Display a position grid.
    if(showGrid) {

        // Every 10.
        const rgb8_pixel_t grey10(128, 128, 128);
        for(size_t i0=0; i0<n0; i0+=10) {
            for(size_t i1=0; i1<n1; i1++) {
                const size_t begin0 = i0 *zoom;
                const size_t end0 = begin0 + zoom;
                const size_t begin1 = i1 *zoom;
                const size_t end1 = begin1 + zoom;
                for(size_t j0=begin0; j0!=end0; j0++) {
                    for(size_t j1=begin1; j1!=end1; j1++) {
                        imageView(j0, j1) = grey10;
                    }
                }
            }
        }
        for(size_t i1=0; i1<n1; i1+=10) {
            for(size_t i0=0; i0<n0; i0++) {
                const size_t begin0 = i0 *zoom;
                const size_t end0 = begin0 + zoom;
                const size_t begin1 = i1 *zoom;
                const size_t end1 = begin1 + zoom;
                for(size_t j0=begin0; j0!=end0; j0++) {
                    for(size_t j1=begin1; j1!=end1; j1++) {
                        imageView(j0, j1) = grey10;
                    }
                }
            }
        }

        // Every 100.
        const rgb8_pixel_t grey100(192,192,192);
        for(size_t i0=0; i0<n0; i0+=100) {
            for(size_t i1=0; i1<n1; i1++) {
                const size_t begin0 = i0 *zoom;
                const size_t end0 = begin0 + zoom;
                const size_t begin1 = i1 *zoom;
                const size_t end1 = begin1 + zoom;
                for(size_t j0=begin0; j0!=end0; j0++) {
                    for(size_t j1=begin1; j1!=end1; j1++) {
                        imageView(j0, j1) = grey100;
                    }
                }
            }
        }
        for(size_t i1=0; i1<n1; i1+=100) {
            for(size_t i0=0; i0<n0; i0++) {
                const size_t begin0 = i0 *zoom;
                const size_t end0 = begin0 + zoom;
                const size_t begin1 = i1 *zoom;
                const size_t end1 = begin1 + zoom;
                for(size_t j0=begin0; j0!=end0; j0++) {
                    for(size_t j1=begin1; j1!=end1; j1++) {
                        imageView(j0, j1) = grey100;
                    }
                }
            }
        }
    }



    // Fill in pixel values.
    const rgb8_pixel_t green(0, 255, 0);
    for(size_t i0=0; i0<n0; i0++) {
        const Base base0 = sequence0[i0];
        const size_t begin0 = i0 *zoom;
        const size_t end0 = begin0 + zoom;
        for(size_t i1=0; i1<n1; i1++) {
            const Base base1 = sequence1[i1];
            if(!(base1 == base0)) {
                continue;
            }
            const size_t begin1 = i1 *zoom;
            const size_t end1 = begin1 + zoom;
            for(size_t j0=begin0; j0!=end0; j0++) {
                for(size_t j1=begin1; j1!=end1; j1++) {
                    imageView(j0, j1) = green;
                }
            }
        }
    }



    // Use SeqAn to compute an alignment free at both ends
    // and highlight it in the image.
    // https://seqan.readthedocs.io/en/master/Tutorial/Algorithms/Alignment/PairwiseSequenceAlignment.html
    if(showAlignment){
        using namespace seqan;
        using seqan::Alignment; // Hide shasta::Alignment.

        typedef String<char> TSequence;
        typedef StringSet<TSequence> TStringSet;
        typedef StringSet<TSequence, Dependent<> > TDepStringSet;
        typedef Graph<Alignment<TDepStringSet> > TAlignGraph;
        // typedef Align<TSequence, ArrayGaps> TAlign;

        TSequence seq0;
        for(const Base b: sequence0) {
            appendValue(seq0, b.character());
        }
        TSequence seq1;
        for(const Base b: sequence1) {
            appendValue(seq1, b.character());
        }

        TStringSet sequences;
        appendValue(sequences, seq0);
        appendValue(sequences, seq1);


        // Call the globalAlignment with AlignConfig arguments
        // determined by clip0 and clip1.
        TAlignGraph graph(sequences);
        int score;
        if(clip0) {
            if(clip1) {
                score = globalAlignment(
                    graph,
                    Score<int, Simple>(1, -1, -1),
                    AlignConfig<true, true, true, true>(),
                    LinearGaps());
            } else {
                score = globalAlignment(
                    graph,
                    Score<int, Simple>(1, -1, -1),
                    AlignConfig<true, false, true, false>(),
                    LinearGaps());
            }
        } else {
            if(clip1) {
                score = globalAlignment(
                    graph,
                    Score<int, Simple>(1, -1, -1),
                    AlignConfig<false, true, false, true>(),
                    LinearGaps());
            } else {
                score = globalAlignment(
                    graph,
                    Score<int, Simple>(1, -1, -1),
                    AlignConfig<false, false, false, false>(),
                    LinearGaps());
            }

        }



        // Extract the alignment from the graph.
        // This creates a single sequence consisting of the two rows
        // of the alignment, concatenated.
        TSequence align;
        convertAlignment(graph, align);
        const size_t totalAlignmentLength = seqan::length(align);
        CZI_ASSERT((totalAlignmentLength % 2) == 0);    // Because we are aligning two sequences.
        const size_t alignmentLength = totalAlignmentLength / 2;

        // Extract the two rows of the alignment.
        array<vector<AlignedBase>, 2> alignment;
        alignment[0].resize(alignmentLength);
        alignment[1].resize(alignmentLength);
        for(size_t i=0; i<alignmentLength; i++) {
            alignment[0][i] = AlignedBase::fromCharacter(align[i]);
            alignment[1][i] = AlignedBase::fromCharacter(align[i + alignmentLength]);
        }
        html << "<br>Sequence lengths: " << n0 << " " << n1 <<
            "<br>Optimal alignment has length " << alignmentLength <<
            ", score " << score <<
            ":<div style='font-family:monospace'>";
        for(size_t i=0; i<2; i++) {
            html << "<br>";
            for(size_t j=0; j<alignmentLength; j++) {
                html << alignment[i][j];
            }
        }
        html << "</div>";


        size_t i0 = 0;
        size_t i1 = 0;
        const rgb8_pixel_t red(255, 0, 0);
        const rgb8_pixel_t yellow(255, 255, 0);
        for(size_t position=0; position<alignmentLength; position++) {
            const AlignedBase b0 = alignment[0][position];
            const AlignedBase b1 = alignment[1][position];

            if(!(b0.isGap() || b1.isGap())) {

                // This pixel is part of the optimal alignment
                const size_t begin0 = i0 *zoom;
                const size_t end0 = begin0 + zoom;
                const size_t begin1 = i1 *zoom;
                const size_t end1 = begin1 + zoom;
                for(size_t j0=begin0; j0!=end0; j0++) {
                    for(size_t j1=begin1; j1!=end1; j1++) {
                        imageView(j0, j1) = (b0==b1 ? red : yellow);
                    }
                }
            }

            if(!b0.isGap()) {
                ++i0;
            }
            if(!b1.isGap()) {
                ++i1;
            }
        }

    }


    // Write it out.
    png_write_view("AlignmentMatrix.png", imageView);

    // Create a base64 version of the png file.
    const string command = "base64 AlignmentMatrix.png > AlignmentMatrix.png.base64";
    ::system(command.c_str());


    // Write out the png file.
    html << "<p><img src=\"data:image/png;base64,";
    ifstream png("AlignmentMatrix.png.base64");
    html << png.rdbuf();
    html << "\"/>";

}



// Compute alignments on an oriented read against
// all other oriented reads.
void Assembler::computeAllAlignments(
    const vector<string>& request,
    ostream& html)
{
    // Get the read id and strand from the request.
    ReadId readId0 = 0;
    const bool readId0IsPresent = getParameterValue(request, "readId0", readId0);
    Strand strand0 = 0;
    const bool strand0IsPresent = getParameterValue(request, "strand0", strand0);

    // Get alignment parameters.
    computeAllAlignmentsData.minMarkerCount = 0;
    getParameterValue(request, "minMarkerCount", computeAllAlignmentsData.minMarkerCount);
    computeAllAlignmentsData.maxSkip = 30;
    getParameterValue(request, "maxSkip", computeAllAlignmentsData.maxSkip);
    computeAllAlignmentsData.maxMarkerFrequency = 10;
    getParameterValue(request, "maxMarkerFrequency", computeAllAlignmentsData.maxMarkerFrequency);
    computeAllAlignmentsData.minAlignedMarkerCount = 100;
    getParameterValue(request, "minAlignedMarkerCount", computeAllAlignmentsData.minAlignedMarkerCount);
    computeAllAlignmentsData.maxTrim = 30;
    getParameterValue(request, "maxTrim", computeAllAlignmentsData.maxTrim);


    // Write the form.
    html <<
        "<form>"
        "<input type=submit value='Compute marker alignments'> of oriented read"
        " <input type=text name=readId0 required size=8 " <<
        (readId0IsPresent ? "value="+to_string(readId0) : "") <<
        " title='Enter a read id between 0 and " << reads.size()-1 << "'>"
        " on strand ";
    writeStrandSelection(html, "strand0", strand0IsPresent && strand0==0, strand0IsPresent && strand0==1);
    html <<
        " against all other oriented reads with at least"
        "<input type=text name=minMarkerCount size=8 value=" << computeAllAlignmentsData.minMarkerCount <<
        "> markers.<br>Maximum ordinal skip allowed: " <<
        "<input type=text name=maxSkip required size=8 value=" << computeAllAlignmentsData.maxSkip << ">"
        "<br>Maximum marker frequency: " <<
        "<input type=text name=maxMarkerFrequency required size=8 value=" <<
        computeAllAlignmentsData.maxMarkerFrequency << ">"
        "<br>Minimum number of aligned markers"
        "<input type=text name=minAlignedMarkerCount required size=8 value=" <<
        computeAllAlignmentsData.minAlignedMarkerCount << ">"
        "<br>Maximum number of trimmed markers"
        "<input type=text name=maxTrim required size=8 value=" <<
        computeAllAlignmentsData.maxTrim << ">"
        "</form>";



    // If the readId or strand are missing, stop here.
    if(!readId0IsPresent || !strand0IsPresent) {
        return;
    }
    const OrientedReadId orientedReadId0(readId0, strand0);

    // Vectors to contain markers sorted by kmerId.
    vector<MarkerWithOrdinal> markers0SortedByKmerId;
    vector<MarkerWithOrdinal> markers1SortedByKmerId;
    getMarkersSortedByKmerId(orientedReadId0, markers0SortedByKmerId);


    // Compute the alignments in parallel.
    computeAllAlignmentsData.orientedReadId0 = orientedReadId0;
    const size_t threadCount =std::thread::hardware_concurrency();
    computeAllAlignmentsData.threadAlignments.resize(threadCount);
    const size_t batchSize = 1000;
    setupLoadBalancing(reads.size(), batchSize);
    const auto t0 = std::chrono::steady_clock::now();
    runThreads(&Assembler::computeAllAlignmentsThreadFunction, threadCount);
    const auto t1 = std::chrono::steady_clock::now();
    html << "<p>Alignment computation using " << threadCount << " threads took " <<
        1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)).count()) << "s.";

    // Gather the alignments found by each thread.
    vector< pair<OrientedReadId, AlignmentInfo> > alignments;
    for(size_t threadId=0; threadId<threadCount; threadId++) {
        const vector< pair<OrientedReadId, AlignmentInfo> >& threadAlignments =
            computeAllAlignmentsData.threadAlignments[threadId];
        copy(threadAlignments.begin(), threadAlignments.end(), back_inserter(alignments));
    }
    computeAllAlignmentsData.threadAlignments.clear();
    sort(alignments.begin(), alignments.end(),
        OrderPairsByFirstOnly<OrientedReadId, AlignmentInfo>());

#if 0
    // Reusable data structures for alignOrientedReads.
    AlignmentGraph graph;
    Alignment alignment;


    // Loop over oriented reads.
    // Eventually this should be multithreaded if we want to use
    // it for large assemblies.
    size_t computedAlignmentCount = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for(ReadId readId1=0; readId1<reads.size(); readId1++) {
        if((readId1 % 10000) == 0) {
            cout << timestamp << readId1 << "/" << reads.size() << " " << alignments.size() << endl;
        }
        for(Strand strand1=0; strand1<2; strand1++) {

            // Skip alignments with self in the same orientation.
            // Allow alignment with self reverse complemented.
            if(readId0==readId1 && strand0==strand1) {
                continue;
            }

            // If this read has less than the required number of markers, skip.
            const OrientedReadId orientedReadId1(readId1, strand1);
            if(markers[orientedReadId1.getValue()].size() < minMarkerCount) {
                continue;
            }

            // Get markers sorted by kmer id.
            getMarkersSortedByKmerId(orientedReadId1, markers1SortedByKmerId);

            // Compute the alignment.
            ++computedAlignmentCount;
            const bool debug = false;
            alignOrientedReads(
                markers0SortedByKmerId,
                markers1SortedByKmerId,
                maxSkip, maxVertexCountPerKmer, debug, graph, alignment);

            // If the alignment has too few markers skip it.
            if(alignment.ordinals.size() < minAlignedMarkerCount) {
                continue;
            }

            // Compute the AlignmentInfo.
            AlignmentInfo alignmentInfo;
            alignmentInfo.create(alignment);

            // If the alignment has too much trim, skip it.
            uint32_t leftTrim;
            uint32_t rightTrim;
            tie(leftTrim, rightTrim) = computeTrim(
                orientedReadId0,
                orientedReadId1,
                alignmentInfo);
            if(leftTrim>maxTrim || rightTrim>maxTrim) {
                continue;
            }
            alignments.push_back(make_pair(orientedReadId1, alignmentInfo));
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    html << "<p>Computed " << computedAlignmentCount << " alignments.";
    html << "<p>Found " << alignments.size() <<
        " alignments satisfying the given criteria.";
    html << "<p>Alignment computation took " <<
        1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)).count()) << "s.";
#endif

    // Now we can display the alignments.
    html << "<p>Found " << alignments.size() <<
        " alignments satisfying the given criteria.";
    displayAlignments(orientedReadId0, alignments, html);
}



void Assembler::computeAllAlignmentsThreadFunction(size_t threadId)
{
    // Get the first oriented read.
    const OrientedReadId orientedReadId0 = computeAllAlignmentsData.orientedReadId0;
    const ReadId readId0 = orientedReadId0.getReadId();
    const Strand strand0 = orientedReadId0.getStrand();

    // Get parameters for alignment computation.
    const size_t minMarkerCount = computeAllAlignmentsData.minMarkerCount;
    const size_t maxSkip = computeAllAlignmentsData.maxSkip;
    const uint32_t maxMarkerFrequency = computeAllAlignmentsData.maxMarkerFrequency;
    const size_t minAlignedMarkerCount =computeAllAlignmentsData.minAlignedMarkerCount;
    const size_t maxTrim =computeAllAlignmentsData.maxTrim;

    // Vector where this thread will store the alignments it finds.
    vector< pair<OrientedReadId, AlignmentInfo> >& alignments =
        computeAllAlignmentsData.threadAlignments[threadId];

    // Reusable data structures for alignOrientedReads.
    AlignmentGraph graph;
    Alignment alignment;
    AlignmentInfo alignmentInfo;

    // Vectors to contain markers sorted by kmerId.
    array<vector<MarkerWithOrdinal>, 2> markersSortedByKmerId;
    getMarkersSortedByKmerId(orientedReadId0, markersSortedByKmerId[0]);

    // Loop over batches assigned to this thread.
    uint64_t begin, end;
    while(getNextBatch(begin, end)) {

        // Loop over reads assigned to this batch.
        for(ReadId readId1=ReadId(begin); readId1!=ReadId(end); ++readId1) {

            // Loop over strands.
            for(Strand strand1=0; strand1<2; strand1++) {

                // Skip alignments with self in the same orientation.
                // Allow alignment with self reverse complemented.
                if(readId0==readId1 && strand0==strand1) {
                    continue;
                }

                // If this read has less than the required number of markers, skip.
                const OrientedReadId orientedReadId1(readId1, strand1);
                if(markers[orientedReadId1.getValue()].size() < minMarkerCount) {
                    continue;
                }

                // Get markers sorted by kmer id.
                getMarkersSortedByKmerId(orientedReadId1, markersSortedByKmerId[1]);

                // Compute the alignment.
                const bool debug = false;
                alignOrientedReads(
                    markersSortedByKmerId,
                    maxSkip, maxMarkerFrequency, debug, graph, alignment, alignmentInfo);

                // If the alignment has too few markers skip it.
                if(alignment.ordinals.size() < minAlignedMarkerCount) {
                    continue;
                }

                // If the alignment has too much trim, skip it.
                uint32_t leftTrim;
                uint32_t rightTrim;
                tie(leftTrim, rightTrim) = alignmentInfo.computeTrim();
                if(leftTrim>maxTrim || rightTrim>maxTrim) {
                    continue;
                }
                alignments.push_back(make_pair(orientedReadId1, alignmentInfo));
            }
        }
    }
}



void Assembler::exploreAlignmentGraph(
    const vector<string>& request,
    ostream& html)
{
    // Get the parameters.
    ReadId readId = 0;
    const bool readIdIsPresent = getParameterValue(request, "readId", readId);

    Strand strand = 0;
    const bool strandIsPresent = getParameterValue(request, "strand", strand);

    size_t minAlignedMarkerCount = 100;
    getParameterValue(request, "minAlignedMarkerCount", minAlignedMarkerCount);

    size_t maxTrim = 30;
    getParameterValue(request, "maxTrim", maxTrim);

    uint32_t maxDistance = 2;
    getParameterValue(request, "maxDistance", maxDistance);

    uint32_t sizePixels = 1200;
    getParameterValue(request, "sizePixels", sizePixels);

    double timeout= 30;
    getParameterValue(request, "timeout", timeout);



    // Write the form.
    html <<
        "<h3>Display a local subgraph of the <a href='docs/ReadGraph.html'>global alignment graph</a></h3>"
        "<form>"

        "<table>"

        "<tr title='Read id between 0 and " << reads.size()-1 << "'>"
        "<td>Read id"
        "<td><input type=text required name=readId size=8 style='text-align:center'"
        << (readIdIsPresent ? ("value='"+to_string(readId)+"'") : "") <<
        ">"

        "<tr title='Choose 0 (+) for the input read or 1 (-) for its reverse complement'>"
        "<td>Strand"
        "<td class=centered>";
    writeStrandSelection(html, "strand", strandIsPresent && strand==0, strandIsPresent && strand==1);

    html <<

        "<tr title='Maximum distance from start vertex (number of edges)'>"
        "<td>Maximum distance"
        "<td><input type=text required name=maxDistance size=8 style='text-align:center'"
        " value='" << maxDistance <<
        "'>"

        "<tr title='The minimum number of aligned markers "
        "in order for an edge to be generated'>"
        "<td>Minimum number of aligned markers"
        "<td><input type=text required name=minAlignedMarkerCount size=8 style='text-align:center'"
        " value='" << minAlignedMarkerCount <<
        "'>"

        "<tr title='The maximum number of trimmed bases on either side "
        "in order for an edge to be generated'>"
        "<td>Minimum alignment trim"
        "<td><input type=text required name=maxTrim size=8 style='text-align:center'"
        " value='" << maxTrim <<
        "'>"

        "<tr title='Graphics size in pixels. "
        "Changing this works better than zooming. Make it larger if the graph is too crowded."
        " Ok to make it much larger than screen size.'>"
        "<td>Graphics size in pixels"
        "<td><input type=text required name=sizePixels size=8 style='text-align:center'" <<
        " value='" << sizePixels <<
        "'>"

        "<tr title='Maximum time (in seconds) allowed for graph creation and layout'>"
        "<td>Timeout (seconds) for graph creation and layout"
        "<td><input type=text required name=timeout size=8 style='text-align:center'" <<
        " value='" << timeout <<
        "'>"

        "</table>"

        "<input type=submit value='Display'>"
        "</form>";



    // If any necessary values are missing, stop here.
    if(!readIdIsPresent || !strandIsPresent) {
        return;
    }



    // Validity checks.
    if(readId > reads.size()) {
        html << "<p>Invalid read id " << readId;
        html << ". Must be between 0 and " << reads.size()-1 << ".";
        return;
    }
    if(strand>1) {
        html << "<p>Invalid strand " << strand;
        html << ". Must be 0 or 1.";
        return;
    }
    const OrientedReadId orientedReadId(readId, strand);



    // Create the local alignment graph.
    LocalAlignmentGraph graph;
    const auto createStartTime = steady_clock::now();
    if(!createLocalAlignmentGraph(orientedReadId,
        minAlignedMarkerCount, maxTrim, maxDistance, timeout, graph)) {
        html << "<p>Timeout for graph creation exceeded. Increase the timeout or reduce the maximum distance from the start vertex.";
        return;
    }
    const auto createFinishTime = steady_clock::now();

    // Write it out in graphviz format.
    const string uuid = to_string(boost::uuids::random_generator()());
    const string dotFileName = "/dev/shm/" + uuid + ".dot";
    graph.write(dotFileName, maxDistance);

    // Compute layout in svg format.
    const string command =
        "timeout " + to_string(timeout - seconds(createFinishTime - createStartTime)) +
        " sfdp -O -T svg " + dotFileName +
        " -Gsize=" + to_string(sizePixels/72.);
    const auto layoutStartTime = steady_clock::now();
    const int commandStatus = ::system(command.c_str());
    const auto layoutFinishTime = steady_clock::now();
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
    filesystem::remove(dotFileName);



    // Write a title and display the graph.
    html <<
        "<h1 style='line-height:10px'>Alignment graph near oriented read " << orientedReadId << "</h1>"
        "Color legend: "
        "<span style='background-color:LightGreen'>start vertex</span> "
        "<span style='background-color:cyan'>vertices at maximum distance (" << maxDistance <<
        ") from the start vertex</span>.";


    // Display the graph.
    const string svgFileName = dotFileName + ".svg";
    ifstream svgFile(svgFileName);
    html << svgFile.rdbuf();
    svgFile.close();



    // Add to each vertex a cursor that shows you can click on it.
    html <<
        "<script>"
        "var vertices = document.getElementsByClassName('node');"
        "for (var i=0;i<vertices.length; i++) {"
        "    vertices[i].style.cursor = 'pointer';"
        "}"
        "</script>";



    // Remove the .svg file.
    filesystem::remove(svgFileName);

    // Write additional graph information.
    html <<
        "<br>This portion of the alignment graph has " << num_vertices(graph) <<
        " vertices and " << num_edges(graph) << " edges." <<
        "<br>Graph creation took " <<
        std::setprecision(2) << seconds(createFinishTime-createStartTime) <<
        " s.<br>Graph layout took " <<
        std::setprecision(2) << seconds(layoutFinishTime-layoutStartTime) << " s.";

    // Write a histogram of the number of vertices by distance.
    vector<int> histogram(maxDistance+1, 0);
    BGL_FORALL_VERTICES(v, graph, LocalAlignmentGraph) {
        ++histogram[graph[v].distance];
    }
    html <<
        "<h4>Vertex count by distance from start vertex</h4>"
        "<table>"
        "<tr><th>Distance<th>Count";
    for(uint32_t distance=0; distance<=maxDistance; distance++) {
        html << "<tr><td class=centered>" << distance << "<td class=centered>" << histogram[distance];
    }
    html << "</table>";

}



void Assembler::exploreReadGraph(
    const vector<string>& request,
    ostream& html)
{
    // Get the parameters.
    ReadId readId = 0;
    const bool readIdIsPresent = getParameterValue(request, "readId", readId);

    Strand strand = 0;
    const bool strandIsPresent = getParameterValue(request, "strand", strand);

    uint32_t maxDistance = 2;
    getParameterValue(request, "maxDistance", maxDistance);

    uint32_t maxTrim = 30;
    getParameterValue(request, "maxTrim", maxTrim);

    string allowChimericReadsString;
    const bool allowChimericReads = getParameterValue(request, "allowChimericReads", allowChimericReadsString);

    string allowCrossStrandEdgesString;
    const bool allowCrossStrandEdges = getParameterValue(request, "allowCrossStrandEdges", allowCrossStrandEdgesString);

    uint32_t sizePixels = 1200;
    getParameterValue(request, "sizePixels", sizePixels);

    string format = "png";
    getParameterValue(request, "format", format);

    double timeout= 30;
    getParameterValue(request, "timeout", timeout);

    string addBlastAnnotationsString;
    const bool addBlastAnnotations = getParameterValue(request, "addBlastAnnotations", addBlastAnnotationsString);



    // Write the form.
    html <<
        "<h3>Display a local subgraph of the <a href='docs/ReadGraph.html'>global read graph</a></h3>"
        "<form>"

        "<table>"

        "<tr title='Read id between 0 and " << reads.size()-1 << "'>"
        "<td>Read id"
        "<td><input type=text required name=readId size=8 style='text-align:center'"
        << (readIdIsPresent ? ("value='"+to_string(readId)+"'") : "") <<
        ">";

    html << "<tr><td>Strand<td class=centered>";
        writeStrandSelection(html, "strand", strandIsPresent && strand==0, strandIsPresent && strand==1);



    html <<

        "<tr title='Maximum distance from start vertex (number of edges)'>"
        "<td>Maximum distance"
        "<td><input type=text required name=maxDistance size=8 style='text-align:center'"
        " value='" << maxDistance <<
        "'>"

        "<tr title='Maximum trim (markers) used to define containment'>"
        "<td>Maximum trim"
        "<td><input type=text required name=maxTrim size=8 style='text-align:center'"
        " value='" << maxTrim <<
        "'>"

        "<tr title='Allow reads marked as chimeric to be included in the local read graph.'>"
        "<td>Allow chimeric reads"
        "<td class=centered><input type=checkbox name=allowChimericReads" <<
        (allowChimericReads ? " checked" : "") <<
        ">"

        "<tr title='Allow edges that skip across strands.'>"
        "<td>Allow cross-strand edges"
        "<td class=centered><input type=checkbox name=allowCrossStrandEdges" <<
        (allowCrossStrandEdges ? " checked" : "") <<
        ">"

        "<tr title='Graphics size in pixels. "
        "Changing this works better than zooming. Make it larger if the graph is too crowded."
        " Ok to make it much larger than screen size.'>"
        "<td>Graphics size in pixels"
        "<td><input type=text required name=sizePixels size=8 style='text-align:center'" <<
        " value='" << sizePixels <<
        "'>"

        "<tr>"
        "<td>Graphics format"
        "<td class=centered>"
        "svg <input type=radio required name=format value='svg'" <<
        (format == "svg" ? " checked=on" : "") <<
        ">"
        "<td class=centered>png <input type=radio required name=format value='png'" <<
        (format == "png" ? " checked=on" : "") <<
        ">"

        "<tr title='Maximum time (in seconds) allowed for graph creation and layout'>"
        "<td>Timeout (seconds) for graph creation and layout"
        "<td><input type=text required name=timeout size=8 style='text-align:center'" <<
        " value='" << timeout <<
        "'>"

        "<tr title='Add to each vertex tooltip summary information on the best alignment to the reference'>"
        "<td>Add Blast annotations"
        "<td class=centered><input type=checkbox name=addBlastAnnotations" <<
        (addBlastAnnotations ? " checked" : "") <<
        ">"

        "</table>"

        "<input type=submit value='Display'>"
        "</form>";



    // If any necessary values are missing, stop here.
    if(! (readIdIsPresent && strandIsPresent)) {
        return;
    }



    // Validity checks.
    if(readId > reads.size()) {
        html << "<p>Invalid read id " << readId;
        html << ". Must be between 0 and " << reads.size()-1 << ".";
        return;
    }



    // Create the local read graph.
    LocalReadGraph graph;
    const auto createStartTime = steady_clock::now();
    if(!createLocalReadGraph(OrientedReadId(readId, strand),
        maxDistance, allowChimericReads, allowCrossStrandEdges, maxTrim, timeout, graph)) {
        html << "<p>Timeout for graph creation exceeded. Increase the timeout or reduce the maximum distance from the start vertex.";
        return;
    }
    html << "<p>The local read graph has " << num_vertices(graph);
    html << " vertices and " << num_edges(graph) << " edges.";
    const auto createFinishTime = steady_clock::now();



    // Add Blast annotations, if requested.
    if(addBlastAnnotations) {

        // Create a fasta file containing the sequences of all the oriented reads
        // in the local read graph.
        const string uuid = to_string(boost::uuids::random_generator()());
        const string fastaFileName = "/dev/shm/" + uuid + ".fa";
        ofstream fastaFile(fastaFileName);
        BGL_FORALL_VERTICES(v, graph, LocalReadGraph) {
            const LocalReadGraphVertex& vertex = graph[v];
            const vector<Base> sequence = getOrientedReadRawSequence(vertex.orientedReadId);
            const auto readName = readNames[vertex.orientedReadId.getReadId()];
            fastaFile << ">" << vertex.orientedReadId << " ";
            copy(readName.begin(), readName.end(), ostream_iterator<char>(fastaFile));
            fastaFile << "\n";
            copy(sequence.begin(), sequence.end(), ostream_iterator<Base>(fastaFile));
            fastaFile << "\n";
        }

        // Create the blast command and run it.
        const string blastOptions =
            "-outfmt '10 qseqid sseqid sstart send' "
            "-evalue 1e-200 -max_hsps 1 -max_target_seqs 1 "
            "-num_threads " + to_string(std::thread::hardware_concurrency());
        const string blastOutputFileName = "/dev/shm/" + uuid + ".txt";
        const string blastErrFileName = "/dev/shm/" + uuid + ".errtxt";
        const string command = "blastn -task megablast -subject " + httpServerData.referenceFastaFileName +
            " -query " + fastaFileName + " 1>" + blastOutputFileName + " 2>" + blastErrFileName +
            " " + blastOptions;
        cout << timestamp << "Running Blast command." << endl;
        ::system(command.c_str());
        cout << timestamp << "Blast command completed." << endl;

        // Copy any error output to html.
        if(filesystem::fileSize(blastErrFileName)) {
            ifstream blastErrFile(blastErrFileName);
            html << "<pre style='font-size:10px'>";
            html << blastErrFile.rdbuf();
            html << "</pre>";
            blastErrFile.close();
        }

        // Store alignments, keyed by OrientedReadId.
        // For each OrientedReadId we store all the alignments we found,
        // already tokenized.
        using Separator = boost::char_separator<char>;
        using Tokenizer = boost::tokenizer<Separator>;
        const Separator separator(",");
        std::map<OrientedReadId, vector< vector<string> > > alignments;
        ifstream blastOutputFile(blastOutputFileName);
        string line;
        vector<string> tokens;
        while(true) {

            // Get a line.
            string line;
            std::getline(blastOutputFile, line);
            if(!blastOutputFile) {
                break;
            }

            // Tokenize it.
            Tokenizer tokenizer(line, separator);
            tokens.clear();
            tokens.insert(tokens.begin(), tokenizer.begin(), tokenizer.end());

            // Extract the OrientedReadId.
            CZI_ASSERT(!tokens.empty());
            const OrientedReadId orientedReadId = OrientedReadId(tokens.front());

            // Store it.
            alignments[orientedReadId].push_back(tokens);
        }

        // Remove the files we created.
        filesystem::remove(fastaFileName);
        filesystem::remove(blastOutputFileName);
        filesystem::remove(blastErrFileName);

        // Now store the alignments as additional text in the vertices tooltips.
        BGL_FORALL_VERTICES(v, graph, LocalReadGraph) {
            LocalReadGraphVertex& vertex = graph[v];
            const auto& vertexAlignments = alignments[vertex.orientedReadId];
            for(const auto& alignment: vertexAlignments) {
                CZI_ASSERT(alignment.size() == 4);
                vertex.additionalToolTipText += " " + alignment[1] + ":" + alignment[2] + "-" + alignment[3];
            }
        }
    }


    // Write it out in graphviz format.
    const string uuid = to_string(boost::uuids::random_generator()());
    const string dotFileName = "/dev/shm/" + uuid + ".dot";
    graph.write(dotFileName, maxDistance);



    // Display the graph in svg format.
    if(format == "svg") {
        const string command =
            "timeout " + to_string(timeout - seconds(createFinishTime - createStartTime)) +
            " sfdp -O -T svg " + dotFileName +
            " -Gsize=" + to_string(sizePixels/72.);
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
        filesystem::remove(dotFileName);



        // Write a title and display the graph.
        html <<
            "<h1 style='line-height:10px'>Read graph near read " << readId << "</h1>"
            "Color legend: "
            "<span style='background-color:LightGreen'>start vertex</span> "
            "<span style='background-color:cyan'>vertices at maximum distance (" << maxDistance <<
            ") from the start vertex</span> "
            "<span style='background-color:red'>chimeric vertices</span>.<br>";


        // Display the graph.
        const string svgFileName = dotFileName + ".svg";
        ifstream svgFile(svgFileName);
        html << svgFile.rdbuf();
        svgFile.close();



        // Add to each vertex a cursor that shows you can click on it.
        html <<
            "<script>"
            "var vertices = document.getElementsByClassName('node');"
            "for (var i=0;i<vertices.length; i++) {"
            "    vertices[i].style.cursor = 'pointer';"
            "}"
            "</script>";



        // Remove the .svg file.
        filesystem::remove(svgFileName);
    }



    // Display the graph in png format.
    else if(format == "png") {

        // Run graphviz to create the png file and the cmapx file.
        // The cmapx file is used to create links on the image.
        // See here for more information:
        // https://www.graphviz.org/doc/info/output.html#d:imap
        const string command =
            "timeout " + to_string(timeout - seconds(createFinishTime - createStartTime)) +
            " sfdp -O -T png -T cmapx " + dotFileName +
            " -Gsize=" + to_string(sizePixels/72.);
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
            filesystem::remove(dotFileName);
            throw runtime_error("Signal " + to_string(signalNumber) + " while running graph layout command: " + command);
        } else {
            filesystem::remove(dotFileName);
            throw runtime_error("Abnormal status " + to_string(commandStatus) + " while running graph layout command: " + command);

        }
        // Remove the .dot file.
        filesystem::remove(dotFileName);

        // Get the names of the files we created.
        const string pngFileName = dotFileName + ".png";
        const string cmapxFileName = dotFileName + ".cmapx";

        // Create a base64 version of the png file.
        const string base64FileName = pngFileName + ".base64";
        const string base64Command = "base64 " + pngFileName + " > " +
            base64FileName;
        ::system(base64Command.c_str());


        // Write out the png image.
        html << "<p><img usemap='#G' src=\"data:image/png;base64,";
        ifstream png(base64FileName);
        html << png.rdbuf();
        html << "\"/>";
        ifstream cmapx(cmapxFileName);
        html << cmapx.rdbuf();

        // Remove the files we created.
        filesystem::remove(pngFileName);
        filesystem::remove(cmapxFileName);
        filesystem::remove(base64FileName);
    }



    // If got here, the format string is not one of the ones
    // we support.
    else {
        html << "Invalid format " << format << " specified";
        filesystem::remove(dotFileName);
    }
}



void ChanZuckerberg::shasta::writeStrandSelection(
    ostream& html,          // The html stream to write the form to.
    const string& name,     // The selection name.
    bool select0,           // Whether strand 0 is selected.
    bool select1)           // Whether strand 1 is selected.
{
    html <<
        "<select name=" << name <<
        " title='Choose 0 (+) for the input read or 1 (-) for its reverse complement'>"

        "<option value=0"
        << (select0 ? " selected" : "") <<
        ">0 (+)</option>"

        "<option value=1"
        << (select1 ? " selected" : "") <<
        ">1 (-)</option>"

        "</select>";

}

#endif
