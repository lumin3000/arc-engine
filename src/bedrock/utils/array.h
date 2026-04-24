#ifndef BALD_ARRAY_H
#define BALD_ARRAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>

#define DECLARE_ARRAY_TYPE(T, N, TypeName) \
    typedef struct { \
        T data[N]; \
        int32_t len; \
        T invalid_value; \
    } TypeName

#define ARRAY_HAS_INDEX(a, index) \
    ((index) >= 0 && (index) < (a)->len)

#define ARRAY_SLICE_PTR(a) \
    ((a)->data)

#define ARRAY_SLICE_LEN(a) \
    ((a)->len)

#define ARRAY_GET(a, index) \
    (assert(ARRAY_HAS_INDEX(a, index)), (a)->data[index])

#define ARRAY_GET_SAFE(a, index, out_value, out_ok) \
    do { \
        if ((index) < 0 || (index) >= (a)->len) { \
            *(out_ok) = false; \
        } else { \
            *(out_value) = (a)->data[index]; \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_GET_PTR(a, index) \
    (assert(ARRAY_HAS_INDEX(a, index)), &(a)->data[index])

#define ARRAY_GET_PTR_SAFE(a, index, out_ptr, out_ok) \
    do { \
        if ((index) < 0 || (index) >= (a)->len) { \
            *(out_ptr) = &(a)->invalid_value; \
            *(out_ok) = false; \
        } else { \
            *(out_ptr) = &(a)->data[index]; \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_SET(a, index, value) \
    do { \
        assert(ARRAY_HAS_INDEX(a, index)); \
        (a)->data[index] = (value); \
    } while(0)

#define ARRAY_SET_SAFE(a, index, value, out_ok) \
    do { \
        if ((index) < 0 || (index) >= (a)->len) { \
            *(out_ok) = false; \
        } else { \
            (a)->data[index] = (value); \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_PUSH(a, N, item) \
    (assert((a)->len < (N)), \
     (a)->data[(a)->len] = (item), \
     (a)->len++)

#define ARRAY_PUSH_SAFE(a, N, item, out_index, out_ok) \
    do { \
        if ((a)->len >= (N)) { \
            *(out_index) = 0; \
            *(out_ok) = false; \
        } else { \
            *(out_index) = (a)->len; \
            (a)->data[(a)->len] = (item); \
            (a)->len++; \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_PUSH_EMPTY(a, N, out_index, out_ok) \
    do { \
        if ((a)->len >= (N)) { \
            *(out_index) = 0; \
            *(out_ok) = false; \
        } else { \
            *(out_index) = (a)->len; \
            (a)->len++; \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_POP_BACK(a) \
    (assert((a)->len > 0), \
     (a)->len--, \
     (a)->data[(a)->len])

#define ARRAY_POP_BACK_SAFE(a, out_item, out_ok) \
    do { \
        if ((a)->len <= 0) { \
            *(out_ok) = false; \
        } else { \
            (a)->len--; \
            *(out_item) = (a)->data[(a)->len]; \
            *(out_ok) = true; \
        } \
    } while(0)

#define ARRAY_REMOVE(a, index) \
    do { \
        assert(ARRAY_HAS_INDEX(a, index)); \
        int32_t n = (a)->len - 1; \
        if ((index) != n) { \
            (a)->data[index] = (a)->data[n]; \
        } \
        (a)->len--; \
    } while(0)

#define ARRAY_CLEAR(a) \
    ((a)->len = 0)

#define ARRAY_FROM_SLICE(a, N, slice_ptr, slice_len) \
    do { \
        int32_t copy_count = (slice_len) < (N) ? (slice_len) : (N); \
        memcpy((a)->data, (slice_ptr), copy_count * sizeof((a)->data[0])); \
        (a)->len = copy_count; \
    } while(0)

DECLARE_ARRAY_TYPE(int, 256, Array_Int_256);
DECLARE_ARRAY_TYPE(float, 256, Array_Float_256);
DECLARE_ARRAY_TYPE(void*, 256, Array_Ptr_256);

#endif
