/* workitems.c -- The main runtime entry that performs work-item execution in
   various ways and the builtin functions closely related to the 
   implementation.

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

/**
 * The fiber based multiple work-item work-group execution uses the Pth
 * library for user mode threading. However, if gccbrig is able to optimize the
 * kernel to a much faster work-group function that implements the multiple
 * WI execution using loops instead of fibers requiring slow context switches,
 * the fiber-based implementation won't be called.
 *
 * @author pekka.jaaskelainen@parmance.com for General Processor Tech.
 */

#include <pth.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "workitems.h"
#include "phsa-rt.h"

//#define BENCHMARK_PHSA_RT

#ifdef BENCHMARK_PHSA_RT
#include <stdio.h>
#include <time.h>

static uint64_t wi_count = 0;
static uint64_t wis_skipped = 0;
static uint64_t wi_total = 0;
static clock_t start_time;

#endif

#include <stdio.h>

//#define DEBUG_PHSA_RT
#ifdef DEBUG_PHSA_RT
#include <stdio.h>
#endif

// HSA requires WGs to be executed in flat work-group id order. Enabling
// the following macro can reveal test cases that rely on the ordering,
// but is not useful for much else.
// #define EXECUTE_WGS_BACKWARDS

uint32_t __phsa_builtin_workitemabsid (uint32_t dim, PHSAWorkItem *context);

uint32_t __phsa_builtin_workitemid (uint32_t dim, PHSAWorkItem *context);

uint32_t __phsa_builtin_gridgroups (uint32_t dim, PHSAWorkItem *context);

uint32_t __phsa_builtin_currentworkgroupsize (uint32_t dim, PHSAWorkItem *wi);

uint32_t __phsa_builtin_workgroupsize (uint32_t dim, PHSAWorkItem *wi);

static void
phsa_fatal_error (int code)
{
  exit (code);
}

