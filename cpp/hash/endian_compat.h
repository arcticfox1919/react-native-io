#pragma once
// Clang/GCC provide these on Darwin, Android and OpenHarmony; Darwin has no
// Linux <endian.h>. MSVC targets are little endian.
#if !defined(__BYTE_ORDER) && defined(__BYTE_ORDER__)
#define __BYTE_ORDER __BYTE_ORDER__
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
