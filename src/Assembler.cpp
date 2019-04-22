#include "Assembler.hpp"
#include "BiasedGaussianConsensusCaller.hpp"
#include "SimpleConsensusCaller.hpp"
#include "SimpleBayesianConsensusCaller.hpp"
#include "TrainedBayesianConsensusCaller.hpp"
#include "MedianConsensusCaller.hpp"
using namespace ChanZuckerberg;
using namespace shasta;



// Constructor to be called one to create a new run.
Assembler::Assembler(
    const string& largeDataFileNamePrefix,
    size_t largeDataPageSize,
    bool useRunLengthReads) :
    MultithreadedObject(*this),
    largeDataFileNamePrefix(largeDataFileNamePrefix),
    largeDataPageSize(largeDataPageSize)
#ifndef SHASTA_STATIC_EXECUTABLE
    , marginPhaseParameters(0)
#endif
{
    assemblerInfo.createNew(largeDataName("Info"), largeDataPageSize);
    assemblerInfo->useRunLengthReads = useRunLengthReads;

    reads.createNew(largeDataName("Reads"), largeDataPageSize);
    reads.close();

    readNames.createNew(largeDataName("ReadNames"), largeDataPageSize);
    readNames.close();

    if(useRunLengthReads) {
        readRepeatCounts.createNew(largeDataName("ReadRepeatCounts"), largeDataPageSize);
        readRepeatCounts.close();

    }

    // assemblerInfo is the only open object
    // when the constructor finishes.

#ifndef SHASTA_STATIC_EXECUTABLE
    fillServerFunctionTable();
#endif
}



// Constructor to be called to continue an existing run.
Assembler::Assembler(
    const string& largeDataFileNamePrefix,
    size_t largeDataPageSize) :
    MultithreadedObject(*this),
    largeDataFileNamePrefix(largeDataFileNamePrefix),
    largeDataPageSize(largeDataPageSize)
#ifndef SHASTA_STATIC_EXECUTABLE
    , marginPhaseParameters(0)
#endif
{

    assemblerInfo.accessExistingReadWrite(largeDataName("Info"));

    // assemblerInfo is the only open object
    // when the constructor finishes.

#ifndef SHASTA_STATIC_EXECUTABLE
    fillServerFunctionTable();
#endif
}



// Destructor.
Assembler::~Assembler()
{
#ifndef SHASTA_STATIC_EXECUTABLE
    if(marginPhaseParameters) {
        destroyConsensusParameters(marginPhaseParameters);
        marginPhaseParameters = 0;
    }
#endif
}



// Set up the ConsensusCaller used to compute the "best"
// base and repeat count at each assembly position.
void Assembler::setupConsensusCaller(const string& s)
{
    if(s == "SimpleConsensusCaller") {
        consensusCaller = std::make_shared<SimpleConsensusCaller>();
        return;
    }

    if(s == "SimpleBayesianConsensusCaller") {
        consensusCaller = std::make_shared<SimpleBayesianConsensusCaller>();
        return;
    }

    if(s == "TrainedBayesianConsensusCaller") {
        consensusCaller = std::make_shared<TrainedBayesianConsensusCaller>();
        return;
    }

    if(s == "BiasedGaussianConsensusCaller") {
        consensusCaller = std::make_shared<BiasedGaussianConsensusCaller>();
        return;
    }

    if(s == "MedianConsensusCaller") {
        consensusCaller = std::make_shared<MedianConsensusCaller>();
        return;
    }


    // If getting here, the argument does not specify a supported
    // consensus caller.
    throw runtime_error("Unsupported consensus caller " + s);
}



// Read marginPhase parameters from file MarginPhase.json in the run directory.
void Assembler::setupMarginPhase()
{
#ifndef SHASTA_STATIC_EXECUTABLE
    const string fileName = "MarginPhase.json";
    marginPhaseParameters = getConsensusParameters(const_cast<char*>(fileName.c_str()));
    if(!marginPhaseParameters) {
        throw runtime_error("Error reading marginPhase parameters from " + fileName);
    }
#else
    // The static executable does not support MarginPhase.
    CZI_ASSERT(0);
#endif
}



void Assembler::checkMarginPhaseWasSetup()
{
#ifndef SHASTA_STATIC_EXECUTABLE
    if(!marginPhaseParameters) {
        throw runtime_error("MarginPhase was not set up.");
    }
#else
    // The static executable does not support MarginPhase.
    CZI_ASSERT(0);
#endif
}

