#!/usr/bin/python3

import shasta
import GetConfig
import os
import sys

# Check that we have what we need.
if not os.path.lexists('data'):
    raise Exception('Missing: data. Use SetupRunDirectory.py to set up the run directory.')
if not os.path.lexists('Data'):
    raise Exception('Missing: Data. Use SetupRunDirectory.py to set up the run directory.')
if not os.path.lexists('threadLogs'):
    raise Exception('Missing: threadLogs. Use SetupRunDirectory.py to set up the run directory.')
if not os.path.lexists('shasta.conf'):
    raise Exception('Missing: configuration file shasta.conf. Sample available in shasta-install/conf.')

# Get from the arguments the list of input fasta files and check that they all exist.
helpMessage = "Invoke passing as arguments the names of the input Fasta files."
if len(sys.argv)==1:
    print(helpMessage)
    exit(1)
fastaFileNames = sys.argv [1:];
for fileName in fastaFileNames:  
    if not os.path.lexists(fileName):
        raise Exception('Input file %s not found.' % fileName)

# Read the config file.
config = GetConfig.getConfig()

# Create the assembler.
a = shasta.Assembler()

# Read the input fasta files.
a.accessReadsReadWrite();
a.accessReadNamesReadWrite();
for fileName in fastaFileNames:  
    print('Reading input file', fileName) 
    a.addReadsFromFasta(
        fileName = fileName, 
        minReadLength = int(config['Reads']['minReadLength']))

# Randomly select the k-mers that will be used as markers.
a.randomlySelectKmers(
    k = int(config['Kmers']['k']), 
    probability = float(config['Kmers']['probability']))
    
# Find the markers in the reads.
a.findMarkers()

# Run MinHash to find pairs of reads that may overlap.
a.findOverlaps(
    m = int(config['MinHash']['m']), 
    minHashIterationCount = int(config['MinHash']['minHashIterationCount']), 
    log2MinHashBucketCount = int(config['MinHash']['log2MinHashBucketCount']),
    maxBucketSize = int(config['MinHash']['maxBucketSize']),
    minFrequency = int(config['MinHash']['minFrequency']))

# Compute alignments and create the global marker graph.
a.computeAllAlignments(
    maxVertexCountPerKmer = int(config['Align']['maxVertexCountPerKmer']),
    maxSkip = int(config['Align']['maxSkip']),
    minAlignedMarkerCount = int(config['Align']['minAlignedMarkerCount']),
    maxTrim = int(config['Align']['minAlignedMarkerCount']),
    minCoverage = int(config['MarkerGraph']['minCoverage']))


