/* ftz.c -- Builtins for flush-to-zero routines.

   Copyright (C) 2015-2017 Free Software Foundation, Inc.
   Contributed by Henry Linjamaki <henry.linjamaki@parmance.com>
   for General Processor Tech.

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files
   (the "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "ftz.h"

#include <stdint.h>
#include <float.h>
#include <math.h>
#if defined(__x86_64__) || defined(__SSE__)
#  include "xmmintrin.h"
#  include "pmmintrin.h"
#endif

/* Sets flags to enable flush-to-zero mode if applicable on the device.
   Returns original flags prior to the call. */

#define INTEL_FTZ_POS 0u
#define INTEL_DAZ_POS 1u

uint64_t
__hsail_enable_ftz ()
{
#if defined(__x86_64__) || defined(__SSE__)
  uint32_t ftz_flag = _MM_GET_FLUSH_ZERO_MODE ();
  uint32_t daz_flag = _MM_GET_DENORMALS_ZERO_MODE();

  _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

  uint64_t saved_flags
    = (!!ftz_flag << INTEL_FTZ_POS) | (!!daz_flag << INTEL_DAZ_POS);
  return saved_flags;
#endif
}

/* Sets flags to disable flush-to-zero mode if applicable on the
   device. Returns original flags prior to the call. */

uint64_t
__hsail_disable_ftz ()
{
#if defined(__x86_64__) || defined(__SSE__)
  uint32_t ftz_flag = _MM_GET_FLUSH_ZERO_MODE ();
  uint32_t daz_flag = _MM_GET_DENORMALS_ZERO_MODE();

  _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_OFF);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);

  uint64_t saved_flags
    = (!!ftz_flag << INTEL_FTZ_POS) | (!!daz_flag << INTEL_DAZ_POS);
  return saved_flags;
#endif
}

/* Restores the original FTZ mode state saved by
   __hsail_{enable,disable}_ftz () call */

void
__hsail_restore_ftz (uint64_t saved_flags)
{
#if defined(__x86_64__) || defined(__SSE__)
  if (saved_flags & (1ul << INTEL_FTZ_POS))
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
  else
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_OFF);

  if (saved_flags & (1ul << INTEL_DAZ_POS))
    _MM_SET_FLUSH_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
  else
    _MM_SET_FLUSH_ZERO_MODE (_MM_DENORMALS_ZERO_OFF);
#endif
}

/* Flush the operand to zero in case it's a subnormal number.
   Do not cause any exceptions in case of NaNs.  */

float
__hsail_ftz_f32 (float a)
{
  union rep {
    float f;
    uint32_t i;
  } r;

  r.f = a;

  /* Only need to flush if the exponent bits are all zeros. */
  if (!(r.i & 0x7f800000u))
    {
      r.i &= 0x80000000u; /* Flush. Preserve sign. */
      return r.f;
    }
  else
    return a;
}

#define F16_MIN (6.10e-5)

/* Flush the single precision operand to zero in case it's considered
   a subnormal number in case it was a f16.  Do not cause any exceptions
   in case of NaNs.  */

float
__hsail_ftz_f32_f16 (float a)
{
  if (isnan (a) || isinf (a) || a == 0.0f)
      return a;

  float r = a;
  if (a < 0.0f)
    {
      if (-a < F16_MIN)
	r = -0.0f;
    }
  else
    {
      if (a < F16_MIN)
	r = 0.0f;
    }

  return r;
}

/* Flush the operand to zero in case it's a subnormal number.
   Do not cause any exceptions in case of NaNs.  */

double
__hsail_ftz_f64 (double a)
{
  if (isnan (a) || isinf (a) || a == 0.0d)
    return a;

  double r = a;
  if (a < 0.0d)
    {
      if (-a < DBL_MIN)
	r = -0.0d;
    }
  else
    {
      if (a < DBL_MIN)
	r = 0.0d;
    }

  return r;
}
