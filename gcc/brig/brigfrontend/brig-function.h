/* brig-function.h -- declaration of brig_function class.
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
#ifndef BRIG_FUNCTION_H
#define BRIG_FUNCTION_H

#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "hsa-brig-format.h"

#include <map>
#include <string>
#include <vector>
#include <set>

typedef std::map<std::string, tree> label_index;
typedef std::map<const BrigDirectiveVariable *, tree> variable_index;
typedef std::vector<tree> tree_stl_vec;

// There are 128 c regs and 2048 s/d/q regs each in the HSAIL.
#define BRIG_2_TREE_HSAIL_C_REG_COUNT (128)
#define BRIG_2_TREE_HSAIL_S_REG_COUNT (2048)
#define BRIG_2_TREE_HSAIL_D_REG_COUNT (2048)
#define BRIG_2_TREE_HSAIL_Q_REG_COUNT (2048)
#define BRIG_2_TREE_HSAIL_TOTAL_REG_COUNT                                      \
  (BRIG_2_TREE_HSAIL_C_REG_COUNT + BRIG_2_TREE_HSAIL_S_REG_COUNT               \
   + BRIG_2_TREE_HSAIL_D_REG_COUNT + BRIG_2_TREE_HSAIL_Q_REG_COUNT)

// Collects data for the currently built function.
class brig_function
{
public:
  typedef std::map<const BrigDirectiveVariable *, size_t> var_offset_table;

private:
  struct reg_decl_index_entry
  {
    tree var_decl_;
  };

public:
  brig_function (const BrigDirectiveExecutable *exec);
  ~brig_function ();

  tree arg_variable (const BrigDirectiveVariable *var) const;
  void add_arg_variable (const BrigDirectiveVariable *brigVar, tree treeDecl);

  // Appends a new kernel argument descriptor for the current kernel's
  // arg space.
  void append_kernel_arg (const BrigDirectiveVariable *var, size_t size,
			  size_t alignment);

  size_t kernel_arg_offset (const BrigDirectiveVariable *var) const;

  // Add work-item ID variables to the beginning of the kernel function
  // which can be used for address computation if the kernel dispatch packet
  // instructions are expanded to tree nodes using them.
  void add_id_variables ();

  // Returns a label with the given name in the function. If not found,
  // creates it (but doesn't append it to the statement list).
  tree label (const std::string &name);

  tree add_local_variable (std::string name, tree type);

  tree get_var_decl_for_reg (const BrigOperandRegister *reg);

  // Tries to convert the current kernel to a work-group function.
  // Returns true in case the conversion was successful.
  bool convert_to_wg_function ();

  void add_wi_loop (int dim, tree_stmt_iterator *header_entry,
		    tree_stmt_iterator *branch_after);

  // Builds the kernel launcher function and the kernel descriptor
  // for the kernel.
  tree build_launcher_and_metadata (size_t group_segment_size,
				    size_t private_segment_size);

  tree append_statement (tree stmt);

  void create_alloca_frame ();

  void finish ();

  void append_return_stmt ();

  bool has_function_scope_var (const BrigBase* var) const;

  const BrigDirectiveExecutable *m_brig_def;

  bool is_kernel;
  bool is_finished;
  std::string name;
  tree current_bind_expr;
  tree func_decl;
  tree entry_label_stmt;
  tree exit_label;

  // The __context function argument.
  tree context_arg;
  // The __group_base_ptr argument in the current function.
  // Points to the start of the group segment for the kernel
  // instance.
  tree group_base_arg;
  // The __private_base_ptr argument in the current function.
  // Points to the start of the private segment.
  tree private_base_arg;

  // The return value variable for the current function.
  tree ret_value;

  // The offsets of the kernel arguments in the __arg blob
  // pointing to the kernel argument space.
  size_t next_kernarg_offset;

  // The largest kernel argument variable alignment.
  size_t kernarg_max_align;

  var_offset_table kernarg_offsets;

  // Argument variables in the currently handled binding expression
  // (argument segment).
  variable_index arg_variables;

  // Labels in the current function are collected here so we can refer
  // to them from jumps before they have been placed to the function.
  label_index m_label_index;

  // If the kernel contains at least one barrier, this is set to true.
  bool has_barriers;

  // If the function has at least one alloca instruction, this is set to true.
  bool has_allocas;

  // If the kernel containts at least one function call that _may_
  // contain a barrier call, this is set to true.
  bool has_function_calls_with_barriers;

  // True in case the function was successfully converted to a WG function.
  bool is_wg_function;

  // Work-item ID related variables are cached in the entry of the kernel
  // function in order to use them directly in address computations, leading
  // to more efficient optimizations. The references to the local variables
  // are stored here.
  tree local_id_vars[3];
  tree cur_wg_size_vars[3];
  tree wg_id_vars[3];
  tree wg_size_vars[3];
  tree grid_size_vars[3];

  // Set to true in case the kernel contains at least one dispatch packet
  // (work-item ID-related) builtin call that could not be expanded to
  // tree nodes.
  bool has_unexpanded_dp_builtins;

  // Points to the instruction after which the real kernel code starts.
  // Usually points to the last WI ID variable initialization statement.
  tree_stmt_iterator kernel_entry;

  // True if we are currently generating the contents of an arg block.
  bool generating_arg_block;

  // A collection of function scope variables seen so far for resolving
  // variable references vs. module scope declarations.
  std::set<const BrigBase*> m_function_scope_vars;

private:
  // Bookkeeping for the different HSA registers and their tree declarations
  // for the currently generated function.
  reg_decl_index_entry *regs_[BRIG_2_TREE_HSAIL_TOTAL_REG_COUNT];
};

#endif
