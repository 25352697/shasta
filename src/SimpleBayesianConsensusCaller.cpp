#include <boost/tokenizer.hpp>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>
#include <string>
#include <cstdio>
#include <array>
#include <cmath>
#include <map>
#include "SimpleBayesianConsensusCaller.hpp"
#include "Coverage.hpp"
#include "ConsensusCaller.hpp"

using ChanZuckerberg::shasta::Consensus;
using Separator = boost::char_separator<char>;
using Tokenizer = boost::tokenizer<Separator>;
using std::runtime_error;
using std::ifstream;
using std::vector;
using std::string;
using std::printf;
using std::array;
using std::pair;
using std::make_pair;
using std::cout;
using std::pow;
using std::map;
using std::max;

using namespace ChanZuckerberg;
using namespace shasta;


// Helper function
void SimpleBayesianConsensusCaller::splitAsDouble(string s, char separatorChar, vector<double>& tokens){
    Separator separator(&separatorChar);
    Tokenizer tok{s, separator};

    for (string token: tok) {
        tokens.push_back(stod(token));
    }
}


// Helper function
void SimpleBayesianConsensusCaller::splitAsString(string s, char separatorChar, vector<string>& tokens){
    Separator separator(&separatorChar);
    Tokenizer tok{s, separator};

    for (string token: tok) {
        tokens.push_back(token);
    }
}


SimpleBayesianConsensusCaller::SimpleBayesianConsensusCaller(){
    maxRunlength = 50;
    ignoreNonConsensusBaseRepeats = true;
    predictGapRunlengths = false;
    countGapsAsZeros = false;

    const string fileName = "SimpleBayesianConsensusCaller.csv";
    ifstream matrixFile(fileName);
    if (not matrixFile.good()) {
        const string errorMessage = "Error opening file: " + fileName;
        throw runtime_error(errorMessage);
    }

    loadConfiguration(matrixFile);

    cout << "Using SimpleBayesianConsensusCaller with '"<< configurationName <<"' configuration\n";
}


void SimpleBayesianConsensusCaller::printProbabilityMatrices(char separator){
    const uint32_t length = uint(probabilityMatrices[0].size());
    uint32_t nBases = 4;

    for (uint32_t b=0; b<nBases; b++){
        cout << '>' << Base::fromInteger(b).character() << " " << probabilityMatrices[b].size() << '\n';

        for (uint32_t i=0; i<length; i++){
            for (uint32_t j=0; j<length; j++){
                // Print with exactly 9 decimal values
                printf("%.9f",probabilityMatrices[b][i][j]);
                if (j != length-1){
                    cout << separator;
                }
            }
            cout << '\n';
        }
        if (b != nBases-1){
            cout << '\n';
        }
    }
}


void SimpleBayesianConsensusCaller::printPriors(char separator){
    const uint32_t length = uint(priors[0].size());
    uint32_t nBases = 2;

    for (uint32_t b=0; b<nBases; b++){
        cout << '>' << Base::fromInteger(b).character() << " " << priors[b].size() << '\n';

        for (uint32_t i=0; i<length; i++){
            printf("%d %.9f",int(i), priors[b][i]);
            if (i != length-1){
                cout << separator;
            }
        }
        if (b != nBases-1){
            cout << '\n';
        }
    }
}


void SimpleBayesianConsensusCaller::parseName(ifstream& matrixFile, string& line){
    // Expect only one line to follow
    getline(matrixFile, line);
    configurationName = line;
}


void SimpleBayesianConsensusCaller::parsePrior(ifstream& matrixFile, string& line, vector<string>& tokens){
    // Expect only one line to follow
    getline(matrixFile, line);

    // Initialize empty vector to fill with tokens from csv
    vector<double> row;

    // Assume csv format
    splitAsDouble(line, ',', row);

    // Two prior distributions exist. One for AT and one for GC, since observed reads are bidirectional
    if (tokens[0] == "AT"){
        priors[0] = row;
    }
    else if (tokens[0] == "GC"){
        priors[1] = row;
    }
}


void SimpleBayesianConsensusCaller::parseLikelihood(ifstream& matrixFile, string& line, vector<string>& tokens){
    char base;
    uint32_t baseIndex = 0;

    // Expect many lines (usually 51)
    while (getline(matrixFile, line)){

        // Stop iterating lines when blank line is encountered
        if (line.empty()){
            break;
        }

        // Initialize empty vector to fill with tokens from csv
        vector<double> row;

        // Assume csv format
        splitAsDouble(line, ',', row);

        base = tokens[0][0];
        baseIndex = uint32_t(Base::fromCharacter(base).value);

        probabilityMatrices[baseIndex].push_back(row);
    }
}


