/* brig-queue-inst-handler.cc -- brig user mode queue related instruction
handling
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

#include <sstream>

#include "brig-code-entry-handler.h"
#include "brig-util.h"
#include "convert.h"
#include "tree-pretty-print.h"
#include "errors.h"
#include "diagnostic-core.h"

brig_queue_inst_handler::brig_queue_inst_handler (brig_to_generic &parent)
  : brig_code_entry_handler (parent)
{
}

size_t
brig_queue_inst_handler::operator () (const BrigBase *base)
{
  const BrigInstBase &inst_base = *(const BrigInstBase *) base;

  tree_stl_vec operands = build_operands (inst_base);

  if (inst_base.opcode == BRIG_OPCODE_LDQUEUEWRITEINDEX
      || inst_base.opcode == BRIG_OPCODE_LDQUEUEREADINDEX)
    {
      std::string builtin_name
	= inst_base.opcode == BRIG_OPCODE_LDQUEUEWRITEINDEX
	    ? "__phsa_builtin_ldqueuewriteindex"
	    : "__phsa_builtin_ldqueuereadindex";

      tree expr
	= call_builtin (NULL, builtin_name.c_str (), 1, uint64_type_node,
			uint64_type_node, operands[1]);
      build_output_assignment (inst_base, operands[0], expr);
    }
  else if (inst_base.opcode == BRIG_OPCODE_STQUEUEWRITEINDEX
	   || inst_base.opcode == BRIG_OPCODE_STQUEUEREADINDEX)
    {
      std::string builtin_name
	= inst_base.opcode == BRIG_OPCODE_STQUEUEWRITEINDEX
	    ? "__phsa_builtin_stqueuewriteindex"
	    : "__phsa_builtin_stqueuereadindex";

      call_builtin (NULL, builtin_name.c_str (), 2, void_type_node,
		    uint64_type_node, operands[0], uint64_type_node,
		    operands[1]);
    }
  else if (inst_base.opcode == BRIG_OPCODE_ADDQUEUEWRITEINDEX)
    {
      std::string builtin_name = "__phsa_builtin_addqueuewriteindex";

      tree expr = call_builtin (NULL, builtin_name.c_str (), 2,
				uint64_type_node, uint64_type_node, operands[1],
				uint64_type_node, operands[2]);
      build_output_assignment (inst_base, operands[0], expr);
    }
  else if (inst_base.opcode == BRIG_OPCODE_CASQUEUEWRITEINDEX)
    {
      std::string builtin_name = "__phsa_builtin_casqueuewriteindex";

      tree expr
	= call_builtin (NULL, builtin_name.c_str (), 3, uint64_type_node,
			uint64_type_node, operands[1], uint64_type_node,
			operands[2], uint64_type_node, operands[3]);
      build_output_assignment (inst_base, operands[0], expr);
    }
  else
    sorry ("unimplemented queue related instruction %u", inst_base.opcode);

  return base->byteCount;
}
