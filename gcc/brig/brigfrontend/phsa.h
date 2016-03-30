/* phsa.h -- phsa interfacing
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

#ifndef PHSA_H
#define PHSA_H

typedef struct
{
  /* The size of the group segment used by the kernel.  */
  unsigned group_segment_size;
  /* Size of the private segment used by a single work-item.  */
  unsigned private_segment_size;
  /* Total size of the kernel arguments.  */
  unsigned kernarg_segment_size;
  /* Maximum alignment of a kernel argument variable.  */
  unsigned kernarg_max_align;
} phsa_kernel_descriptor;

#define PHSA_KERNELDESC_SECTION_PREFIX "phsa.kerneldesc."

#endif
