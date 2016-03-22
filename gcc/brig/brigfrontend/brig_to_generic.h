/* brig_to_generic.h -- brig to gcc generic conversion
   Copyright (C) 2015-2016 Free Software Foundation, Inc.

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
#ifndef BRIG_TO_GENERIC_H
#define BRIG_TO_GENERIC_H

#include <string>
#include <map>
#include <vector>

#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "hsa-brig-format.h"
#include "brig-function.h"

/**
 * Converts an HSAIL BRIG input to GENERIC.
 *
 * This class holds global state for the translation process. Handling
 * of the smaller pieces of BRIG data is delegated to various handler
 * classes declared in brig-code-entry-handlers.h.
 *
 * @author pekka.jaaskelainen@parmance.com for General Processor Tech.
 */

struct reg_decl_index_entry;

class brig_to_generic
{
public:
  typedef std::map<const BrigDirectiveVariable *, tree> variable_index;

private:
  // Group variables can be either BrigDirectiveVariables or
  // BrigDirectiveFbarriers.
  typedef std::map<const BrigBase *, size_t> group_var_offset_table;
  typedef std::map<const BrigDirectiveVariable *, size_t> var_offset_table;

public:
  brig_to_generic (const char *brig_blob);

  // Generate all global declarations.
  void write_globals ();

  // Returns a string from the data section as a zero terminated
  // string. Owned by the callee.
  char *get_c_string (size_t entry_offset) const;
  const BrigData *get_brig_data_entry (size_t entry_offset) const;
  const BrigBase *get_brig_operand_entry (size_t entry_offset) const;
  const BrigBase *get_brig_code_entry (size_t entry_offset) const;

  void append_global (tree g);

  // Returns a function declaration with the given name. Assumes it has been
  // created previously via a DirectiveFunction or similar.
  tree function_decl (const std::string &name);
  void add_function_decl (const std::string &name, tree func_decl);

  tree global_variable (const BrigDirectiveVariable *var) const;
  void add_global_variable (const BrigDirectiveVariable *brig_var,
			    tree tree_decl);

  // Initializes a new currently handled function.
  void init_current_function (tree f);
  // Finalizes the currently handled function. Should be called before
  // setting a new function.
  void finish_current_function ();

  // Appends a new group variable (or an fbarrier) to the current kernel's
  // group segment.
  void append_group_variable (const BrigBase *var, size_t size,
			      size_t alignment);

  // Appends a new group variable to the current kernel's
  // private segment.
  void append_private_variable (const BrigDirectiveVariable *var, size_t size,
				size_t alignment);

  size_t group_variable_segment_offset (const BrigBase *var) const;

  size_t
  private_variable_segment_offset (const BrigDirectiveVariable *var) const;

  size_t private_variable_size (const BrigDirectiveVariable *var) const;

  // The size of the group and private segments required by the currently
  // processed kernel. Private segment size must be multiplied by the
  // number of work-items in the launch, in case of a work-group function.
  size_t group_segment_size () const;
  size_t private_segment_size () const;

  brig_function *get_finished_function (tree func_decl);

  static tree s_fp16_type;
  static tree s_fp32_type;
  static tree s_fp64_type;

  // The default rounding mode that should be used for float instructions.
  // This can be set in each BRIG module header.
  BrigRound8_t m_default_float_rounding_mode;

  // The currently built function.
  brig_function *m_cf;

private:
  const char *m_brig;
  const char *m_data;
  size_t m_data_size;
  const char *m_operand;
  size_t m_operand_size;
  const char *m_code;
  size_t m_code_size;

  tree m_globals;

  variable_index m_global_variables;

  // The size of each private variable, including the alignment padding.
  std::map<const BrigDirectiveVariable *, size_t> m_private_data_sizes;

  // The same for group variables.
  size_t m_next_group_offset;
  group_var_offset_table m_group_offsets;

  // And private.
  size_t m_next_private_offset;
  var_offset_table m_private_offsets;

  // Name index for declared functions.
  label_index m_function_index;

  // Stores all processed kernels in order.
  std::vector<brig_function *> m_kernels;

  // Stores all already processed functions from the translation unit
  // for some interprocedural analysis.
  std::map<tree, brig_function *> m_finished_functions;
};

/// An interface to organize the different types of BRIG element handlers.
class brig_entry_handler
{
public:
  brig_entry_handler (brig_to_generic &parent) : parent_ (parent) {}
  // Handles the brig_code data at the given pointer and adds it to the
  // currently built tree. Returns the number of consumed bytes;
  virtual size_t operator() (const BrigBase *base) = 0;

protected:
  brig_to_generic &parent_;
};

// Build a call to a builtin function.
// Stolen from gogo-tree.cc in the Go frontend.
tree call_builtin (tree *pdecl, const char *name, int nargs, tree rettype, ...);

// BRIG regs are untyped, but GENERIC is not. We need to
// add implicit casts in case treating the operand with
// an instruction with a type different than the created
// reg var type in order to select correct instruction type
// later on. This function creates the necessary reinterpret
// type cast from a source variable to the destination type.
// In case no cast is needed to the same type, SOURCE is returned
// directly.
tree build_reinterpret_cast (tree destination_type, tree source);

tree build_stmt (enum tree_code code, ...);

#endif
