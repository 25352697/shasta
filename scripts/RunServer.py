#!/usr/bin/python3

import os
import shasta
import GetConfig

# Find the path to the docs directory.
thisScriptPath = os.path.realpath(__file__)
thisScriptDirectory = os.path.dirname(thisScriptPath)
thisScriptParentDirectory = os.path.dirname(thisScriptDirectory)
docsDirectory = thisScriptParentDirectory + '/docs'

# Read the config file.
config = GetConfig.getConfig()

# Initialize the assembler and access what we need.
a = shasta.Assembler()
a.accessReadsReadOnly()
a.accessReadNamesReadOnly()
a.accessKmers()
a.accessMarkers()
a.accessCandidateAlignments()
a.accessAlignmentData()
a.accessReadGraph()
a.accessMarkerGraphVertices()
a.setupConsensusCaller(config['Assembly']['consensusCaller'])

a.setDocsDirectory(docsDirectory)
a.explore()


