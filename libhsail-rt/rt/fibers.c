/* fibers.c -- extremely simple lightweight thread (fiber) implementation
   Copyright (C) 2016 Free Software Foundation, Inc.
   Contributed by Pekka Jaaskelainen <pekka.jaaskelainen@parmance.com>
   for General Processor Tech.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "target-config.h"

#include "fibers.h"

void
phsa_fatal_error (int code);

ucontext_t main_context;

/* The last fiber in the linked list.  */
static fiber_t *tail_fiber = NULL;
/* The first fiber in the linked list.  */
static fiber_t *head_fiber = NULL;
/* The fiber currently being executed.  */
static fiber_t *current_fiber = NULL;

/* Makecontext accepts only integer arguments.  We need to split the
   pointer argument in case pointer does not fit into int.  This helper
   function can be used to restore the pointer from the arguments. */

void *
fiber_int_args_to_ptr (int arg0, int arg1)
{
  void *ptr = NULL;
#if SIZEOF_VOIDP == 8 && SIZEOF_INT == 4
  ptr = (void*)(((uint64_t) arg0 & (uint64_t) 0xFFFFFFFF) |
		((uint64_t) arg1 << 32));
#elif SIZEOF_VOIDP == 4 && SIZEOF_INT == 4
  ptr = (void*)arg0;
#else
# error Unsupported pointer/int size.
#endif
  return ptr;
}

void
fiber_init (fiber_t *fiber, fiber_function_t start_function, void *arg,
	    size_t stack_size, size_t stack_align)
{
  int arg0, arg1;
  if (getcontext (&fiber->context) != 0)
    phsa_fatal_error (3);
  if (posix_memalign (&fiber->context.uc_stack.ss_sp, stack_align, stack_size)
      != 0)
    phsa_fatal_error (4);
  fiber->context.uc_stack.ss_size = stack_size;
  fiber->context.uc_link = &main_context;

  /* makecontext() accepts only integer arguments.  Split the
     pointer argument to two args in the case pointer does not fit
     into one int.  */
#if SIZEOF_VOIDP == 8 && SIZEOF_INT == 4
  arg0 = (int32_t) 0xFFFFFFFF & (uint64_t)arg;
  arg1 = (int32_t) 0xFFFFFFFF & ((uint64_t)arg >> 32);
#elif SIZEOF_VOIDP == 4 && SIZEOF_INT == 4
  arg0 = (int)arg;
  arg1 = 0;
#else
# error Unsupported pointer/int size.
#endif

  makecontext (&fiber->context, (void*)start_function, 2, arg0, arg1);

  fiber->status = FIBER_STATUS_READY;
  fiber->next = NULL;
  fiber->prev = NULL;

  /* Create a linked list of the created fibers.  Append the new one at
     the end.  */
  if (tail_fiber == NULL)
    tail_fiber = fiber;
  else
    {
      tail_fiber->next = fiber;
      fiber->prev = tail_fiber;
      tail_fiber = fiber;
    }

  if (head_fiber == NULL)
    head_fiber = fiber;
}

void
fiber_exit ()
{
  fiber_status_t old_status = current_fiber->status;
  current_fiber->status = FIBER_STATUS_EXITED;
#ifdef DEBUG_PHSA_FIBERS
  printf ("%p exited\n", current_fiber);
#endif
  if (old_status == FIBER_STATUS_JOINED)
    /* In case this thread has been joined, return back to the joiner.  */
    swapcontext (&current_fiber->context, &main_context);
  else
    /* In case the thread exited while being yielded from another thread,
       switch back to another fiber.  */
    fiber_yield ();
}

void
fiber_join (fiber_t *fiber)
{
  fiber_t *next_ready_fiber = NULL;
  current_fiber = fiber;
  if (fiber->status != FIBER_STATUS_EXITED)
    {
      fiber->status = FIBER_STATUS_JOINED;
      while (fiber->status != FIBER_STATUS_EXITED)
	swapcontext (&main_context, &fiber->context);
    }

  /* Remove the successfully joined fiber from the linked list so we won't
     access it later (the fiber itself might be freed after the join). */
  if (fiber->prev != NULL)
    fiber->prev->next = fiber->next;

  if (fiber->next != NULL)
    fiber->next->prev = fiber->prev;

  if (head_fiber == fiber)
    head_fiber = fiber->next;

  if (tail_fiber == fiber)
    tail_fiber = fiber->prev;

  free (fiber->context.uc_stack.ss_sp);
}

void
fiber_yield ()
{
  fiber_t *next_ready_fiber = current_fiber;

  if (current_fiber == head_fiber &&
      current_fiber == tail_fiber)
    {
      /* If the last fiber exits independently, there is no
	 fiber to switch to.  Switch to the main context in that
	 case.  */
      if (current_fiber->status == FIBER_STATUS_EXITED)
	{
#ifdef DEBUG_PHSA_FIBERS
	  printf ("%p is last, switching back to main\n", current_fiber);
#endif
	  swapcontext (&current_fiber->context, &main_context);
	}
    }

  do {
    next_ready_fiber = next_ready_fiber->next != NULL ?
      next_ready_fiber->next : head_fiber;
  } while (next_ready_fiber != current_fiber &&
	   next_ready_fiber->status == FIBER_STATUS_EXITED);

  fiber_t *old_current_fiber = current_fiber;
  current_fiber = next_ready_fiber;
  swapcontext (&old_current_fiber->context, &next_ready_fiber->context);
}

size_t
fiber_barrier_reach (fiber_barrier_t *barrier)
{
  /* Yield once to ensure that there are no fibers waiting for
     a previous triggering of the barrier in the waiting_count
     loop.  This should release them before we update the reached
     counter again. */
  fiber_yield ();

  barrier->reached++;
#ifdef DEBUG_PHSA_FIBERS
  printf ("reached %p %d of %d\n", barrier, barrier->reached,
	  barrier->threshold);
#endif

  ++barrier->waiting_count;
  while (barrier->reached < barrier->threshold)
    fiber_yield ();
  --barrier->waiting_count;

  /* Wait until all the fibers have reached this point.  */
  while (barrier->waiting_count > 0)
    fiber_yield ();

  /* Now all fibers have been released from the barrier waiting
     loop.   We can now safely reset the reach count for new triggering.  */
  if (barrier->reached > 0)
    {
      barrier->reached = 0;
      return 0;
    }
  return 1;
}

void
fiber_barrier_init (fiber_barrier_t *barrier, size_t threshold)
{
  barrier->threshold = threshold;
  barrier->waiting_count = 0;
  barrier->reached = 0;
}
