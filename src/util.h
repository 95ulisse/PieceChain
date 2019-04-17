#ifndef __UTIL_H__
#define __UTIL_H__

#define MIN(a, b) \
    __extension__({ \
        __typeof__(a) __a = (a); \
        __typeof__(b) __b = (b); \
        __a < __b ? __a : __b; \
    })

#define MAX(a, b) \
    __extension__({ \
        __typeof__(a) __a = (a); \
        __typeof__(b) __b = (b); \
        __a > __b ? __a : __b; \
    })

#ifndef offsetof
#define offsetof(type, member) ((size_t) &(((type*) 0)->member))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    __extension__({ \
	    void* __mptr = (void*)(ptr); \
	    ((type*) (__mptr - offsetof(type, member))); \
    })
#endif

#endif