// Pth-based work-item thread implementation.
static void *
phsa_work_item_thread (void *arg)
{
  PHSAWorkItem *wi = (PHSAWorkItem *) arg;
  volatile PHSAWorkGroup *wg = wi->wg;
  PHSAKernelLaunchData *l_data = wi->launch_data;

  do
    {
      int retcode
	= pth_barrier_reach ((pth_barrier_t *) l_data->wg_start_barrier);
      if (retcode == FALSE)
	phsa_fatal_error (1);

      /* At this point the threads can assume that either more_wgs is 0 or
	 the current_work_group_* is set to point to the WG executed next. */
      if (!wi->wg->more_wgs)
	break;
#ifdef DEBUG_PHSA_RT
      printf (
	"Running work-item %lu/%lu/%lu for wg %lu/%lu/%lu / %lu/%lu/%lu...\n",
	wi->x, wi->y, wi->z, wg->x, wg->y, wg->z, l_data->wg_max_x,
	l_data->wg_max_y, l_data->wg_max_z);
#endif

      if (wi->x < __phsa_builtin_currentworkgroupsize (0, wi)
	  && wi->y < __phsa_builtin_currentworkgroupsize (1, wi)
	  && wi->z < __phsa_builtin_currentworkgroupsize (2, wi))
	{
	  l_data->kernel (l_data->kernarg_addr, wi, wg->group_base_ptr,
			  wg->private_base_ptr);
#ifdef DEBUG_PHSA_RT
	  printf ("done.\n");
#endif
#ifdef BENCHMARK_PHSA_RT
	  wi_count++;
#endif
	}
      else
	{
#ifdef DEBUG_PHSA_RT
	  printf ("skipped (partial WG).\n");
#endif
#ifdef BENCHMARK_PHSA_RT
	  wis_skipped++;
#endif
	}

      retcode
	= pth_barrier_reach ((pth_barrier_t *) l_data->wg_completion_barrier);
      if (retcode == FALSE)
	phsa_fatal_error (2);

      /* If there's only one work-item, it returns PTH_BARRIER_TAILLIGHT,
	 not _HEADLIGHT. */
      if (retcode == PTH_BARRIER_TAILLIGHT)
	{
/* The last thread updates the WG to execute next. */
#ifdef EXECUTE_WGS_BACKWARDS
	  if (wg->x == l_data->wg_min_x)
	    {
	      wg->x = l_data->wg_max_x - 1;
	      if (wg->y == l_data->wg_min_y)
		{
		  wg->y = l_data->wg_max_y - 1;
		  if (wg->z == l_data->wg_min_z)
		    wg->more_wgs = 0;
		  else
		    wg->z--;
		}
	      else
		wg->y--;
	    }
	  else
	    wg->x--;
#else
	  if (wg->x + 1 >= l_data->wg_max_x)
	    {
	      wg->x = l_data->wg_min_x;
	      if (wg->y + 1 >= l_data->wg_max_y)
		{
		  wg->y = l_data->wg_min_y;
		  if (wg->z + 1 >= l_data->wg_max_z)
		    wg->more_wgs = 0;
		  else
		    wg->z++;
		}
	      else
		wg->y++;
	    }
	  else
	    wg->x++;
#endif

	  /* Reinitialize the work-group barrier according to the new WG's
	     size, which might not be the same as the previous ones, due
	     to "partial WGs". */
	  size_t wg_size = __phsa_builtin_currentworkgroupsize (0, wi)
			   * __phsa_builtin_currentworkgroupsize (1, wi)
			   * __phsa_builtin_currentworkgroupsize (2, wi);

#ifdef DEBUG_PHSA_RT
	  printf ("Reinitializing the WG barrier to %lu.\n", wg_size);
#endif
	  pth_barrier_init ((pth_barrier_t *) wi->launch_data->wg_sync_barrier,
			    wg_size);

#ifdef BENCHMARK_PHSA_RT
	  if (wi_count % 1000 == 0)
	    {
	      clock_t spent_time = clock () - start_time;
	      double spent_time_sec = (double) spent_time / CLOCKS_PER_SEC;
	      double wis_per_sec = wi_count / spent_time_sec;
	      uint64_t eta_sec
		= (wi_total - wi_count - wis_skipped) / wis_per_sec;

	      printf ("%lu WIs executed %lu skipped in %lus (%lu WIs/s, ETA in "
		      "%lu s)\n",
		      wi_count, wis_skipped, (uint64_t) spent_time_sec,
		      (uint64_t) wis_per_sec, (uint64_t) eta_sec);
	    }
#endif
	}
    }
  while (1);

  pth_exit (NULL);
}

#define MIN(a, b) ((a < b) ? a : b)
#define MAX(a, b) ((a > b) ? a : b)

/* Spawns a given number of work-items to execute a set of work-groups,
 * blocks until their completion. */
