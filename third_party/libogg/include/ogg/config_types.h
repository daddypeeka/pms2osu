#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* This file is generated from config_types.h.in for the bundled libogg
   source (no autotools/cmake configure step is run when building
   pms2osu-v2). Values below are what configure produces on a normal
   64-bit Linux / macOS host. */

#define INCLUDE_INTTYPES_H 1
#define INCLUDE_STDINT_H 1
#define INCLUDE_SYS_TYPES_H 1

#if INCLUDE_INTTYPES_H
#  include <inttypes.h>
#endif
#if INCLUDE_STDINT_H
#  include <stdint.h>
#endif
#if INCLUDE_SYS_TYPES_H
#  include <sys/types.h>
#endif

typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#endif
