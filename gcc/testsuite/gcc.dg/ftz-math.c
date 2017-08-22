/* Tests -fftz-math flag */
/* { dg-do run { target x86_64-*-* } } */
/* { dg-options "-O2 -fftz-math" } */

#include <math.h>

/* #define DEBUG_TEST */
#ifdef DEBUG_TEST
#  include <stdio.h>
#endif

#include "xmmintrin.h"
#include "pmmintrin.h"

union uf
{
  unsigned int u;
  float f;
};

union ud
{
  unsigned long long u;
  double d;
};

static unsigned int
f2u (float v)
{
  union uf u;
  u.f = v;
  return u.u;
}

static unsigned long long
d2u (double v)
{
  union ud u;
  u.d = v;
  return u.u;
}


static void
enable_ftz_mode ()
{
  _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
}

static int
test_sf_is_zero (float x)
{
  /* FTZ mode is on, must do bitwise ops for zero test. */
  return ((f2u (x) & 0x7fffffffu) == 0u);
}

static int
test_df_is_zero (double x)
{
  /* FTZ mode is on, must do bitwise ops for zero test. */
  return ((d2u (x) & 0x7fffffffffffffffull) == 0ull);
}

static int
test_sf_is_subnormal (float x) {
  unsigned int u = f2u (x);
  if (u & 0x7f800000u)
    return 0;
  return (u & 0x007fffffu);
}

static int
test_df_is_subnormal (double x) {
  unsigned long long u = d2u (x);
  if (u & 0x7ff0000000000000ull)
    return 0;
  return (u & 0x000fffffffffffffull);
}