static void
phsa_execute_wi_gang (PHSAKernelLaunchData *context, void *group_base_ptr,
		      size_t wg_size_x, size_t wg_size_y, size_t wg_size_z)
{
  PHSAWorkItem wi_threads[PHSA_MAX_WG_SIZE];
  PHSAWorkGroup wg;
  size_t flat_wi_id = 0, x, y, z, max_x, max_y, max_z;
  pth_barrier_t wg_start_barrier;
  pth_barrier_t wg_completion_barrier;
  pth_barrier_t wg_sync_barrier;

  max_x = wg_size_x == 0 ? 1 : wg_size_x;
  max_y = wg_size_y == 0 ? 1 : wg_size_y;
  max_z = wg_size_z == 0 ? 1 : wg_size_z;

  size_t wg_size = max_x * max_y * max_z;
  if (wg_size > PHSA_MAX_WG_SIZE)
    phsa_fatal_error (2);

  wg.private_segment_total_size = context->dp->private_segment_size * wg_size;
  if (wg.private_segment_total_size > 0
      && posix_memalign (&wg.private_base_ptr, 256,
			 wg.private_segment_total_size)
	   != 0)
    phsa_fatal_error (3);

  wg.alloca_stack_p = wg.private_segment_total_size;
  wg.alloca_frame_p = wg.alloca_stack_p;

#ifdef EXECUTE_WGS_BACKWARDS
  wg.x = context->wg_max_x - 1;
  wg.y = context->wg_max_y - 1;
  wg.z = context->wg_max_z - 1;
#else
  wg.x = context->wg_min_x;
  wg.y = context->wg_min_y;
  wg.z = context->wg_min_z;
#endif

  pth_barrier_init (&wg_sync_barrier, wg_size);
  pth_barrier_init (&wg_start_barrier, wg_size);
  pth_barrier_init (&wg_completion_barrier, wg_size);

  context->wg_start_barrier = &wg_start_barrier;
  context->wg_sync_barrier = &wg_sync_barrier;
  context->wg_completion_barrier = &wg_completion_barrier;

  wg.more_wgs = 1;
  wg.group_base_ptr = group_base_ptr;

#ifdef BENCHMARK_PHSA_RT
  wi_count = 0;
  wis_skipped = 0;
  start_time = clock ();
#endif
  for (x = 0; x < max_x; ++x)
    for (y = 0; y < max_y; ++y)
      for (z = 0; z < max_z; ++z)
	{
	  PHSAWorkItem *wi = &wi_threads[flat_wi_id];
	  wi->launch_data = context;
	  wi->wg = &wg;
	  wi->x = x;
	  wi->y = y;
	  wi->z = z;

	  /* TODO: set PTH_ATTR_STACK_SIZE according to the private
		   segment size. Too big stack consumes huge amount of
		   memory in case of huge number of WIs and a too small stack
		   will fail in mysterious and potentially dangerous ways. */
	  wi->wi_thread_handle = pth_spawn (NULL, phsa_work_item_thread, wi);
	  ++flat_wi_id;
	}

  do
    {
      --flat_wi_id;
      pth_join (wi_threads[flat_wi_id].wi_thread_handle, NULL);
    }
  while (flat_wi_id > 0);

  if (wg.private_segment_total_size > 0)
    free (wg.private_base_ptr);
}

/* Spawn the work-item threads to execute work-groups and let
 * them execute all the WGs, including a potential partial WG. */
static void
phsa_spawn_work_items (PHSAKernelLaunchData *context, void *group_base_ptr)
{
  hsa_kernel_dispatch_packet_t *dp = context->dp;
  size_t x, y, z;

  /* TO DO: host-side memory management of group and private segment
     memory. Agents in general are less likely to support efficient dynamic mem
     allocation. */
  if (dp->group_segment_size > 0
      && posix_memalign (&group_base_ptr, 256, dp->group_segment_size) != 0)
    phsa_fatal_error (3);

  context->group_segment_start_addr = (size_t) group_base_ptr;

  pth_init ();

  /* HSA seems to allow the WG size to be larger than the grid size. We need to
     saturate the effective WG size to the grid size to prevent the extra WIs
     from executing. */
  size_t sat_wg_size_x, sat_wg_size_y, sat_wg_size_z, sat_wg_size;
  sat_wg_size_x = MIN (dp->workgroup_size_x, dp->grid_size_x);
  sat_wg_size_y = MIN (dp->workgroup_size_y, dp->grid_size_y);
  sat_wg_size_z = MIN (dp->workgroup_size_z, dp->grid_size_z);
  sat_wg_size = sat_wg_size_x * sat_wg_size_y * sat_wg_size_z;

#ifdef BENCHMARK_PHSA_RT
  wi_total = (uint64_t) dp->grid_size_x
	     * (dp->grid_size_y > 0 ? dp->grid_size_y : 1)
	     * (dp->grid_size_z > 0 ? dp->grid_size_z : 1);
#endif

  /* For now execute all work groups in a single coarse thread (does not utilize
     multicore/multithread). */
  context->wg_min_x = context->wg_min_y = context->wg_min_z = 0;

  int dims = dp->setup & 0x3;

  context->wg_max_x = ((uint64_t) dp->grid_size_x + dp->workgroup_size_x - 1)
		      / dp->workgroup_size_x;

  context->wg_max_y
    = dims < 2 ? 1 : ((uint64_t) dp->grid_size_y + dp->workgroup_size_y - 1)
		       / dp->workgroup_size_y;

  context->wg_max_z
    = dims < 3 ? 1 : ((uint64_t) dp->grid_size_z + dp->workgroup_size_z - 1)
		       / dp->workgroup_size_z;

#ifdef DEBUG_PHSA_RT
  printf ("### launching work-groups %lu/%lu/%lu to %lu/%lu/%lu with "
	  "wg size %lu/%lu/%lu grid size %u/%u/%u\n",
	  context->wg_min_x, context->wg_min_y, context->wg_min_z,
	  context->wg_max_x, context->wg_max_y, context->wg_max_z,
	  sat_wg_size_x, sat_wg_size_y, sat_wg_size_z, dp->grid_size_x,
	  dp->grid_size_y, dp->grid_size_z);
#endif

  phsa_execute_wi_gang (context, group_base_ptr, sat_wg_size_x, sat_wg_size_y,
			sat_wg_size_z);

  if (dp->group_segment_size > 0)
    free (group_base_ptr);

  pth_kill ();
}

