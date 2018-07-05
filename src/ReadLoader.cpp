// Nanopore2.
#include "ReadLoader.hpp"
#include "splitRange.hpp"
#include "timestamp.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;

// Standard library.
#include "tuple.hpp"



// Load reads from a fastq or fasta file.
ReadLoader::ReadLoader(
    const string& fileName,
    size_t minReadLength,
    size_t blockSize,
    size_t threadCountForReadingArgument,
    size_t threadCountForProcessingArgument,
    const string& dataNamePrefix,
    size_t pageSize,
    LongBaseSequences& reads,
    MemoryMapped::VectorOfVectors<char, uint64_t>& readNames) :

    MultithreadedObject(*this),
    minReadLength(minReadLength),
    blockSize(blockSize),
    threadCountForReading(threadCountForReadingArgument),
    threadCountForProcessing(threadCountForProcessingArgument)
{
    cout << timestamp << "Loading reads from " << fileName << "." << endl;
    cout << "Input file block size: " << blockSize << " bytes." << endl;
    const auto tBegin = std::chrono::steady_clock::now();

    // Adjust the numbers of threads, if necessary.
    if(threadCountForReading == 0) {
        threadCountForReading = 1;
    }
    if(threadCountForProcessing == 0) {
        threadCountForProcessing = std::thread::hardware_concurrency();
    }
    cout << "Using " << threadCountForReading << " threads for reading and ";
    cout << threadCountForProcessing << " threads for processing." << endl;

    // Allocate space to keep a block of the file.
    buffer.reserve(blockSize);

    // Open the input file.
    fileDescriptor = ::open(fileName.c_str(), O_RDONLY);
    if(fileDescriptor == -1) {
        throw runtime_error("Error opening " + fileName + " for read.");
    }

    // Find the size of the input file.
    getFileSize();
    cout << "Input file size is " << fileSize << " bytes." << endl;

    // Allocate space for the data structures where
    // each thread stores the reads it found and their names.
    threadReadNames.resize(threadCountForProcessing);
    threadReads.resize(threadCountForProcessing);
    for(size_t threadId=0; threadId<threadCountForProcessing; threadId++) {
        const string  threadDataNamePrefix = dataNamePrefix + +"tmp-ReadLoader-" + to_string(threadId) + "-";
        threadReadNames[threadId] = make_shared< MemoryMapped::VectorOfVectors<char, uint64_t> >();
        threadReadNames[threadId]->createNew(threadDataNamePrefix + "ReadNames", pageSize);
        threadReads[threadId] = make_shared<LongBaseSequences>();
        threadReads[threadId]->createNew(threadDataNamePrefix + "Reads", pageSize);
    }


    // Main loop over blocks in the input file.
    for(blockBegin=0; blockBegin<fileSize; ) {

        // Read this block.
        blockEnd = min(blockBegin+blockSize, fileSize);
        cout << timestamp << "Reading block " << blockBegin << " " << blockEnd << ", " << blockEnd-blockBegin << " bytes." << endl;
        const auto t0 = std::chrono::steady_clock::now();
        readBlock(threadCountForReading);
        const auto t1 = std::chrono::steady_clock::now();
        const double t01 = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)).count());
        cout << "Block read in " << t01 << " s at " << double(blockEnd-blockBegin)/t01 << " bytes/s." << endl;
        // cout << leftOver.size() << " characters in this block will be processed with the next block." << endl;

        // Process this block in parallel.
        // This does not use load balancing. Each thread is assigned a predetermined
        // portion of this block. This way, we store the reads in the same
        // order as they appear in the input file.
        cout << "Processing " << buffer.size() << " input characters." << endl;
        const auto t2 = std::chrono::steady_clock::now();
        runThreads(&ReadLoader::processThreadFunction, threadCountForProcessing);
        const auto t3 = std::chrono::steady_clock::now();
        const double t23 = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2)).count());
        cout << "Block processed in " << t23 << " s." << endl;

        // Permanently store the reads found by each thread.
        // We could speed this up by just making space in single threaded code,
        // then copying the data in multi threaded code.
        // cout << timestamp << "Storing reads for this block." << endl;
        const auto t4 = std::chrono::steady_clock::now();
        for(size_t threadId=0; threadId<threadCountForProcessing; threadId++) {
            MemoryMapped::VectorOfVectors<char, uint64_t>& thisThreadReadNames = *(threadReadNames[threadId]);
            LongBaseSequences& thisThreadReads = *(threadReads[threadId]);
            const size_t n = thisThreadReadNames.size();
            CZI_ASSERT(thisThreadReads.size() == n);
            for(size_t i=0; i<n; i++) {
                readNames.appendVector(thisThreadReadNames.begin(i), thisThreadReadNames.end(i));
                reads.append(thisThreadReads[i]);
            }
            thisThreadReadNames.clear();
            thisThreadReads.clear();
        }
        const auto t5 = std::chrono::steady_clock::now();
        const double t45 = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t5 - t4)).count());
        cout << "Reads for this block stored in " << t45 << " s." << endl;

        // Prepare to process the next block.
        blockBegin = blockEnd;
    }
    cout << timestamp << "Done processing fasta file." << endl;


    // Close the input file.
    ::close(fileDescriptor);

    // Remove the temporary data used for thread storage.
    for(size_t threadId=0; threadId<threadCountForProcessing; threadId++) {
        threadReadNames[threadId]->remove();
        threadReads[threadId]->remove();
    }
    const auto tEnd = std::chrono::steady_clock::now();
    const double tTotal = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tBegin)).count());
    cout << "Processed " << fileSize << " bytes in " << tTotal;
    cout << "s, " << double(fileSize)/tTotal << " bytes/s." << endl;
    cout << timestamp << "Done loading reads." << endl;
}



