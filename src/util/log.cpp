#include "log.h"

using namespace dy;

std::ostream& dy::log(dy::Level l) {
    switch (l) {
    case trace:
    case debug:
    case info:
        return std::cout;
    case warning:
    case error:
    case critical:
        return std::cerr;
    default:
        return std::cout;
    }
}