/*
 * Executes the given work-group function for all work groups in the grid.
 *
 * A work-group function is a version of the original kernel which executes
 * the kernel for all work-items in a work-group. It is produced by gccbrig
 * if it can handle the kernel's barrier usage and is much faster way to
 * execute massive numbers of work-items in a non-SPMD machine than fibers
 * (easily 100x faster).
 */
static void
phsa_execute_work_groups (PHSAKernelLaunchData *context, void *group_base_ptr)
{
  hsa_kernel_dispatch_packet_t *dp = context->dp;
  size_t x, y, z, wg_x, wg_y, wg_z;

  /* TODO: host-side memory management of group and private segment
     memory. Agents in general are less likely to support efficient dynamic mem
     allocation. */
  if (dp->group_segment_size > 0
      && posix_memalign (&group_base_ptr, 256, dp->group_segment_size) != 0)
    phsa_fatal_error (3);

  context->group_segment_start_addr = (size_t) group_base_ptr;

  /* HSA seems to allow the WG size to be larger than the grid size. We need
     to saturate the effective WG size to the grid size to prevent the extra WIs
     from executing. */
  size_t sat_wg_size_x, sat_wg_size_y, sat_wg_size_z, sat_wg_size;
  sat_wg_size_x = MIN (dp->workgroup_size_x, dp->grid_size_x);
  sat_wg_size_y = MIN (dp->workgroup_size_y, dp->grid_size_y);
  sat_wg_size_z = MIN (dp->workgroup_size_z, dp->grid_size_z);
  sat_wg_size = sat_wg_size_x * sat_wg_size_y * sat_wg_size_z;

#ifdef BENCHMARK_PHSA_RT
  wi_total = (uint64_t) dp->grid_size_x
	     * (dp->grid_size_y > 0 ? dp->grid_size_y : 1)
	     * (dp->grid_size_z > 0 ? dp->grid_size_z : 1);
#endif

  context->wg_min_x = context->wg_min_y = context->wg_min_z = 0;

  int dims = dp->setup & 0x3;

  context->wg_max_x = ((uint64_t) dp->grid_size_x + dp->workgroup_size_x - 1)
		      / dp->workgroup_size_x;

  context->wg_max_y
    = dims < 2 ? 1 : ((uint64_t) dp->grid_size_y + dp->workgroup_size_y - 1)
		       / dp->workgroup_size_y;

  context->wg_max_z
    = dims < 3 ? 1 : ((uint64_t) dp->grid_size_z + dp->workgroup_size_z - 1)
		       / dp->workgroup_size_z;

#ifdef DEBUG_PHSA_RT
  printf ("### launching work-groups %lu/%lu/%lu to %lu/%lu/%lu with "
	  "wg size %lu/%lu/%lu grid size %u/%u/%u\n",
	  context->wg_min_x, context->wg_min_y, context->wg_min_z,
	  context->wg_max_x, context->wg_max_y, context->wg_max_z,
	  sat_wg_size_x, sat_wg_size_y, sat_wg_size_z, dp->grid_size_x,
	  dp->grid_size_y, dp->grid_size_z);
#endif

  PHSAWorkItem wi;
  PHSAWorkGroup wg;
  wi.wg = &wg;
  wi.x = wi.y = wi.z = 0;
  wi.launch_data = context;

#ifdef BENCHMARK_PHSA_RT
  start_time = clock ();
  uint64_t wg_count = 0;
#endif

  size_t wg_size = __phsa_builtin_workgroupsize (0, &wi)
		   * __phsa_builtin_workgroupsize (1, &wi)
		   * __phsa_builtin_workgroupsize (2, &wi);

  void *private_base_ptr = NULL;
  if (dp->private_segment_size > 0
      && posix_memalign (&private_base_ptr, 256,
			 dp->private_segment_size * wg_size)
	   != 0)
    phsa_fatal_error (3);

  wg.alloca_stack_p = dp->private_segment_size * wg_size;
  wg.alloca_frame_p = wg.alloca_stack_p;

  wg.private_base_ptr = private_base_ptr;
  wg.group_base_ptr = group_base_ptr;

#ifdef DEBUG_PHSA_RT
  printf ("priv seg size %u wg_size %lu @ %p\n", dp->private_segment_size,
	  wg_size, private_base_ptr);
#endif

  for (wg_z = context->wg_min_z; wg_z < context->wg_max_z; ++wg_z)
    for (wg_y = context->wg_min_y; wg_y < context->wg_max_y; ++wg_y)
      for (wg_x = context->wg_min_x; wg_x < context->wg_max_x; ++wg_x)
	{
	  wi.wg->x = wg_x;
	  wi.wg->y = wg_y;
	  wi.wg->z = wg_z;

	  context->kernel (context->kernarg_addr, &wi, group_base_ptr,
			   private_base_ptr);

#if defined(BENCHMARK_PHSA_RT) && 0
	  wg_count++;
	  if (wg_count % 1000000 == 0)
	    {
	      clock_t spent_time = clock () - start_time;
	      uint64_t wi_count = wg_x * sat_wg_size_x + wg_y * sat_wg_size_y
				  + wg_z * sat_wg_size_z;
	      double spent_time_sec = (double) spent_time / CLOCKS_PER_SEC;
	      double wis_per_sec = wi_count / spent_time_sec;
	      uint64_t eta_sec = (wi_total - wi_count) / wis_per_sec;

	      printf ("%lu WIs executed in %lus (%lu WIs/s, ETA in %lu s)\n",
		      wi_count, (uint64_t) spent_time_sec,
		      (uint64_t) wis_per_sec, (uint64_t) eta_sec);
	    }
#endif
	}

#ifdef BENCHMARK_PHSA_RT
  clock_t spent_time = clock () - start_time;
  double spent_time_sec = (double) spent_time / CLOCKS_PER_SEC;
  double wis_per_sec = wi_total / spent_time_sec;

  printf ("### %lu WIs executed in %lu s (%lu WIs / s)\n", wi_total,
	  (uint64_t) spent_time_sec, (uint64_t) wis_per_sec);
#endif

  if (dp->group_segment_size > 0)
    free (group_base_ptr);

  free (private_base_ptr);
  private_base_ptr = NULL;
}