#ifdef DEBUG_TEST
void err_print (unsigned line, const char* expr)
{
  printf ("Line %d: FAIL: %s\n", line, expr);
  abort ();
}
#  define TEST_SF_IS_ZERO(expr) \
  if (!test_sf_is_zero (expr)) err_print (__LINE__, #expr)
#  define TEST_SF_IS_SUBNORMAL(expr) \
  if (!test_sf_is_subnormal (expr)) err_print (__LINE__, #expr)
#  define TEST_DF_IS_ZERO(expr) \
  if (!test_df_is_zero (expr)) err_print (__LINE__, #expr)
#  define TEST_DF_IS_SUBNORMAL(expr) \
  if (!test_df_is_subnormal (expr)) err_print (__LINE__, #expr)
#  define TEST_TRUE(expr) if (!(expr)) err_print (__LINE__, #expr)
#else
#  define TEST_SF_IS_ZERO(expr) if (!test_sf_is_zero (expr)) abort ()
#  define TEST_SF_IS_SUBNORMAL(expr) if (!test_sf_is_subnormal (expr)) abort ()
#  define TEST_DF_IS_ZERO(expr) if (!test_df_is_zero (expr)) abort ()
#  define TEST_DF_IS_SUBNORMAL(expr) if (!test_df_is_subnormal (expr)) abort ()
#  define TEST_TRUE(expr) if (!(expr)) abort ()
#endif

volatile float sf;
volatile double df;

int
main ()
{
  enable_ftz_mode ();

  /* Circulate through volatile to avoid constant folding. */
  sf = 2.87E-42f; /* = subnormal */
  float x = sf;

  TEST_SF_IS_SUBNORMAL (x); /* Store/load should not flush. */
  TEST_TRUE (!isnormal (x));
  TEST_TRUE (fpclassify (x) == FP_SUBNORMAL);

  TEST_DF_IS_ZERO ((double) x);

  /* Test the expression is not simplified to plain x, thus, leaking the
     subnormal. */
  TEST_SF_IS_ZERO (x * 1);
  TEST_SF_IS_ZERO (x * -1);
  TEST_SF_IS_ZERO (x * 0.5);

  TEST_SF_IS_ZERO (x / 1);
  TEST_SF_IS_ZERO (x / -1);
  TEST_SF_IS_ZERO (x / 2);

  TEST_SF_IS_ZERO (fminf (x, x));
  TEST_SF_IS_ZERO (fminf (x, -x));
  TEST_SF_IS_ZERO (fmaxf (x, x));
  TEST_SF_IS_ZERO (fmaxf (x, -x));

  TEST_SF_IS_ZERO (x + 0);
  TEST_SF_IS_ZERO (0 - x);

  TEST_SF_IS_ZERO (x - 0);
  TEST_SF_IS_ZERO (x + x);

  TEST_SF_IS_ZERO (x + 0.0f);
  TEST_SF_IS_ZERO (0.0f - x);
  TEST_SF_IS_ZERO (x - 0.0f);
  TEST_SF_IS_ZERO (x + x);

  TEST_SF_IS_ZERO (x * copysignf (1.0f, x));
  TEST_SF_IS_ZERO (x * copysignf (1.0f, -x));

  float y = sf;
  TEST_SF_IS_ZERO (fminf (fmaxf (x, y), y));

  TEST_SF_IS_SUBNORMAL (x == y ? x : y);
  TEST_SF_IS_SUBNORMAL (x != y ? x : y);
  TEST_SF_IS_SUBNORMAL (x >= y ? x : y);
  TEST_SF_IS_SUBNORMAL (x > y ? x : y);
  TEST_SF_IS_SUBNORMAL (x <= y ? x : y);
  TEST_SF_IS_SUBNORMAL (x < y ? x : y);

  /* FP ops that should not flush. */
  TEST_SF_IS_SUBNORMAL (fabsf (x));
  TEST_SF_IS_SUBNORMAL (x < 0 ? -x : x);
  TEST_SF_IS_SUBNORMAL (-x);
  TEST_SF_IS_SUBNORMAL (copysignf (x, -1.0));

  /* Test constant folding with subnormal values. */
  TEST_TRUE (!isnormal (2.87E-42f));

  TEST_SF_IS_SUBNORMAL (-(2.87E-42f));
  TEST_SF_IS_SUBNORMAL (fabsf (-2.87E-42f));
  TEST_SF_IS_SUBNORMAL (copysignf (2.87E-42f, -1.0));

  TEST_SF_IS_ZERO (fminf (2.87E-42f, 2.87E-42f));
  TEST_SF_IS_ZERO (fminf (2.87E-42f, -5.74E-42f));
  TEST_SF_IS_ZERO (fmaxf (2.87E-42f, 2.87E-42f));
  TEST_SF_IS_ZERO (fmaxf (2.87E-42f, -5.74E-42f));

  TEST_SF_IS_ZERO (floorf (-2.87E-42f));
  TEST_SF_IS_ZERO (ceilf (2.87E-42f));

  TEST_SF_IS_ZERO (sqrtf (2.82E-42f));

  TEST_SF_IS_ZERO (2.87E-42f + 0.0f);
  TEST_SF_IS_ZERO (2.87E-42f + 5.74E-42f);
  TEST_SF_IS_ZERO (2.87E-42f - 0.0f);
  TEST_SF_IS_ZERO (0.0f - 2.87E-42f);
  TEST_SF_IS_ZERO (2.87E-42f * 1.0f);
  TEST_SF_IS_ZERO (2.87E-42f * -1.0f);
  TEST_SF_IS_ZERO (2.87E-42f * 12.3f);
  TEST_SF_IS_ZERO (2.87E-42f / 1.0f);
  TEST_SF_IS_ZERO (2.87E-42f / 12.3f);

  TEST_TRUE (2.87E-42f == -5.74E-42f);
  TEST_TRUE (2.87E-42f == -5.74E-42f ? 1 : 0);

  TEST_TRUE (2.87E-42f == -5.74E-42f ? 1 : 0);
  TEST_TRUE (2.87E-42f != -5.74E-42f ? 0 : 1);
  TEST_TRUE (2.87E-42f >= -5.74E-42f ? 1 : 0);
  TEST_TRUE (2.87E-42f >= 5.74E-42f ? 1 : 0);
  TEST_TRUE (2.87E-42f > -5.74E-42f ? 0 : 1);
  TEST_TRUE (2.87E-42f > 5.74E-42f ? 0 : 1);
  TEST_TRUE (2.87E-42f <= -5.74E-42f ? 1 : 0);
  TEST_TRUE (2.87E-42f <= 5.74E-42f ? 1 : 0);
  TEST_TRUE (2.87E-42f < -5.74E-42f ? 0 : 1);
  TEST_TRUE (2.87E-42f < 5.74E-42f ? 0 : 1);

  /*  A < B ? A : B -> min (B, A)  must not happen (min flushes to zero).*/
  TEST_SF_IS_SUBNORMAL (2.87E-42f < -5.74E-42f ? 2.87E-42f : -5.74E-42f);

  /* Normal and subnormal input. */
  TEST_TRUE ((2.87E-42f + 1.1754944E-38f) == 1.1754944E-38f);
  TEST_TRUE ((1.1754944E-38f - 2.87E-42f) == 1.1754944E-38f);

  /* Expression with normal numbers. Result of the Substraction is
     subnormal. */
  float sf_tmp = (1.469368E-38f - 1.1754944E-38f) + 1.1754944E-38f;
  TEST_TRUE (sf_tmp == 1.1754944E-38f);


  /*** Test with double precision. ***/
  df = 5.06E-321;
  double dx = df;

  TEST_DF_IS_SUBNORMAL (dx);
  TEST_TRUE (!isnormal (dx));
  TEST_TRUE (fpclassify (dx) == FP_SUBNORMAL);

  TEST_SF_IS_ZERO ((float)dx);

  TEST_DF_IS_ZERO (dx * 1);
  TEST_DF_IS_ZERO (dx * -1);
  TEST_DF_IS_ZERO (dx * 0.5);

  TEST_DF_IS_ZERO (dx / 1);
  TEST_DF_IS_ZERO (dx / -1);
  TEST_DF_IS_ZERO (dx / 2);

  TEST_DF_IS_ZERO (fmin (dx, dx));
  TEST_DF_IS_ZERO (fmin (dx, -dx));
  TEST_DF_IS_ZERO (fmax (dx, dx));
  TEST_DF_IS_ZERO (fmax (dx, -dx));

  TEST_DF_IS_ZERO (dx + 0);
  TEST_DF_IS_ZERO (0 - dx);
  TEST_DF_IS_ZERO (dx - 0);
  TEST_DF_IS_ZERO (dx + dx);

  TEST_DF_IS_ZERO (dx + 0.0);
  TEST_DF_IS_ZERO (0.0 - dx);
  TEST_DF_IS_ZERO (dx - 0.0);

  TEST_DF_IS_ZERO (dx * copysign (1.0, dx));
  TEST_DF_IS_ZERO (dx * copysign (1.0, -dx));

  df = -1.61895E-319;
  double dy = df;
  TEST_SF_IS_ZERO (fmin (fmax (dx, dy), dy));

  TEST_DF_IS_SUBNORMAL (dx == dy ? dx : dy);
  TEST_DF_IS_SUBNORMAL (dx != dy ? dx : dy);
  TEST_DF_IS_SUBNORMAL (dx >= dy ? dx : dy);
  TEST_DF_IS_SUBNORMAL (dx > dy ? dx : dy);
  TEST_DF_IS_SUBNORMAL (dx <= dy ? dx : dy);
  TEST_DF_IS_SUBNORMAL (dx < dy ? dx : dy);

  /* FP ops that should not flush. */
  TEST_DF_IS_SUBNORMAL (fabs (dx));
  TEST_DF_IS_SUBNORMAL (dx < 0 ? -dx : dx);
  TEST_DF_IS_SUBNORMAL (-dx);
  TEST_DF_IS_SUBNORMAL (copysign (dx, -1.0));

  /* Test constant folding with subnormal values. */

  TEST_TRUE (!isnormal (5.06E-321));
  TEST_TRUE (fpclassify (5.06E-321) == FP_SUBNORMAL);

  TEST_DF_IS_SUBNORMAL (-(5.06E-321));
  TEST_DF_IS_SUBNORMAL (fabs (-5.06E-321));
  TEST_DF_IS_SUBNORMAL (copysign (5.06E-321, -1.0));

  TEST_DF_IS_ZERO (fmin (5.06E-321, 5.06E-321));
  TEST_DF_IS_ZERO (fmin (5.06E-321, -1.61895E-319));
  TEST_DF_IS_ZERO (fmax (5.06E-321, 5.06E-321));
  TEST_DF_IS_ZERO (fmax (5.06E-321, -1.61895E-319));

  TEST_DF_IS_ZERO (floor (-5.06E-321));
  TEST_DF_IS_ZERO (ceil (5.06E-321));
  TEST_DF_IS_ZERO (sqrt (2.82E-42f));

  TEST_DF_IS_ZERO (5.06E-321 + 0.0);
  TEST_DF_IS_ZERO (5.06E-321 + 1.61895E-319);
  TEST_DF_IS_ZERO (5.06E-321 - 0.0);
  TEST_DF_IS_ZERO (0.0 - 5.06E-321);
  TEST_DF_IS_ZERO (5.06E-321 * 1.0);
  TEST_DF_IS_ZERO (5.06E-321 * -1.0);
  TEST_DF_IS_ZERO (5.06E-321 * 12.3);
  TEST_DF_IS_ZERO (5.06E-321 / 1.0);
  TEST_DF_IS_ZERO (5.06E-321 / 12.3);

  TEST_TRUE (5.06E-321 == -1.61895E-319);

  TEST_TRUE (5.06E-321 == -1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 == -1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 != -1.61895E-319 ? 0 : 1);
  TEST_TRUE (5.06E-321 >= -1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 >= 1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 > -1.61895E-319 ? 0 : 1);
  TEST_TRUE (5.06E-321 > 1.61895E-319 ? 0 : 1);
  TEST_TRUE (5.06E-321 <= -1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 <= 1.61895E-319 ? 1 : 0);
  TEST_TRUE (5.06E-321 < -1.61895E-319 ? 0 : 1);
  TEST_TRUE (5.06E-321 < 1.61895E-319 ? 0 : 1);

  /*  A < B ? A : B -> min (B, A)  must not happen (min flushes to zero).*/
  TEST_DF_IS_SUBNORMAL (5.06E-321 < -1.61895E-319 ? 5.06E-321 : -1.61895E-319);

  /* Normal and subnormal input. */
  TEST_TRUE ((5.06E-321 + 3.33E-308) == 3.33E-308);
  TEST_TRUE ((3.33E-308 - 5.06E-321) == 3.33E-308);

  /* Expression with normal numbers. Result of the Substraction is
     subnormal. */
  double df_a = 3.33E-308;
  double df_b = 2.78E-308;
  double df_tmp = (df_a - df_b) + df_b;
  TEST_TRUE (df_tmp == df_b);

  return 0;
}
