/* Tests -fftz-math flag */
/* { dg-do run { target x86_64-*-* } } */
/* { dg-options "-O2 -fftz-math" } */

#include <math.h>

#include "xmmintrin.h"
#include "pmmintrin.h"

union uf
{
  unsigned int u;
  float f;
};

static unsigned int
f2u (float v)
{
  union uf u;
  u.f = v;
  return u.u;
}

static void
enable_ftz_mode ()
{
  _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
}

static void
test_is_zero (float x)
{
  /* FTZ mode is on, must do bitwise ops for zero test. */
  if ((f2u (x) & 0x7fffffff) != 0u)
    abort ();
}

static void
test_is_subnormal (float x) {
  unsigned int u = f2u (x);
  if (u & 0x7f800000)
    abort ();
  if (!(u & 0x007fffff))
    abort ();
}

volatile float gf;

int
main ()
{
  enable_ftz_mode ();

  /* Circulate through volatile to avoid constant folding. */
  gf = 2.87E-42; /* = subnormal */
  float x = gf;

  test_is_subnormal (x); /* Store/load should not flush. */

  /* Test the expression is not simplified to plain x, thus, leaking the
     subnormal. */
  test_is_zero (x*1);
  test_is_zero (x*-1);
  test_is_zero (x*0.5);

  test_is_zero (x/1);
  test_is_zero (x/-1);
  test_is_zero (x/2);

  /* FP ops that should not flush. */
  test_is_subnormal (fabsf (x));
  test_is_subnormal (x < 0 ? -x : x);
  test_is_subnormal (-x);
  test_is_subnormal (copysignf (x, -1.0));

  /* Test constant folding. */

  test_is_subnormal (fabsf (-2.87E-42));
  test_is_subnormal (copysignf (2.87E-42, -1.0));

  /* floor and ceil must see flushed value. */
  test_is_zero (floorf (-2.87E-42));
  test_is_zero (ceilf (2.87E-42));

  return 0;
}
