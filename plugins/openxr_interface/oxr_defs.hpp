#pragma once

#include <openxr/openxr.h>

#define OXR_CheckErrors(cmd, pfunc) do { \
    XrResult res = cmd; \
    if (XR_FAILED(res)) { \
        spdlog::get("illixr")->error("OpenXR error {} at {}:{} from {}", static_cast<int>(res), __FILE__, __LINE__, #pfunc); \
        throw std::runtime_error("Call failed");                                     \
    } \
} while(0)
#define OXR(func) OXR_CheckErrors(func, #func);
