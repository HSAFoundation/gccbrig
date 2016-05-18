/* brig-atomic-inst-handler.cc -- brig atomic instruction handling
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
#include "fold-const.h"
#include "diagnostic.h"
#include "tree-pretty-print.h"
#include "print-tree.h"
#include "convert.h"
#include "langhooks.h"
#include "gimple-expr.h"
#include "stringpool.h"

brig_atomic_inst_handler::atomic_builtins_map
  brig_atomic_inst_handler::s_atomic_builtins;

brig_atomic_inst_handler::brig_atomic_inst_handler (brig_to_generic &parent)
  : brig_code_entry_handler (parent)
{
  if (s_atomic_builtins.size () > 0)
    return;

  tree sync_4_fn = build_function_type_list (integer_type_node, ptr_type_node,
					     integer_type_node, NULL_TREE);

  tree sync_8_fn
    = build_function_type_list (long_integer_type_node, ptr_type_node,
				long_integer_type_node, NULL_TREE);

  tree sync_u4_fn = build_function_type_list (uint32_type_node, ptr_type_node,
					      uint32_type_node, NULL_TREE);

  tree sync_u8_fn = build_function_type_list (uint64_type_node, ptr_type_node,
					      uint64_type_node, NULL_TREE);

  tree sync_u4_2_fn
    = build_function_type_list (uint32_type_node, ptr_type_node,
				uint32_type_node, uint32_type_node, NULL_TREE);

  tree sync_u8_2_fn
    = build_function_type_list (uint64_type_node, ptr_type_node,
				uint64_type_node, uint64_type_node, NULL_TREE);

  tree decl = add_builtin_function ("__sync_fetch_and_sub_4", sync_4_fn,
				    BUILT_IN_SYNC_FETCH_AND_SUB_4,
				    BUILT_IN_NORMAL, NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_SUB_4, decl, true);
  s_atomic_builtins["__sync_fetch_and_sub_4"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_sub_8", sync_8_fn,
			       BUILT_IN_SYNC_FETCH_AND_SUB_8, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_SUB_8, decl, true);
  s_atomic_builtins["__sync_fetch_and_sub_8"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_add_4", sync_4_fn,
			       BUILT_IN_SYNC_FETCH_AND_ADD_4, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_ADD_4, decl, true);
  s_atomic_builtins["__sync_fetch_and_add_4"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_add_8", sync_8_fn,
			       BUILT_IN_SYNC_FETCH_AND_ADD_8, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_ADD_8, decl, true);
  s_atomic_builtins["__sync_fetch_and_add_8"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_and_4", sync_4_fn,
			       BUILT_IN_SYNC_FETCH_AND_AND_4, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_AND_4, decl, true);
  s_atomic_builtins["__sync_fetch_and_and_4"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_and_8", sync_8_fn,
			       BUILT_IN_SYNC_FETCH_AND_AND_8, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_AND_8, decl, true);
  s_atomic_builtins["__sync_fetch_and_and_8"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_xor_4", sync_u4_fn,
			       BUILT_IN_SYNC_FETCH_AND_XOR_4, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_XOR_4, decl, true);
  s_atomic_builtins["__sync_fetch_and_xor_4"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_xor_8", sync_u8_fn,
			       BUILT_IN_SYNC_FETCH_AND_XOR_8, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_XOR_8, decl, true);
  s_atomic_builtins["__sync_fetch_and_xor_8"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_or_4", sync_u4_fn,
			       BUILT_IN_SYNC_FETCH_AND_OR_4, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_OR_4, decl, true);
  s_atomic_builtins["__sync_fetch_and_or_4"] = decl;

  decl = add_builtin_function ("__sync_fetch_and_or_8", sync_u8_fn,
			       BUILT_IN_SYNC_FETCH_AND_OR_8, BUILT_IN_NORMAL,
			       NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_FETCH_AND_OR_8, decl, true);
  s_atomic_builtins["__sync_fetch_and_or_8"] = decl;

  decl = add_builtin_function ("__sync_lock_test_and_set_4", sync_u4_fn,
			       BUILT_IN_SYNC_LOCK_TEST_AND_SET_4,
			       BUILT_IN_NORMAL, NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_LOCK_TEST_AND_SET_4, decl, true);
  s_atomic_builtins["__sync_lock_test_and_set_4"] = decl;

  decl = add_builtin_function ("__sync_lock_test_and_set_8", sync_u8_fn,
			       BUILT_IN_SYNC_LOCK_TEST_AND_SET_8,
			       BUILT_IN_NORMAL, NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_LOCK_TEST_AND_SET_8, decl, true);
  s_atomic_builtins["__sync_lock_test_and_set_8"] = decl;

  decl = add_builtin_function ("__sync_val_compare_and_swap_4", sync_u4_2_fn,
			       BUILT_IN_SYNC_VAL_COMPARE_AND_SWAP_4,
			       BUILT_IN_NORMAL, NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_VAL_COMPARE_AND_SWAP_4, decl, true);
  s_atomic_builtins["__sync_val_compare_and_swap_4"] = decl;

  decl = add_builtin_function ("__sync_val_compare_and_swap_8", sync_u8_2_fn,
			       BUILT_IN_SYNC_VAL_COMPARE_AND_SWAP_8,
			       BUILT_IN_NORMAL, NULL, NULL_TREE);
  set_builtin_decl (BUILT_IN_SYNC_VAL_COMPARE_AND_SWAP_8, decl, true);
  s_atomic_builtins["__sync_val_compare_and_swap_8"] = decl;

  add_custom_atomic_builtin ("__phsa_builtin_atomic_min_s32", 2,
			     integer_type_node, ptr_type_node,
			     integer_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_min_s64", 2,
			     long_integer_type_node, ptr_type_node,
			     long_integer_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_min_u32", 2,
			     uint32_type_node, ptr_type_node, uint32_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_min_u64", 2,
			     uint64_type_node, ptr_type_node, uint64_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_max_s32", 2,
			     integer_type_node, ptr_type_node,
			     integer_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_max_u32", 2,
			     uint32_type_node, ptr_type_node, uint32_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_max_s64", 2,
			     long_integer_type_node, ptr_type_node,
			     long_integer_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_max_u64", 2,
			     uint64_type_node, ptr_type_node, uint64_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_wrapdec_u32", 2,
			     uint32_type_node, ptr_type_node, uint32_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_wrapdec_u64", 2,
			     uint64_type_node, ptr_type_node, uint64_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_wrapinc_u32", 2,
			     uint32_type_node, ptr_type_node, uint32_type_node);

  add_custom_atomic_builtin ("__phsa_builtin_atomic_wrapinc_u64", 2,
			     uint64_type_node, ptr_type_node, uint64_type_node);
}

void
brig_atomic_inst_handler::add_custom_atomic_builtin (const char *name,
						     int nargs, tree rettype,
						     ...)
{
  va_list ap;
  va_start (ap, rettype);
  tree builtin = vbuild_builtin (name, nargs, rettype, ap);
  va_end (ap);
  s_atomic_builtins[name] = builtin;
}

size_t
brig_atomic_inst_handler::generate_tree (const BrigInstBase &inst,
					 BrigAtomicOperation8_t atomic_opcode)
{
  tree_stl_vec operands = build_operands (inst);
  const int first_input
    = gccbrig_hsa_opcode_op_output_p (inst.opcode, 0) ? 1 : 0;

  tree instr_type = get_tree_type_for_hsa_type (inst.type);

  // Utilize the atomic data types (from C++11 support) for implementing
  // atomic operations.

  tree atomic_type = build_qualified_type (instr_type, TYPE_QUAL_ATOMIC);

  gcc_assert (atomic_type != NULL_TREE);

  tree signal_handle = operands[first_input];
  tree atomic_ptype = build_pointer_type (atomic_type);
  tree casted_to_ptr = convert_to_pointer (atomic_ptype, signal_handle);

  tree mem_ref = build2 (MEM_REF, atomic_type, casted_to_ptr,
			 build_int_cst (atomic_ptype, 0));

  tree src0 = NULL_TREE;
  if (atomic_opcode != BRIG_ATOMIC_LD)
    src0 = operands[first_input + 1];

  tree instr_expr = NULL_TREE;

  tree ptype = build_pointer_type (instr_type);
  tree ptr = convert_to_pointer (ptype, operands[first_input]);

  if (atomic_opcode == BRIG_ATOMIC_ST)
    instr_expr = build2 (MODIFY_EXPR, atomic_type, mem_ref, src0);
  else if (atomic_opcode == BRIG_ATOMIC_LD
	   || (atomic_opcode >= BRIG_ATOMIC_WAIT_EQ
	       && atomic_opcode <= BRIG_ATOMIC_WAITTIMEOUT_GTE))
    // signal_wait* instructions can return spuriously before the
    // condition becomes true.  Therefore it's legal to return
    // right away.  TO OPTIMIZE: builtin calls which can be
    // implemented with a power efficient sleep-wait.
    instr_expr = mem_ref;
  else if (atomic_opcode == BRIG_ATOMIC_CAS)
    {
      std::ostringstream builtin_name;
      builtin_name << "__sync_val_compare_and_swap"
		   << "_" << gccbrig_hsa_type_bit_size (inst.type) / 8;

      atomic_builtins_map::iterator i
	= s_atomic_builtins.find (builtin_name.str ());
      gcc_assert (i != s_atomic_builtins.end ());
      tree built_in = (*i).second;
      tree src1 = operands[first_input + 2];

      tree src0_type
	= TREE_VALUE (TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (built_in))));

      tree src1_type = TREE_VALUE
	(TREE_CHAIN (TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (built_in)))));

      instr_expr = call_builtin (&built_in, NULL, 3, instr_type, ptype, ptr,
				 src0_type, src0, src1_type, src1);
    }
  else
    {
      std::ostringstream builtin_name;

      switch (atomic_opcode)
	{
	case BRIG_ATOMIC_ADD:
	  builtin_name << "__sync_fetch_and_add";
	  break;
	case BRIG_ATOMIC_SUB:
	  builtin_name << "__sync_fetch_and_sub";
	  break;
	case BRIG_ATOMIC_AND:
	  builtin_name << "__sync_fetch_and_and";
	  break;
	case BRIG_ATOMIC_XOR:
	  builtin_name << "__sync_fetch_and_xor";
	  break;
	case BRIG_ATOMIC_OR:
	  builtin_name << "__sync_fetch_and_or";
	  break;
	case BRIG_ATOMIC_EXCH:
	  builtin_name << "__sync_lock_test_and_set";
	  break;
	default:
	  break;
	}

      if (builtin_name.str ().empty ())
	{
	  // Use phsail's builtin.
	  switch (atomic_opcode)
	    {
	    case BRIG_ATOMIC_MIN:
	    case BRIG_ATOMIC_MAX:
	    case BRIG_ATOMIC_WRAPINC:
	    case BRIG_ATOMIC_WRAPDEC:
	      builtin_name << "__phsa_builtin_atomic_";

	      switch (atomic_opcode)
		{
		case BRIG_ATOMIC_MIN:
		  builtin_name << "min_";
		  break;
		case BRIG_ATOMIC_MAX:
		  builtin_name << "max_";
		  break;
		case BRIG_ATOMIC_WRAPINC:
		  builtin_name << "wrapinc_";
		  break;
		case BRIG_ATOMIC_WRAPDEC:
		  builtin_name << "wrapdec_";
		  break;
		default:
		  break;
		}

	      builtin_name << gccbrig_type_name (inst.type);
	      break;
	    default:
	      internal_error ("Unuspported atomic opcode %x.", atomic_opcode);
	      break;
	    }
	}
      else
	{
	  // Use gcc's atomic builtin.  They have the byte size appended to the
	  // end of the name.
	  builtin_name << "_" << gccbrig_hsa_type_bit_size (inst.type) / 8;
	}

      atomic_builtins_map::iterator i
	= s_atomic_builtins.find (builtin_name.str ());
      if (i == s_atomic_builtins.end ())
	internal_error ("Couldn't find the builtin '%s'.",
			builtin_name.str ().c_str ());

      tree built_in = (*i).second;

      tree arg0_type
	= TREE_VALUE (TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (built_in))));

      instr_expr = call_builtin (&built_in, NULL, 2, instr_type, ptr_type_node,
				 ptr, arg0_type, src0);

      // We need a temp variable for the result, because otherwise
      // the gimplifier drops a necessary (unsigned to signed) cast in
      // the output assignment and fails a check later.
      tree tmp_var = create_tmp_var (arg0_type, "builtin_out");
      tree tmp_assign
	= build2 (MODIFY_EXPR, TREE_TYPE (tmp_var), tmp_var, instr_expr);
      m_parent.m_cf->append_statement (tmp_assign);
      instr_expr = tmp_var;
    }

  if (first_input > 0)
    build_output_assignment (inst, operands[0], instr_expr);
  else
    m_parent.m_cf->append_statement (instr_expr);

  return inst.base.byteCount;
}

size_t
brig_atomic_inst_handler::operator () (const BrigBase *base)
{
  const BrigInstAtomic *inst = (const BrigInstAtomic *) base;
  BrigAtomicOperation8_t atomic_opcode;
  atomic_opcode = inst->atomicOperation;

  return generate_tree (inst->base, atomic_opcode);
}
