/* brig-function.cc -- declaration of brig_function class.
   Copyright (C) 2015 Free Software Foundation, Inc.

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
 * @author pekka.jaaskelainen@parmance.com for General Processor Tech. 2015
 */

#include <sstream>
#include <iomanip>

#include "brig-function.h"
#include "stringpool.h"
#include "tree-iterator.h"
#include "toplev.h"
#include "cgraph.h"
#include "gimplify.h"
#include "gimple-expr.h"
#include "print-tree.h"
#include "hsa-brig-format.h"
#include "stor-layout.h"
#include "diagnostic-core.h"
#include "brig-code-entry-handler.h"
#include "brig-machine.h"
#include "brig-util.h"
#include "phsa.h"
#include "tree-pretty-print.h"
#include "dumpfile.h"
#include "tree-cfg.h"
#include "errors.h"

brig_function::brig_function ()
  : is_kernel (false), is_finished (false), name (""),
    current_bind_expr (NULL_TREE), func_decl (NULL_TREE),
    context_arg (NULL_TREE), group_base_arg (NULL_TREE),
    private_base_arg (NULL_TREE), ret_value (NULL_TREE),
    next_kernarg_offset (0), kernarg_max_align (0), has_barriers (false),
  has_function_calls_with_barriers (false), is_wg_function (false),
  has_unexpanded_dp_builtins (false), generating_arg_block (false)
{
  memset(regs_, 0, BRIG_2_TREE_HSAIL_TOTAL_REG_COUNT *
	 sizeof (BrigOperandRegister*));
}

brig_function::~brig_function ()
{
  for (size_t i = 0; i < BRIG_2_TREE_HSAIL_TOTAL_REG_COUNT; ++i)
    {
      if (regs_[i] != NULL)
        {
          delete regs_[i];
          regs_[i] = NULL;
        }
    }
}

tree
brig_function::label (const std::string& name)
{
  label_index::const_iterator i = m_label_index.find (name);
  if (i == m_label_index.end())
    {
      tree name_identifier =
	get_identifier_with_length (name.c_str(), name.size());

      tree label_decl = build_decl (UNKNOWN_LOCATION, LABEL_DECL,
				    name_identifier, void_type_node);

      DECL_CONTEXT (label_decl) = func_decl;
      DECL_ARTIFICIAL (label_decl) = 0;

      m_label_index[name] = label_decl;
      return label_decl;
    }
  else
    return (*i).second;
}

// Record an argument variable for later use. This includes both local variables
// inside arg blocks and incoming function arguments.
void
brig_function::add_arg_variable
(const BrigDirectiveVariable* brigVar, tree treeDecl)
{
  arg_variables[brigVar] = treeDecl;
}

tree
brig_function::arg_variable
(const BrigDirectiveVariable* var) const
{
  variable_index::const_iterator i = arg_variables.find (var);
  if (i == arg_variables.end()) return NULL_TREE;
  else return (*i).second;
}

void
brig_function::append_kernel_arg
(const BrigDirectiveVariable* var, size_t size, size_t alignment)
{

  gcc_assert (func_decl != NULL_TREE);
  gcc_assert (is_kernel);
  size_t align_padding = next_kernarg_offset % alignment;
  next_kernarg_offset += align_padding;
  kernarg_offsets[var] = next_kernarg_offset;
  next_kernarg_offset += size;

  kernarg_max_align =
    kernarg_max_align < alignment ?
			alignment : kernarg_max_align;
}

size_t
brig_function::kernel_arg_offset (const BrigDirectiveVariable* var) const
{
  var_offset_table::const_iterator i = kernarg_offsets.find(var);
  gcc_assert (i != kernarg_offsets.end());
  return (*i).second;
}

