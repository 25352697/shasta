#include "MultitreadedObject.hpp"
#include "timestamp.hpp"
using namespace ChanZuckerberg;
using namespace Nanopore2;

#include <chrono>



// Class used only by function testMultithreadedObject.
class ChanZuckerberg::Nanopore2::MultithreadedObjectTestClass :
    public MultithreadedObject<MultithreadedObjectTestClass> {
public:

    MultithreadedObjectTestClass(size_t n) : MultithreadedObject(*this), n(n)
    {
        x.resize(n);
        y.resize(n);
        z.resize(n);
        for(size_t i=0; i<n; i++) {
            x[i] = i;
            y[i] = 2 * i;
            z[i] = 18;
        }
    }
    void compute(size_t threadId)
    {
        ostream& out = getLog(threadId);

        size_t begin, end;
        while(getNextBatch(begin, end)) {
            out << timestamp << begin << " " << end << endl;
            for(size_t i=begin; i!=end; i++) {
                size_t s = 0;
                for(size_t j=0; j<n; j++) {
                    s += x[i] * y[j];
                }
                z[i] = s;
            }
        }
    }
    void check() const
    {
        for(size_t i=0; i<x.size(); i++) {
            size_t s = 0;
            for(size_t j=0; j<n; j++) {
                s += x[i] * y[j];
            }
            CZI_ASSERT(z[i] == s);
        }
    }

    size_t n;
    vector<size_t> x;
    vector<size_t> y;
    vector<size_t> z;
};



void ChanZuckerberg::Nanopore2::testMultithreadedObject()
{

    const size_t n = 32 * 1024;
    const size_t batchSize = 64;
    const size_t threadCount = 8;
    MultithreadedObjectTestClass x(n);
    for(int i=0; i<10; i++) {
        x.setupLoadBalancing(n, batchSize);
        const auto t0 = std::chrono::steady_clock::now();
        x.runThreads(&MultithreadedObjectTestClass::compute, threadCount, "threadLogs/");
        const auto t1 = std::chrono::steady_clock::now();
        const double t01 = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)).count());
        cout << t01 << endl;
    }
    x.check();
}

