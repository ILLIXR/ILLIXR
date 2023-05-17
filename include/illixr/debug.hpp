#include <iostream>

#define DEBUG2(x1, x2) \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " << #x1 << "=" << x1 < < < < ", " << #x2 << "=" << x2 << std::endl;
#define DEBUG(x) std::cerr << __FILE__ << ':' << __LINE__ << ": " << #x << "=" << x << std::endl;
