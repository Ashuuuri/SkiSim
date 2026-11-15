// 15.16 fixed-point helpers, following the course convention (fix15).
// The RP2040 has no hardware FPU, so filtering and game physics use these.
#ifndef SKISIM_FIX15_H
#define SKISIM_FIX15_H

#include <stdint.h>

typedef int32_t fix15;

#define multfix15(a, b)   ((fix15)((((int64_t)(a)) * ((int64_t)(b))) >> 15))
#define float2fix15(a)    ((fix15)((a) * 32768.0f))
#define fix2float15(a)    ((float)(a) / 32768.0f)
#define absfix15(a)       ((a) < 0 ? -(a) : (a))
#define int2fix15(a)      ((fix15)((a) << 15))
#define fix2int15(a)      ((int)((a) >> 15))
#define divfix(a, b)      ((fix15)((((int64_t)(a)) << 15) / (b)))

#endif // SKISIM_FIX15_H
