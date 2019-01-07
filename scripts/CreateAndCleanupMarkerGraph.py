#!/usr/bin/python3

import shasta
import GetConfig
import sys

# Read the config file.
config = GetConfig.getConfig()

# Initialize the assembler and access what we need.
a = shasta.Assembler()
a.accessReadsReadOnly()
a.accessKmers()
a.accessMarkers()
a.accessAlignmentData()
a.accessReadGraph()
a.accessChimericReadsFlags()

# Create vertices of the marker graph.
a.createMarkerGraphVertices(
    maxVertexCountPerKmer = int(config['Align']['maxVertexCountPerKmer']),
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

