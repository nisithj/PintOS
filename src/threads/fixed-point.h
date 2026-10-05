#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

#define F (1 << 14)   /* 16384, the scaling factor for 17.14 fixed-point */

/* int -> fixed-point */
static inline int
int_to_fp (int n)
{
  return n * F;
}

/* fixed-point -> int, truncating toward zero */
static inline int
fp_to_int (int x)
{
  return x / F;
}

/* fixed-point -> int, rounding to nearest */
static inline int
fp_to_int_round (int x)
{
  if (x >= 0)
    return (x + F / 2) / F;
  else
    return (x - F / 2) / F;
}

/* add two fixed-point values */
static inline int
add_fp (int x, int y)
{
  return x + y;
}

/* subtract two fixed-point values */
static inline int
sub_fp (int x, int y)
{
  return x - y;
}

/* add a fixed-point value and an int */
static inline int
add_fp_int (int x, int n)
{
  return x + n * F;
}

/* subtract an int from a fixed-point value */
static inline int
sub_fp_int (int x, int n)
{
  return x - n * F;
}

/* multiply two fixed-point values */
static inline int
mul_fp (int x, int y)
{
  return ((int64_t) x) * y / F;
}

/* multiply a fixed-point value by an int */
static inline int
mul_fp_int (int x, int n)
{
  return x * n;
}

/* divide two fixed-point values */
static inline int
div_fp (int x, int y)
{
  return (((int64_t) x) * F) / y;
}

/* divide a fixed-point value by an int */
static inline int
div_fp_int (int x, int n)
{
  return x / n;
}

#endif