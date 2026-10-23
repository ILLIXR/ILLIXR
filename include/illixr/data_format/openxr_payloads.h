#pragma once

#include <stdint.h>

#define IXR_TOPIC_FOO   0
#define IXR_TOPIC_BAR   1
#define IXR_TOPIC_COUNT 2

#define IXR_DATA_TYPE_FOO     1
#define IXR_FOO_DATA_VERSION  1
#define IXR_DATA_TYPE_BAR     2
#define IXR_BAR_DATA_VERSION  1

typedef struct ixr_foo_data {
    float   values[4];
    int32_t count;
    int32_t padding;
} ixr_foo_data;

typedef struct ixr_bar_data {
    float values[2];
    int32_t count;
    int32_t padding;
} ixr_bar_data;
