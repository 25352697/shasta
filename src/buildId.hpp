#ifndef CZI_SHASTA_BUILD_ID_HPP
#define CZI_SHASTA_BUILD_ID_HPP


// The build id is used to identify the build
// (by release number of by other means).
// It is obtained by -DBUILD_ID on the compile line.
// To set that, use something like this when running cmake:
// -DBUILD_ID="Build identification string"


#include "string.hpp"

namespace ChanZuckerberg {
    namespace shasta {
        extern const string buildId;
    }
}

#endif
