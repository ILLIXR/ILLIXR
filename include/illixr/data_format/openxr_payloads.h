#pragma once

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Topic id's
// When adding new topics always increment from the last number used, do not reuse any numbers
#define IXR_TOPIC_FOO   0
#define IXR_TOPIC_FOO_ECHO   1
#define IXR_TOPIC_COUNT 2

// Define data types below
// Each type must have its own unique identifier and a version number
// When adding new data types always increment the data type identifier from the last number used, do not reuse any numbers
#define IXR_DATA_TYPE_FOO     1
#define IXR_FOO_DATA_VERSION  1

typedef struct ixr_foo_data {
    float   values[4];
    int32_t count;
    int32_t padding;
} ixr_foo_data;

#ifdef __cplusplus
};
#endif