void SimpleBayesianConsensusCaller::loadConfiguration(ifstream& matrixFile){
    string line;

    while (getline(matrixFile, line)){

        // Header line (labeled via fasta-like headers)
        if (line[0] == '>'){
            vector<string> tokens;

            // Store the header
            line = line.substr(1, line.size()-1);
            splitAsString(line, ' ', tokens);

            if (tokens[0] == "Name"){
                parseName(matrixFile, line);

            }else if (tokens[1] == "prior"){
                parsePrior(matrixFile, line, tokens);

            }else if (tokens[1] == "likelihood"){
                parseLikelihood(matrixFile, line, tokens);
            }
        }
    }
}


void SimpleBayesianConsensusCaller::printLogLikelihoodVector(vector<double>& logLikelihoods){
    int i = 0;
    for (auto& item: logLikelihoods){
        cout << i << " " << pow(10, item) << '\n';
        i++;
    }
}


void SimpleBayesianConsensusCaller::normalizeLikelihoods(vector<double>& x, double xMax) const{
    for (uint32_t i=0; i<x.size(); i++){
        x[i] = x[i]-xMax;
    }
}


void SimpleBayesianConsensusCaller::factorRepeats(array<map<uint16_t,uint16_t>,2>& factoredRepeats, const Coverage& coverage) const{
    // Store counts for each unique observation
    for (auto& observation: coverage.getReadCoverageData() ){
        // If NOT a gap, always increment
        if (not observation.base.isGap()) {
            factoredRepeats[uint16_t(observation.strand)][uint16_t(observation.repeatCount)]++;
        // If IS a gap only increment if "countGapsAsZeros" is true
        }else if (countGapsAsZeros){
            factoredRepeats[uint16_t(observation.strand)][0]++;
        }
    }
}


void SimpleBayesianConsensusCaller::factorRepeats(array<map<uint16_t,uint16_t>,2>& factoredRepeats, const Coverage& coverage, AlignedBase consensusBase) const{
    // Store counts for each unique observation
    for (auto& observation: coverage.getReadCoverageData() ){
        // Ignore non consensus repeat values
        if (observation.base.value == consensusBase.value){
            // If NOT a gap, always increment
            if (not observation.base.isGap()) {
                factoredRepeats[uint16_t(observation.strand)][uint16_t(observation.repeatCount)]++;
            // If IS a gap only increment if "countGapsAsZeros" is true
            }else if (countGapsAsZeros){
                factoredRepeats[uint16_t(observation.strand)][0]++;
            }
        }
    }
}


uint16_t SimpleBayesianConsensusCaller::predictRunlength(const Coverage &coverage, AlignedBase consensusBase, vector<double>& logLikelihoodY) const{
    array <map <uint16_t,uint16_t>, 2> factoredRepeats;    // Repeats grouped by strand and length

    size_t priorIndex = -1;    // Used to determine which prior probability vector to access (AT=0 or GC=1)
    uint16_t x_i;               // Element of X = {x_0, x_1, ..., x_i} observed repeats
    uint16_t c_i;               // Number of times x_i was observed
    uint16_t y_j;               // Element of Y = {y_0, y_1, ..., y_j} true repeat between 0 and j=max_runlength
    double logSum;             // Product (in logspace) of P(x_i|y_j) for each i

    double yMaxLikelihood = -INF;     // Probability of most probable true repeat length
    uint16_t yMax = 0;                 // Most probable repeat length

    // Determine which index to use for this->priors
    if (consensusBase.character() == 'A' || consensusBase.character() == 'T'){
        priorIndex = 0;
    }
    else if (consensusBase.character() == 'G' || consensusBase.character() == 'C'){
        priorIndex = 1;
    }

    // Count the number of times each unique repeat was observed, to reduce redundancy in calculating log likelihoods/
    // Depending on class boolean "ignoreNonConsensusBaseRepeats" filter out observations
    if (ignoreNonConsensusBaseRepeats) {
        factorRepeats(factoredRepeats, coverage, consensusBase);
    }
    else {
        factorRepeats(factoredRepeats, coverage);
    }

    // Iterate all possible Y from 0 to j to calculate p(Y_j|X) where X is all observations 0 to i,
    // assuming i and j are less than maxRunlength
    for (y_j = 0; y_j <= maxRunlength; y_j++){
        // Initialize logSum for this Y value using empirically determined priors
        logSum = priors[priorIndex][y_j];

        for (uint16_t strand = 0; strand <= factoredRepeats.size() - 1; strand++){
            for (auto& item: factoredRepeats[strand]){
                x_i = item.first;
                c_i = item.second;

                // In the case that observed runlength is too large for the matrix, cap it at maxRunlength
                if (x_i > maxRunlength){
                    x_i = maxRunlength;
                }

                // Increment log likelihood for this y_j
                logSum += double(c_i)*probabilityMatrices[consensusBase.value][y_j][x_i];
            }
        }

        logLikelihoodY[y_j] = logSum;

        // Update max Y value if log likelihood is greater than previous maximum... Should do this outside this loop?
        if (logSum > yMaxLikelihood){
            yMaxLikelihood = logSum;
            yMax = y_j;
        }
    }

    normalizeLikelihoods(logLikelihoodY, yMaxLikelihood);

    return max(uint16_t(1), yMax);   // Don't allow zeroes...
}