void
brig_function::add_id_variables ()
{
  tree bind_expr = current_bind_expr;
  tree stmts = BIND_EXPR_BODY (bind_expr);

  // Initialize the WG limits and local ids.
  static tree workitemid_builtin;
  static tree currentworkgroupsize_builtin;
  static tree workgroupsize_builtin;
  static tree workgroupid_builtin;
  static tree gridsize_builtin;

  tree_stmt_iterator entry = tsi_start (stmts);

  for (int i = 0; i < 3; ++i)
    {
      char dim_char = (char)((int)'x' + i);

      // The local sizes are limited to 16b values, but let's still use 32b
      // to avoid unnecessary casts (the ID functions are 32b).
      local_id_vars[i] =
	add_local_variable
	(std::string ("__local_") + dim_char, uint32_type_node);

      tree workitemid_call = call_builtin
	(&workitemid_builtin, "__phsa_builtin_workitemid",
	 2, uint32_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, i), ptr_type_node,
	 context_arg);

      tree id_init = build2
	(MODIFY_EXPR, TREE_TYPE (local_id_vars[i]),
	 local_id_vars[i], workitemid_call);

      tsi_link_after (&entry, id_init, TSI_NEW_STMT);

      cur_wg_size_vars[i] =
	add_local_variable
	(std::string ("__cur_wg_size_") + dim_char, uint32_type_node);

      tree cwgz_call = call_builtin
	(&currentworkgroupsize_builtin, "__phsa_builtin_currentworkgroupsize",
	 2, uint32_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, i), ptr_type_node,
	 context_arg);

      tree limit_init = build2
	(MODIFY_EXPR, TREE_TYPE (cur_wg_size_vars[i]),
	 cur_wg_size_vars[i], cwgz_call);

      tsi_link_after (&entry, limit_init, TSI_NEW_STMT);

      wg_id_vars[i] =
	add_local_variable
	(std::string ("__workgroupid_") + dim_char, uint32_type_node);

      tree wgid_call = call_builtin
	(&workgroupid_builtin, "__phsa_builtin_workgroupid",
	 2, uint32_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, i), ptr_type_node,
	 context_arg);

      tree wgid_init = build2
	(MODIFY_EXPR, TREE_TYPE (wg_id_vars[i]),
	 wg_id_vars[i], wgid_call);

      tsi_link_after (&entry, wgid_init, TSI_NEW_STMT);

      wg_size_vars[i] =
	add_local_variable
	(std::string ("__workgroupsize_") + dim_char, uint32_type_node);

      tree wgsize_call = call_builtin
	(&workgroupsize_builtin, "__phsa_builtin_workgroupsize",
	 2, uint32_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, i), ptr_type_node,
	 context_arg);

      tree wgsize_init = build2
	(MODIFY_EXPR, TREE_TYPE (wg_size_vars[i]),
	 wg_size_vars[i], wgsize_call);

      tsi_link_after (&entry, wgsize_init, TSI_NEW_STMT);

      grid_size_vars[i] =
	add_local_variable
	(std::string ("__gridsize_") + dim_char, uint32_type_node);

      tree gridsize_call = call_builtin
	(&gridsize_builtin, "__phsa_builtin_gridsize",
	 2, uint32_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, i), ptr_type_node,
	 context_arg);

      tree gridsize_init = build2
	(MODIFY_EXPR, TREE_TYPE (grid_size_vars[i]),
	 grid_size_vars[i], gridsize_call);

      tsi_link_after (&entry, gridsize_init, TSI_NEW_STMT);
    }

  kernel_entry = entry;
}

// Adds a new local variable to the current function.
tree
brig_function::add_local_variable (std::string name, tree type)
{
  tree name_identifier =
    get_identifier_with_length (name.c_str(), name.size());
  tree variable = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			      name_identifier, type);

  DECL_NONLOCAL (variable) = 0;
  TREE_ADDRESSABLE (variable) = 0;
  TREE_STATIC (variable) = 0;
  TREE_USED (variable) = 1;
  DECL_ARTIFICIAL (variable) = 0;

  tree bind_expr = DECL_SAVED_TREE (func_decl);

  DECL_CONTEXT (variable) = func_decl;

  DECL_CHAIN (variable) = BIND_EXPR_VARS (bind_expr);
  BIND_EXPR_VARS (bind_expr) = variable;
  return variable;
}

// Returns a DECL_VAR for the given HSAIL operand register.
// If it has not been created yet for the function being generated,
// creates it as an unsigned int variable.
tree
brig_function::get_var_decl_for_reg (const BrigOperandRegister* reg)
{
  size_t offset = reg->regNum;
  switch (reg->regKind)
    {
    case BRIG_REGISTER_KIND_QUAD:
      offset += BRIG_2_TREE_HSAIL_D_REG_COUNT;
    case BRIG_REGISTER_KIND_DOUBLE:
      offset += BRIG_2_TREE_HSAIL_S_REG_COUNT;
    case BRIG_REGISTER_KIND_SINGLE:
      offset += BRIG_2_TREE_HSAIL_C_REG_COUNT;
    case BRIG_REGISTER_KIND_CONTROL:
      break;
    default:
      sorry ("reg operand kind %u", reg->regKind);
      break;
    }

  reg_decl_index_entry *regEntry = regs_[offset];
  if (regEntry == NULL)
    {
      size_t reg_size = gccbrig_reg_size (reg);
      tree type;
      if (reg_size > 1)
	type = build_nonstandard_integer_type (reg_size, true);
      else
	type = boolean_type_node;

      // Drop the const qualifier so we do not end up with a read only
      // register variable which cannot be written to later.
      tree nonconst_type = build_type_variant (type, false, false);

      regEntry = new reg_decl_index_entry;

      regEntry->var_decl_ =
	add_local_variable (gccbrig_reg_name (reg), nonconst_type);
      regs_[offset] = regEntry;
    }
  return regEntry->var_decl_;
}

