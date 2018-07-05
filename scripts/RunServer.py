#!/usr/bin/python3

import shasta


# Initialize the assembler and access what we need.
a = shasta.Assembler()
a.accessReadsReadOnly()
a.accessReadNamesReadOnly()
a.accessKmers()
a.accessMarkers()
a.accessOverlaps()
a.accessAlignmentData()
a.accessGlobalMarkerGraph()
a.explore()