AlignedBase SimpleBayesianConsensusCaller::predictConsensusBase(const Coverage& coverage) const{
    const vector<CoverageData>& coverageDataVector = coverage.getReadCoverageData();
    vector<uint32_t> baseCounts(5,0);
    uint32_t maxBaseCount = 0;
    uint8_t maxBase = 4;   // Default to gap in case coverage is empty (is this possible?)
    uint32_t key;

    // Count bases. If it's a gap increment placeholder 4 in baseCount vector
    for(const CoverageData& observation: coverageDataVector) {
        if (not observation.base.isGap()) {
            key = observation.base.value;
            baseCounts[key]++;
        }
        else{
            key = 4;
            baseCounts[key]++;
        }
    }

    // Determine most represented base (consensus)
    for (uint32_t i=0; i<5; i++){
        if (baseCounts[i] > maxBaseCount){
            maxBaseCount = baseCounts[i];
            maxBase = uint8_t(i);
        }
    }

    return AlignedBase::fromInteger(maxBase);
}


Consensus SimpleBayesianConsensusCaller::operator()(const Coverage& coverage) const{
    AlignedBase consensusBase;
    uint16_t consensusRepeat;
    uint16_t modalConsensusRepeat;

    vector<double> logLikelihoods(u_long(maxRunlength), -INF);    // initialize as zeros in log space

    consensusBase = predictConsensusBase(coverage);
    modalConsensusRepeat = uint16_t(coverage.mostFrequentRepeatCount(consensusBase));

    // If the simple modal consensus is 1 or 2, use it instead of the bayesian consensus
    if (modalConsensusRepeat < 3){
        consensusRepeat = modalConsensusRepeat;
    }
    else{
        if (predictGapRunlengths) {
            // Predict all run lengths regardless of whether consensus base is a gap
            consensusRepeat = predictRunlength(coverage, consensusBase, logLikelihoods);
        }
        else{
            if (not consensusBase.isGap()) {
                // Consensus is NOT a gap character, and the configuration forbids predicting gaps
                consensusRepeat = predictRunlength(coverage, consensusBase, logLikelihoods);
            }
            else{
                // Consensus IS a gap character, and the configuration forbids predicting gaps
                consensusRepeat = 0;
            }
        }
    }

    return Consensus(AlignedBase::fromInteger(consensusBase.value), consensusRepeat);
}


void testSimpleBayesianConsensusCaller(){
    SimpleBayesianConsensusCaller classifier;
    Coverage coverage;

    coverage.addRead(AlignedBase::fromInteger((uint8_t)1), 1, 1);    // Arguments are base, strand, repeat count.
    coverage.addRead(AlignedBase::fromInteger((uint8_t)1), 0, 2);
    coverage.addRead(AlignedBase::fromInteger((uint8_t)2), 1, 3);
    coverage.addRead(AlignedBase::fromInteger((uint8_t)1), 0, 2);
    coverage.addRead(AlignedBase::fromInteger((uint8_t)1), 1, 2);
    coverage.addRead(AlignedBase::fromInteger((uint8_t)2), 0, 2);
    coverage.addRead(AlignedBase::fromInteger((uint8_t)4), 0, 0);

    AlignedBase consensusBase;

    consensusBase = coverage.mostFrequentBase();

    cout << "CONSENSUS BASE = " << consensusBase << "\n";

    const Consensus consensus = classifier(coverage);

    cout << consensus.base << " " << consensus.repeatCount << '\n';
}
