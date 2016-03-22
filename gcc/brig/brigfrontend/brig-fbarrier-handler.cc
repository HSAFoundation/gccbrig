/* brig-fbarrier-handler.cc -- brig fbarrier directive handling
   Copyright (C) 2016 Free Software Foundation, Inc.

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
/**
 * @author pekka.jaaskelainen@parmance.com for General Processor Tech. 2016
 */

#include "brig-code-entry-handler.h"

#include "stringpool.h"
#include "errors.h"

// Allocate this many bytes from the group segment for each fbarrier.
#define FBARRIER_STRUCT_SIZE 32

size_t
brig_directive_fbarrier_handler::operator() (const BrigBase *base)
{
  // Model fbarriers as group segment variables with fixed size
  // large enough to store whatever data the actual target needs
  // to store to maintain the barrier info. The handle is the
  // offset to the beginning of the object.
  parent_.append_group_variable (base, FBARRIER_STRUCT_SIZE, 1);
  parent_.m_cf->has_barriers = true;
  return base->byteCount;
}