/* gccbrig generates the following from each HSAIL kernel:

   1) The actual kernel function (a single work-item kernel or a work-group
      function) generated from HSAIL (BRIG).

	 static void _Kernel(void* args, void* context, void* group_base_ptr)
	 {
	   ...
	 }

  2) A public facing kernel function that is called from the PHSA runtime:

   a) A single work-item function (that requires fibers for multi-WI):

      void Kernel(void* context) {
	 __phsa_launch_kernel (_Kernel, context);
      }

      or

    b) a when gccbrig could generate a work-group function:

      void Kernel(void* context) {
         __phsa_launch_wg_function (_Kernel, context);
      }
*/

void
__phsa_launch_kernel (gccbrigKernelFunc kernel, PHSAKernelLaunchData *context,
		      void *group_base_ptr)
{
  context->kernel = kernel;
  phsa_spawn_work_items (context, group_base_ptr);
}

void
__phsa_launch_wg_function (gccbrigKernelFunc kernel,
			   PHSAKernelLaunchData *context, void *group_base_ptr)
{
  context->kernel = kernel;
  phsa_execute_work_groups (context, group_base_ptr);
}

uint32_t
__phsa_builtin_workitemabsid (uint32_t dim, PHSAWorkItem *context)
{
  hsa_kernel_dispatch_packet_t *dp = context->launch_data->dp;

  uint32_t id;
  switch (dim)
    {
    default:
    case 0:
      // Overflow semantics in the case of WG dim > grid dim.
      id = ((uint64_t) context->wg->x * dp->workgroup_size_x + context->x)
	   % dp->grid_size_x;
      break;
    case 1:
      id = ((uint64_t) context->wg->y * dp->workgroup_size_y + context->y)
	   % dp->grid_size_y;
      break;
    case 2:
      id = ((uint64_t) context->wg->z * dp->workgroup_size_z + context->z)
	   % dp->grid_size_z;
      break;
    }
  return id;
}

