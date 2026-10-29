#ifndef CMT_TYPEDEFS_H
#define CMT_TYPEDEFS_H

/* Types used by the CMOSTEK driver; no STM32 headers or keyword-like macros. */
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t BOOL;

#ifndef TRUE
#define TRUE ((BOOL)1U)
#endif
#ifndef FALSE
#define FALSE ((BOOL)0U)
#endif

#endif
