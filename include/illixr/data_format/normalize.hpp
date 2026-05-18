#pragma once

namespace ILLIXR {
/*
 * Normalize the coordinates, using the input size as a reference
 */
template<typename T>
inline void normalize(T& obj, const float width, const float height, const float depth) {
    if (obj.normalized) {
        return;
    }
    obj.x() /= width;
    obj.y() /= height;
    obj.z() /= depth;
    obj.normalized = true;
}

template<typename T>
inline void normalize(T& obj, const float width, const float height) {
    normalize<T>(obj, width, height, 1.);
}

/*
 * Denormalize the coordinates, using the input size as reference
 */
template<typename T>
inline void denormalize(T& obj, const float width, const float height, const float depth) {
    if (!obj.valid)
        return;
    if (!obj.normalized) {
        return;
    }

    obj.x() *= width;
    obj.y() *= height;
    obj.z() *= depth;
    obj.normalized = false;
}

template<typename T>
inline void denormalize(T& obj, const float width, const float height) {
    denormalize<T>(obj, width, height, 1.);
}
} // namespace ILLIXR::data_format