uint32_t
__phsa_builtin_workitemid (uint32_t dim, PHSAWorkItem *context)
{
  PHSAWorkItem *c = (PHSAWorkItem *) context;
  hsa_kernel_dispatch_packet_t *dp = context->launch_data->dp;

  // The number of dimensions is in the two least significant bits.
  int dims = dp->setup & 0x3;

  uint32_t id;
  switch (dim)
    {
    default:
    case 0:
      id = c->x;
      break;
    case 1:
      id = dims < 2 ? 0 : c->y;
      break;
    case 2:
      id = dims < 3 ? 0 : c->z;
      break;
    }
  return id;
}

uint32_t
__phsa_builtin_gridgroups (uint32_t dim, PHSAWorkItem *context)
{
  hsa_kernel_dispatch_packet_t *dp = context->launch_data->dp;
  int dims = dp->setup & 0x3;

  uint32_t id;
  switch (dim)
    {
    default:
    case 0:
      id = (dp->grid_size_x + dp->workgroup_size_x - 1) / dp->workgroup_size_x;
      break;
    case 1:
      id = dims < 2 ? 1 : (dp->grid_size_y + dp->workgroup_size_y - 1)
			    / dp->workgroup_size_y;
      break;
    case 2:
      id = dims < 3 ? 1 : (dp->grid_size_z + dp->workgroup_size_z - 1)
			    / dp->workgroup_size_z;
      break;
    }
  return id;
}

uint32_t
__phsa_builtin_workitemflatid (PHSAWorkItem *c)
{
  hsa_kernel_dispatch_packet_t *dp = c->launch_data->dp;

  return c->x + c->y * dp->workgroup_size_x
	 + c->z * dp->workgroup_size_x * dp->workgroup_size_y;
}

uint32_t
__phsa_builtin_currentworkitemflatid (PHSAWorkItem *c)
{
  hsa_kernel_dispatch_packet_t *dp = c->launch_data->dp;

  return c->x + c->y * __phsa_builtin_currentworkgroupsize (0, c)
	 + c->z * __phsa_builtin_currentworkgroupsize (0, c)
	     * __phsa_builtin_currentworkgroupsize (1, c);
}

void
__phsa_builtin_setworkitemid (uint32_t dim, uint32_t id, PHSAWorkItem *context)
{
  switch (dim)
    {
    default:
    case 0:
      context->x = id;
      break;
    case 1:
      context->y = id;
      break;
    case 2:
      context->z = id;
      break;
    }
}

uint64_t
__phsa_builtin_workitemflatabsid_u64 (PHSAWorkItem *context)
{
  PHSAWorkItem *c = (PHSAWorkItem *) context;
  hsa_kernel_dispatch_packet_t *dp = context->launch_data->dp;

  // work-item flattened absolute ID = ID0 + ID1 * max0 + ID2 * max0 * max1
  uint64_t id0 = __phsa_builtin_workitemabsid (0, context);
  uint64_t id1 = __phsa_builtin_workitemabsid (1, context);
  uint64_t id2 = __phsa_builtin_workitemabsid (2, context);

  uint64_t max0 = dp->grid_size_x;
  uint64_t max1 = dp->grid_size_y;
  uint64_t id = id0 + id1 * max0 + id2 * max0 * max1;

  return id;
}