void ReadLoader::getFileSize()
{
    CZI_ASSERT(fileDescriptor != -1);

    struct stat buffer;
    if(::fstat(fileDescriptor, &buffer)) {
        throw runtime_error("Error from fstat.");
    }
    fileSize = buffer.st_size;
}



// Read one block into the above buffer.
// This copies the leftOver data to buffer, then
// reads into the rest of the buffer the portion of the input file
// at offset in [blockBegin, blockEnd).
// It finally moves to the leftOver data the
// final, possibly partial, read in the buffer
// (this is not done for the final block).
void ReadLoader::readBlock(size_t threadCount)
{
    // Prepare the buffer for this block and
    // copy the leftOver data to the buffer.
    buffer.resize(leftOver.size() + (blockEnd - blockBegin));
    copy(leftOver.begin(), leftOver.end(), buffer.begin());


    if(threadCount <= 1) {
        readBlockSequential();
    } else {
        readBlockParallel(threadCount);
    }

    if(buffer.front() != '>') {
        throw runtime_error("Expected '>' at beginning of a block.");
    }

    if(blockEnd != fileSize) {
        // Go back to the beginning of the last read in the buffer.
        size_t bufferIndex=buffer.size()-1;
        for(; bufferIndex>0; bufferIndex--) {
            if(readBeginsHere(bufferIndex)) {
                break;
            }
        }
        CZI_ASSERT(buffer[bufferIndex]=='>' && (bufferIndex==0 || buffer[bufferIndex-1]=='\n'));

        // Throw away what follows, store it as leftover.
        leftOver.clear();
        leftOver.resize(buffer.size() - bufferIndex);
        copy(buffer.begin()+bufferIndex, buffer.end(), leftOver.begin());
        buffer.resize(bufferIndex);
    }
}



void ReadLoader::readBlockSequential()
{

    size_t bytesToRead = blockEnd - blockBegin;
    char* bufferPointer = buffer.data() + leftOver.size();
    while(bytesToRead) {
        const ssize_t byteCount = ::read(fileDescriptor, bufferPointer, bytesToRead);
        if(byteCount <= 0) {
            throw runtime_error("Error during read.");
        }
        bytesToRead -= byteCount;
        bufferPointer += byteCount;
    }

}



void ReadLoader::readBlockParallel(size_t threadCount)
{
    runThreads(&ReadLoader::readThreadFunction, threadCountForReading);
}



void ReadLoader::processThreadFunction(size_t threadId)
{

    // Get the slice of the buffer assigned to this thread.
    size_t sliceBegin, sliceEnd;
    tie(sliceBegin, sliceEnd) = splitRange(0, buffer.size(), threadCountForProcessing, threadId);


    // Locate the first read that begins in the slice assigned to this thread.
    // We only process reads that begin in this slice.
    size_t bufferIndex = sliceBegin;
    while(bufferIndex<sliceEnd && !readBeginsHere(bufferIndex)) {
        ++bufferIndex;
    }
    if(threadId == 0) {
        CZI_ASSERT(bufferIndex == 0);
    }
    if(bufferIndex == sliceEnd) {
        return;
    }

    // Access the data structures that this thread will use to store reads
    // and read names.
    MemoryMapped::VectorOfVectors<char, uint64_t>& thisThreadReadNames = *(threadReadNames[threadId]);
    LongBaseSequences& thisThreadReads = *(threadReads[threadId]);

    // Main loop over the buffer slice assigned to this thread.
    string readName;
    vector<Base> read;
    while(bufferIndex < sliceEnd) {

        // Skip the '>' that introduces the new read.
        CZI_ASSERT(buffer[bufferIndex++] == '>');

        // Extract the read name and discard the rest of the line.
        bool blankFound = false;
        while(bufferIndex < buffer.size()) {
            const char c = buffer[bufferIndex++];
            if(c == '\n') {
                break;
            }
            if(c==' ') {
                blankFound = true;
            }
            if(!blankFound) {
                readName.push_back(c);
            }
        }
        // cout << readName << endl;


        // Read the base characters.
        while(bufferIndex < buffer.size()) {
            const char c = buffer[bufferIndex++];
            if(c == '\n') {
                break;
            }
            read.push_back(Base(c, Base::FromCharacter()));
        }

        // If the read is too short, skip it.
        if(read.size() < minReadLength) {
            continue;
        }

        // Store the read name.
        thisThreadReadNames.appendVector(readName.begin(), readName.end());
        readName.clear();

        // Store the read bases.
        thisThreadReads.append(read);
        read.clear();

    }
}



void ReadLoader::readThreadFunction(size_t threadId)
{
    // Get the slice of the block assigned to this thread.
    size_t sliceBegin, sliceEnd;
    tie(sliceBegin, sliceEnd) = splitRange(blockBegin, blockEnd, threadCountForReading, threadId);

    // Read all the data in the slice.
    size_t bytesToRead = sliceEnd - sliceBegin;
    char* bufferPointer = buffer.data() + leftOver.size() + sliceBegin;
    size_t offset = sliceBegin;
    while(bytesToRead) {
        const ssize_t byteCount = ::pread(fileDescriptor, bufferPointer, bytesToRead, offset);
        if(byteCount <= 0) {
            throw runtime_error("Error during read.");
        }
        bytesToRead -= byteCount;
        bufferPointer += byteCount;
        offset += byteCount;
    }
}



// Return true if a read begins at this position in the buffer.
bool ReadLoader::readBeginsHere(size_t bufferIndex) const
{
    const char c = buffer[bufferIndex];
    if(bufferIndex == 0) {
        CZI_ASSERT(c == '>');
        return true;
    } else {
        return c=='>' && buffer[bufferIndex-1]=='\n';
    }
}
