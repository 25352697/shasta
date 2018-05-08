#ifndef CZI_NANOPORE2_READ_ID_HPP
#define CZI_NANOPORE2_READ_ID_HPP

// CZI.
#include "CZI_ASSERT.hpp"

// Standard libraries.
#include "cstdint.hpp"
#include "iostream.hpp"
#include <limits>
#include "string.hpp"



namespace ChanZuckerberg {
    namespace Nanopore2 {

        // Type used to identify a read.
        // This is used as an index into Assembler::reads.
        using ReadId = uint32_t;

        // Class used to identify an oriented read,
        // that is a read, possibly reverse complemented.
        class OrientedReadId;
        inline ostream& operator<<(ostream&, OrientedReadId);
        using Strand = ReadId;

    }
}


// Class used to identify an oriented read,
// that is a read, possibly reverse complemented.
// The strand stored in the least significant
// bit is 0 if the oriented read is identical
// to the original read and 1 if it is reverse complemented
class ChanZuckerberg::Nanopore2::OrientedReadId {
public:
    OrientedReadId() : value(std::numeric_limits<ReadId>::max()) {}
    OrientedReadId(ReadId readId, Strand strand) : value((readId<<1) | strand)
    {
        CZI_ASSERT(strand < 2);
    }
    ReadId getReadId() const
    {
        return value >> 1;
    }
    Strand getStrand() const
    {
        return value & 1;
    }

    // Return the integer value, which can be used as an index intoi Assembler::orientedReadIds.
    ReadId getValue() const
    {
        return value;
    }

    // Return a string representing this OrientedReadId.
    string getString() const
    {
        return to_string(getReadId()) + "-" + to_string(getStrand());
    }

    bool operator==(const OrientedReadId& that) const
    {
        return value == that.value;
    }
    bool operator<(const OrientedReadId& that) const
    {
        return value < that.value;
    }

private:
    ReadId value;
};



inline std::ostream& ChanZuckerberg::Nanopore2::operator<<(
    std::ostream& s,
    OrientedReadId orientedReadId)
{
    s << orientedReadId.getString();
    return s;
}


#endif