uint32_t
__phsa_builtin_workitemflatabsid_u32 (PHSAWorkItem *context)
{
  PHSAWorkItem *c = (PHSAWorkItem *) context;
  hsa_kernel_dispatch_packet_t *dp = context->launch_data->dp;

  // work-item flattened absolute ID = ID0 + ID1 * max0 + ID2 * max0 * max1
  uint64_t id0 = __phsa_builtin_workitemabsid (0, context);
  uint64_t id1 = __phsa_builtin_workitemabsid (1, context);
  uint64_t id2 = __phsa_builtin_workitemabsid (2, context);

  uint64_t max0 = dp->grid_size_x;
  uint64_t max1 = dp->grid_size_y;
  uint64_t id = id0 + id1 * max0 + id2 * max0 * max1;
  return (uint32_t) id;
}

uint32_t
__phsa_builtin_currentworkgroupsize (uint32_t dim, PHSAWorkItem *wi)
{
  hsa_kernel_dispatch_packet_t *dp = wi->launch_data->dp;
  uint32_t wg_size = 0;
  switch (dim)
    {
    default:
    case 0:
      if ((uint64_t) wi->wg->x < dp->grid_size_x / dp->workgroup_size_x)
	wg_size = dp->workgroup_size_x; /* full WG */
      else
	wg_size = dp->grid_size_x % dp->workgroup_size_x; /* partial WG */
      break;
    case 1:
      if ((uint64_t) wi->wg->y < dp->grid_size_y / dp->workgroup_size_y)
	wg_size = dp->workgroup_size_y; /* full WG */
      else
	wg_size = dp->grid_size_y % dp->workgroup_size_y; /* partial WG */
      break;
    case 2:
      if ((uint64_t) wi->wg->z < dp->grid_size_z / dp->workgroup_size_z)
	wg_size = dp->workgroup_size_z; /* full WG */
      else
	wg_size = dp->grid_size_z % dp->workgroup_size_z; /* partial WG */
      break;
    }
  return wg_size;
}

uint32_t
__phsa_builtin_workgroupsize (uint32_t dim, PHSAWorkItem *wi)
{
  hsa_kernel_dispatch_packet_t *dp = wi->launch_data->dp;
  switch (dim)
    {
    default:
    case 0:
      return dp->workgroup_size_x;
    case 1:
      return dp->workgroup_size_y;
    case 2:
      return dp->workgroup_size_z;
    }
}

uint32_t
__phsa_builtin_gridsize (uint32_t dim, PHSAWorkItem *wi)
{
  hsa_kernel_dispatch_packet_t *dp = wi->launch_data->dp;
  switch (dim)
    {
    default:
    case 0:
      return dp->grid_size_x;
    case 1:
      return dp->grid_size_y;
    case 2:
      return dp->grid_size_z;
    }
}

uint32_t
__phsa_builtin_workgroupid (uint32_t dim, PHSAWorkItem *wi)
{
  switch (dim)
    {
    default:
    case 0:
      return wi->wg->x;
    case 1:
      return wi->wg->y;
    case 2:
      return wi->wg->z;
    }
}

uint32_t
__phsa_builtin_dim (PHSAWorkItem *wi)
{
  hsa_kernel_dispatch_packet_t *dp = wi->launch_data->dp;
  return dp->setup & 0x3;
}

uint64_t
__phsa_builtin_packetid (PHSAWorkItem *wi)
{
  return wi->launch_data->packet_id;
}

uint32_t
__phsa_builtin_packetcompletionsig_sig32 (PHSAWorkItem *wi)
{
  return (uint32_t) wi->launch_data->dp->completion_signal.handle;
}

