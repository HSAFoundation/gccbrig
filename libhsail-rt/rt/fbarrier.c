/* fbarrier.c -- HSAIL fbarrier built-ins.

   Copyright (C) 2015-2016 Free Software Foundation, Inc.
   Contributed by Pekka Jaaskelainen <pekka.jaaskelainen@parmance.com>
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

#include <pth.h>
#include <stdlib.h>
#include <signal.h>

#include "workitems.h"
#include "phsa-rt.h"

typedef struct
{
  volatile uint32_t member_count;
  volatile uint32_t arrive_count;
  volatile uint32_t wait_count;
  /* No need for a mutex here as there's no real parallel
     execution between work-items in the simple fiber-based
     implementation.  */
} fbarrier;

void
__phsa_builtin_initfbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  fbar->member_count = 0;
  fbar->arrive_count = 0;
  fbar->wait_count = 0;
}

void
__phsa_builtin_releasefbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  fbar->member_count = 0;
  fbar->arrive_count = 0;
  fbar->wait_count = 0;
}

void
__phsa_builtin_joinfbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  ++fbar->member_count;
}

void
__phsa_builtin_leavefbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  --fbar->member_count;
}

void
__phsa_builtin_waitfbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  ++fbar->arrive_count;
  ++fbar->wait_count;
  while (fbar->arrive_count < fbar->member_count)
    pth_yield (NULL);
  --fbar->wait_count;
  while (fbar->wait_count > 0)
    pth_yield (NULL);
  fbar->arrive_count = 0;
}

void
__phsa_builtin_arrivefbar (uint32_t addr, PHSAWorkItem *wi)
{
  fbarrier *fbar = (fbarrier *) (wi->wg->group_base_ptr + addr);
  ++fbar->arrive_count;
}
