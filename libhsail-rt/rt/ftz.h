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

#ifndef PHSA_RT_FTZ_H
#define PHSA_RT_FTZ_H

#include <stdint.h>

uint64_t
__hsail_enable_ftz ();

uint64_t
__hsail_disable_ftz ();

void
__hsail_restore_ftz (uint64_t saved_flags);

float
__hsail_ftz_f32 (float a);

float
__hsail_ftz_f32_f16 (float a);

double
__hsail_ftz_f64 (double a);

#endif 