// Builds a work-item do..while loop for a single DIM. HEADER_ENTRY is
// a statement after which the iteration variables should be initialized and
// the loop body starts.  BRANCH_AFTER is the statement after which the loop
// predicate check and the back edge goto will be appended.
void
brig_function::add_wi_loop
(int dim, tree_stmt_iterator *header_entry, tree_stmt_iterator *branch_after)
{
  tree ivar = local_id_vars [dim];
  tree ivar_max = cur_wg_size_vars [dim];
  tree_stmt_iterator entry = *header_entry;

  // TODO: this is not a parallel loop as we share the "register
  // variables" across work-items. Should create a copy of them
  // per WI instance. That is, declare them inside the loop, not
  // at function scope.

  // TODO: Initialize the iteration variable. Assume always starting from 0.
  tree ivar_init = build2
    (MODIFY_EXPR, TREE_TYPE (ivar), ivar, build_zero_cst (TREE_TYPE (ivar)));
  tsi_link_after (&entry, ivar_init, TSI_NEW_STMT);

  tree loop_body_label = label (std::string ("__wi_loop_") +
				(char)((int)'x' + dim));
  tree loop_body_label_stmt =	build_stmt (LABEL_EXPR, loop_body_label);

  tsi_link_after (&entry, loop_body_label_stmt, TSI_NEW_STMT);

  if (has_unexpanded_dp_builtins)
    {
      static tree id_set_builtin;
      // Set the local ID to the current wi-loop iteration variable value to
      // ensure the builtins see the correct values.
      tree id_set_call = call_builtin
	(&id_set_builtin, "__phsa_builtin_setworkitemid",
	 3, void_type_node, uint32_type_node,
	 build_int_cst (uint32_type_node, dim),
	 uint32_type_node, ivar, ptr_type_node, context_arg);
      tsi_link_after (&entry, id_set_call, TSI_NEW_STMT);
    }


  // Increment the WI iteration variable.
  tree incr = build2 (PREINCREMENT_EXPR, TREE_TYPE (ivar),
		      ivar, build_one_cst (TREE_TYPE (ivar)));

  tsi_link_after (branch_after, incr, TSI_NEW_STMT);

  // Append the predicate check with the back edge goto.
  tree condition = build2 (LT_EXPR, TREE_TYPE (ivar), ivar, ivar_max);
  tree target_goto = build1 (GOTO_EXPR, void_type_node, loop_body_label);
  tree if_stmt = build3 (COND_EXPR, void_type_node, condition, target_goto,
			 NULL_TREE);
  tsi_link_after (branch_after, if_stmt, TSI_NEW_STMT);
}

bool
brig_function::convert_to_wg_function ()
{
  if (has_barriers || has_function_calls_with_barriers) return false;

  // The most trivial case: No barriers at all in the kernel.
  // We can create one big work-item loop around the whole kernel.
  tree bind_expr = current_bind_expr;
  tree stmts = BIND_EXPR_BODY (bind_expr);

  for (int i = 0; i < 3; ++i)
    {
      // The previous loop has added a new label to the end of the function,
      // the next level loop should wrap around it also.
      tree_stmt_iterator function_exit = tsi_last (stmts);
      add_wi_loop	(i, &kernel_entry, &function_exit);
    }

  is_wg_function = true;
  return false;
}

