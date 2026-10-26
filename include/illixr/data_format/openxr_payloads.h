#pragma once

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define IXR_TOPIC_FOO   0
#define IXR_TOPIC_FOO_ECHO   1
#define IXR_TOPIC_COUNT 2

#define IXR_DATA_TYPE_FOO     1
#define IXR_FOO_DATA_VERSION  1
#define IXR_DATA_TYPE_BAR_ECHO     2
#define IXR_FOO_ECHO_DATA_VERSION  1

typedef struct ixr_foo_data {
    float   values[4];
    int32_t count;
    int32_t padding;
} ixr_foo_data;

#ifdef __cplusplus
};
#endif
