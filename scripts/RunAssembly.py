#!/usr/bin/python3

import shasta
import GetConfig
import ast
import os
import sys


def verifyConfigFiles(parentDirectory=""):
    # Generate absolute paths to the files that will be created
    dataPath = os.path.abspath(os.path.join(parentDirectory, "Data"))
    threadLogsPath = os.path.abspath(os.path.join(parentDirectory, "threadLogs"))
    confPath = os.path.abspath(os.path.join(parentDirectory, "shasta.conf"))

    # Check that we have what we need.
    if not os.path.lexists(dataPath):
        raise Exception('Missing: Data. Use SetupRunDirectory.py to set up the run directory.')
    if not os.path.lexists(threadLogsPath):
        raise Exception('Missing: threadLogs. Use SetupRunDirectory.py to set up the run directory.')
    if not os.path.lexists(confPath):
        raise Exception('Missing: configuration file shasta.conf. Sample available in shasta-install/conf.')


def verifyFastaFiles(fastaFileNames):
    # Get from the arguments the list of input fasta files and check that they all exist.
    helpMessage = "Invoke passing as arguments the names of the input Fasta files."
    if len(sys.argv)==1:
        print(helpMessage)
        exit(1)

    for fileName in fastaFileNames:
        if not os.path.lexists(fileName):
            raise Exception('Input file %s not found.' % fileName)


def runAssembly(config, fastaFileNames):
    # Create the assembler.
    useRunLengthReadsString = config['Reads']['useRunLengthReads']
    if useRunLengthReadsString == 'True':
        useRunLengthReads = True
    elif useRunLengthReadsString == 'False':
        useRunLengthReads = False
    else:
        raise RuntimeError("Configuration parameter useRunLengthReads in section Reads must be True or False.")
    
    # Create the Assembler.
    a = shasta.Assembler(useRunLengthReads = useRunLengthReads)
    
    # Set up the consensus caller.
    a.setupConsensusCaller(config['Assembly']['consensusCaller'])
    
    # Figure out if we should use marginPhase, and if so set it up.
    useMarginPhase = ast.literal_eval(config['Assembly']['useMarginPhase'])
    if useMarginPhase:
        a.setupMarginPhase()
    
    # Read the input fasta files.
    a.accessReadsReadWrite()
    a.accessReadNamesReadWrite()
    
    for fileName in fastaFileNames:  
        print('Reading input file', fileName, flush=True) 
        a.addReadsFromFasta(
            fileName = fileName, 
            minReadLength = int(config['Reads']['minReadLength']))
    
    # Create a histogram of read lengths.
    a.histogramReadLength(fileName="ReadLengthHistogram.csv")
    
    # Randomly select the k-mers that will be used as markers.
    a.randomlySelectKmers(
        k = int(config['Kmers']['k']), 
        probability = float(config['Kmers']['probability']))
        
    # Find the markers in the reads.
    a.findMarkers()
    
    # Run MinHash to find pairs of reads that may overlap.
    a.findAlignmentCandidatesMinHash(
        m = int(config['MinHash']['m']), 
        minHashIterationCount = int(config['MinHash']['minHashIterationCount']), 
        maxBucketSize = int(config['MinHash']['maxBucketSize']),
        minFrequency = int(config['MinHash']['minFrequency']))
    
    # Compute alignments.
    a.computeAlignments(
        maxMarkerFrequency = int(config['Align']['maxMarkerFrequency']),
        maxSkip = int(config['Align']['maxSkip']),
        minAlignedMarkerCount = int(config['Align']['minAlignedMarkerCount']),
        maxTrim = int(config['Align']['maxTrim']))
        
    # Create the read graph.
    a.createReadGraph(maxTrim = int(config['Align']['maxTrim']))
    
    # Flag chimeric reads.
    a.flagChimericReads(
        maxChimericReadDistance = int(config['ReadGraph']['maxChimericReadDistance']))
    a.computeReadGraphConnectedComponents()
    
    # Create vertices of the marker graph.
    a.createMarkerGraphVertices(
        maxMarkerFrequency = int(config['Align']['maxMarkerFrequency']),
        maxSkip = int(config['Align']['maxSkip']),
        minCoverage = int(config['MarkerGraph']['minCoverage']),
        maxCoverage = int(config['MarkerGraph']['maxCoverage']))
    
    # Create edges of the marker graph.
    a.createMarkerGraphEdges()
    
    # Approximate transitive reduction.
    a.flagMarkerGraphWeakEdges(
        lowCoverageThreshold = int(config['MarkerGraph']['lowCoverageThreshold']),
        highCoverageThreshold = int(config['MarkerGraph']['highCoverageThreshold']),
        maxDistance = int(config['MarkerGraph']['maxDistance']),
        )
    
    # Prune the strong subgraph of the marker graph.
    a.pruneMarkerGraphStrongSubgraph(
        iterationCount = int(config['MarkerGraph']['pruneIterationCount']))
    
    # Remove short cycles and bubbles from the marker graph.
    a.removeShortMarkerGraphCycles(
        maxLength = int(config['MarkerGraph']['shortCycleLengthThreshold']))
    a.removeMarkerGraphBubbles(
        maxLength = int(config['MarkerGraph']['bubbleLengthThreshold']))
    a.removeMarkerGraphSuperBubbles(
        maxLength = int(config['MarkerGraph']['superBubbleLengthThreshold']))
    
    # Create the assembly graph.
    a.createAssemblyGraphEdges()
    a.createAssemblyGraphVertices()
    a.writeAssemblyGraph("AssemblyGraph-Final.dot")
    
    print("############# useMarginPhase = ", useMarginPhase)
    print("############# type of useMarginPhase = ", type(useMarginPhase))
    print("############# config['Assembly']['useMarginPhase'] = ", config['Assembly']['useMarginPhase'])
    print("############# type of config['Assembly']['useMarginPhase'] = ", type(config['Assembly']['useMarginPhase']))

    # Use the assembly graph for global assembly.
    a.assemble(
        markerGraphEdgeLengthThresholdForConsensus =
        int(config['Assembly']['markerGraphEdgeLengthThresholdForConsensus']),
        useMarginPhase = useMarginPhase)
    
    a.computeAssemblyStatistics()
    a.writeGfa1('Assembly.gfa')
    a.writeFasta('Assembly.fasta')


def main():
    # Parse arguments
    fastaFileNames = sys.argv [1:]

    # Ensure prerequisite files are present
    verifyConfigFiles()
    verifyFastaFiles(fastaFileNames=fastaFileNames)

    # Read the config file.
    config = GetConfig.getConfig()

    # Run with user specified configuration and input files
    runAssembly(config=config, fastaFileNames=fastaFileNames)
    
if __name__ == "__main__":
    main()