tree
brig_function::build_launcher_and_metadata
(size_t group_segment_size, size_t private_segment_size)
{

  /* The launcher function calls the device-side runtime
     that runs the kernel for all work-items. In C:

     void KernelName(void* context, void* group_base_addr) {
     __phsa_launch_kernel (_KernelName, context, group_base_addr);
     }

     or, in case of a successful conversion to a work-group function:

     void KernelName(void* context, void* group_base_addr) {
     __phsa_launch_wg_function (_KernelName, context, group_base_addr);
     }

     The user/host sees this function as the kernel to call from the
     outside. The actual kernel generated from HSAIL was named _KernelName.
  */

  // The original kernel name without the '_' prefix.
  std::string kern_name = name.substr(1);

  tree name_identifier =
    get_identifier_with_length (kern_name.c_str(), kern_name.size());

  tree launcher = build_decl
    (UNKNOWN_LOCATION, FUNCTION_DECL,
     name_identifier,
     build_function_type_list
     (void_type_node, ptr_type_node, ptr_type_node, NULL_TREE));

  TREE_USED (launcher) = 1;
  DECL_ARTIFICIAL (launcher) = 1;

  tree context_arg =
    build_decl (UNKNOWN_LOCATION, PARM_DECL,
		get_identifier ("__context"), ptr_type_node);

  DECL_ARGUMENTS (launcher) = context_arg;
  DECL_ARG_TYPE (context_arg) = ptr_type_node;
  DECL_CONTEXT (context_arg) = launcher;
  TREE_USED (context_arg) = 1;
  DECL_ARTIFICIAL (context_arg) = 1;

  tree group_base_addr_arg =
    build_decl (UNKNOWN_LOCATION, PARM_DECL,
		get_identifier ("__group_base_addr"), ptr_type_node);

  chainon (DECL_ARGUMENTS (launcher), group_base_addr_arg);
  DECL_ARG_TYPE (group_base_addr_arg) = ptr_type_node;
  DECL_CONTEXT (group_base_addr_arg) = launcher;
  TREE_USED (group_base_addr_arg) = 1;
  DECL_ARTIFICIAL (group_base_addr_arg) = 1;

  tree resdecl =
    build_decl (UNKNOWN_LOCATION, RESULT_DECL, NULL_TREE, void_type_node);

  DECL_RESULT (launcher) = resdecl;
  DECL_CONTEXT (resdecl) = launcher;

  DECL_INITIAL (launcher) = make_node (BLOCK);
  TREE_USED (DECL_INITIAL (launcher)) = 1;

  tree stmt_list = alloc_stmt_list ();

  tree bind_expr = build3 (BIND_EXPR, void_type_node, NULL, stmt_list, NULL);

  TREE_STATIC (launcher) = 0;
  TREE_PUBLIC (launcher) = 1;

  DECL_SAVED_TREE (launcher) = bind_expr;

  if (DECL_STRUCT_FUNCTION (launcher) == NULL)
    push_struct_function (launcher);
  else
    push_cfun (DECL_STRUCT_FUNCTION (launcher));

  tree kernel_func_ptr = build1 (ADDR_EXPR, ptr_type_node, func_decl);

  tree phsail_launch_kernel_call;

  if (is_wg_function)
    phsail_launch_kernel_call =
      call_builtin (NULL, "__phsa_launch_wg_function",
		    3, void_type_node,
		    ptr_type_node, kernel_func_ptr,
		    ptr_type_node, context_arg, ptr_type_node,
		    group_base_addr_arg);
  else
    phsail_launch_kernel_call =
      call_builtin (NULL, "__phsa_launch_kernel",
		    3, void_type_node,
		    ptr_type_node, kernel_func_ptr,
		    ptr_type_node, context_arg, ptr_type_node,
		    group_base_addr_arg);

  append_to_statement_list_force (phsail_launch_kernel_call, &stmt_list);

  phsa_kernel_descriptor desc;
  // TO FIX: This does not compute the total group segment size correctly,
  // in case the kernel is calling another function later in the program
  // that defines group variables.  We should first do a pass for collecting
  // all the variables used by the kernels to get the total size, and
  // preferably use a call graph to track which functions are called by which
  // kernels.
  desc.group_segment_size = group_segment_size;
  desc.private_segment_size = private_segment_size;
  desc.kernarg_segment_size = next_kernarg_offset;
  desc.kernarg_max_align = kernarg_max_align;

  /* Generate a descriptor for the kernel with HSAIL-
     specific info needed by the runtime. It's done
     via an assembly directive that generates a special
     ELF section for each kernel that contains raw bytes
     of a descriptor object. This is slightly disgusting,
     but life is never perfect ;) */

  std::ostringstream strstr;
  strstr
    << std::endl
    << ".pushsection " << PHSA_KERNELDESC_SECTION_PREFIX << kern_name
    << std::endl
    << "\t.p2align 1, 1, 1" << std::endl
    << "\t.byte ";

  for (size_t i = 0; i < sizeof (phsa_kernel_descriptor); ++i)
    {
      strstr << "0x" << std::setw(2) << std::setfill('0')
	     << std::hex << (unsigned)*((unsigned char*)&desc + i);
      if (i + 1 < sizeof (phsa_kernel_descriptor))
	strstr << ", ";
    }

  strstr << std::endl
	 << ".popsection" << std::endl << std::endl;

  tree metadata_asm =
    build_stmt
    (ASM_EXPR, build_string (strstr.str().size(), strstr.str().c_str()),
     NULL_TREE, NULL_TREE, NULL_TREE, NULL_TREE);

  append_to_statement_list_force (metadata_asm, &stmt_list);

  return launcher;
}