uint64_t
__phsa_builtin_packetcompletionsig_sig64 (PHSAWorkItem *wi)
{
  return (uint64_t) (wi->launch_data->dp->completion_signal.handle);
}

void
__phsa_builtin_barrier (PHSAWorkItem *wi)
{
  pth_barrier_reach ((pth_barrier_t *) wi->launch_data->wg_sync_barrier);
}

//#define DEBUG_ALLOCA
/**
 * Return a 32b private segment address that points to a dynamically
 * allocated chunk of 'size' with 'align'.
 *
 * Allocates the space from the end of the private segment allocated
 * for the whole work group.  In implementations with separate private
 * memories per WI, we will need to have a stack pointer per WI. But in
 * the current implementation, the segment is shared, so we possibly
 * save some space in case all WIs do not call the alloca.
 *
 * The "alloca frames" are organized as follows:
 *
 * wg->alloca_stack_p points to the last allocated data (initially
 * outside the private segment)
 * wg->alloca_frame_p points to the first address _outside_ the current
 * function's allocations (initially to the same as alloca_stack_p)
 *
 * The data is allocated downwards from the end of the private segment.
 *
 * In the beginning of a new function which has allocas, a new alloca
 * frame is pushed which adds the current alloca_frame_p (the current
 * function's frame starting point) to the top of the alloca stack and
 * alloca_frame_p is set to the current stack position.
 *
 * At the exit points of a function with allocas, the alloca frame
 * is popped before returning. This involves popping the alloca_frame_p
 * to the one of the previous function in the call stack, and alloca_stack_p
 * similarly, to the position of the last word alloca'd by the previous
 * function.
 */
uint32_t
__phsa_builtin_alloca (uint32_t size, uint32_t align, PHSAWorkItem *wi)
{
  volatile PHSAWorkGroup *wg = wi->wg;
  uint32_t new_pos = wg->alloca_stack_p - size;
  while (new_pos % align != 0)
    new_pos--;
  wg->alloca_stack_p = new_pos;

#ifdef DEBUG_ALLOCA
  printf ("--- alloca (%u, %u) sp @%u fp @%u\n", size, align,
	  wg->alloca_stack_p, wg->alloca_frame_p);
#endif
  return new_pos;
}

/**
 * Initializes a new "alloca frame" in the private segment.
 *
 * This should be called at all the function entry points in case
 * the function contains at least one call to alloca.
 */
void
__phsa_builtin_alloca_push_frame (PHSAWorkItem *wi)
{
  volatile PHSAWorkGroup *wg = wi->wg;
/* Store the alloca_frame_p without any alignment padding so
   we know exactly where the previous frame ended after popping
   it. */
#ifdef DEBUG_ALLOCA
  printf ("--- push frame ");
#endif
  uint32_t last_word_offs = __phsa_builtin_alloca (4, 1, wi);
  memcpy (wg->private_base_ptr + last_word_offs,
	  (const void *) &wg->alloca_frame_p, 4);
  wg->alloca_frame_p = last_word_offs;

#ifdef DEBUG_ALLOCA
  printf ("--- sp @%u fp @%u\n", wg->alloca_stack_p, wg->alloca_frame_p);
#endif
}

/**
 * Frees the current "alloca frame" and restores the frame
 * pointer.
 *
 * This should be called at all the function return points in case
 * the function contains at least one call to alloca. Restores the
 * alloca stack to the condition it was before pushing the frame
 * the last time.
 */
void
__phsa_builtin_alloca_pop_frame (PHSAWorkItem *wi)
{
  volatile PHSAWorkGroup *wg = wi->wg;

  wg->alloca_stack_p = wg->alloca_frame_p;
  memcpy (&wg->alloca_frame_p,
	  (const void *) (wg->private_base_ptr + wg->alloca_frame_p), 4);
  /* Now frame_p points to the beginning of the previous function's
     frame and stack_p to its end. */

  wg->alloca_stack_p += 4;

#ifdef DEBUG_ALLOCA
  printf ("--- pop frame sp @%u fp @%u\n", wg->alloca_stack_p,
	  wg->alloca_frame_p);
#endif
}
