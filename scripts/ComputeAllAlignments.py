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
a.accessCandidateAlignments()

# Do the computation.
a.computeAllAlignments(
    maxVertexCountPerKmer = int(config['Align']['maxVertexCountPerKmer']),
    maxSkip = int(config['Align']['maxSkip']),
    minAlignedMarkerCount = int(config['Align']['minAlignedMarkerCount']),
    maxTrim = int(config['Align']['minAlignedMarkerCount']),
    minCoverage = int(config['MarkerGraph']['minCoverage']))

