/* brig-variable-handler.cc -- brig variable directive handling
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

#include "brig-code-entry-handler.h"

#include "stringpool.h"
#include "errors.h"
#include "brig-machine.h"
#include "brig-util.h"

tree
brig_directive_variable_handler::build_variable (
    const BrigDirectiveVariable *brigVar, tree_code var_decl_type)
{
  const BrigData *name_data =
    parent_.get_brig_data_entry (brigVar->name);

  // TODO: Encountering a (global) variable should mean a possible
  // currently built function has ended in the BRIG. We should call
  // finish_current_function () to handle the previously
  // created function in that case.

	std::string var_name ((const char*) (name_data->bytes + 1),
												name_data->byteCount - 1);
  // Strip & from the beginning of the name.
  tree name_identifier = get_identifier (var_name.c_str());

  tree var_decl;
  tree t;
  size_t alignment;
	size_t var_size;
  if (brigVar->type & BRIG_TYPE_ARRAY)
    {
      tree element_type = get_tree_type_for_hsa_type
				(brigVar->type & ~BRIG_TYPE_ARRAY);
      uint64_t element_count = gccbrig_to_uint64_t (brigVar->dim);
      if (element_count == 0)
				error("array variable size cannot be zero");
			if (var_decl_type == PARM_DECL)
				t = build_pointer_type (element_type);
			else
				t = build_array_type_nelts (element_type, element_count);
			size_t element_size = tree_to_uhwi (TYPE_SIZE (element_type));
      alignment = element_size / 8;
			var_size = element_size * element_count / 8;
    }
  else
    {
      t = get_tree_type_for_hsa_type (brigVar->type);
			var_size = tree_to_uhwi (TYPE_SIZE (t)) / 8;
      alignment = var_size;
    }

  if (brigVar->segment == BRIG_SEGMENT_READONLY ||
      brigVar->segment == BRIG_SEGMENT_KERNARG ||
      (brigVar->modifier.allBits & BRIG_VARIABLE_CONST))
    {
      TYPE_READONLY (t) = 1;
    }
  TYPE_ADDR_SPACE (t) = gccbrig_get_target_addr_space_id (brigVar->segment);

  // Non-default alignment.
  if (brigVar->align != BRIG_ALIGNMENT_NONE)
    {
      alignment = 1 << (brigVar->align - 1);
    }

  var_decl = build_decl (UNKNOWN_LOCATION, var_decl_type, name_identifier, t);

  DECL_ALIGN (var_decl) = alignment * 8;

  // Force the HSA alignments.
  DECL_USER_ALIGN (var_decl) = 1;

  TREE_USED (var_decl) = 1;

  TREE_PUBLIC (var_decl) = 1;
  if (brigVar->modifier.allBits & BRIG_VARIABLE_DEFINITION)
		DECL_EXTERNAL (var_decl) = 0;
  else
		DECL_EXTERNAL (var_decl) = 1; // The definition is elsewhere.

  if (brigVar->init != 0)
    {
      gcc_assert (brigVar->segment == BRIG_SEGMENT_READONLY ||
									brigVar->segment == BRIG_SEGMENT_GLOBAL);

      const BrigBase *cst_operand_data =
				parent_.get_brig_operand_entry (brigVar->init);

      tree initializer = NULL_TREE;
      if (cst_operand_data->kind == BRIG_KIND_OPERAND_CONSTANT_BYTES)
				{
					initializer = get_tree_cst_for_hsa_operand
						((const BrigOperandConstantBytes*) cst_operand_data, t);
				}
      else
				{
					error ("variable initializers of type %x not implemented",
								 cst_operand_data->kind);
				}
      gcc_assert (initializer != NULL_TREE);
      DECL_INITIAL (var_decl) = initializer;
    }

	if (var_decl_type == PARM_DECL)
		{
			DECL_ARG_TYPE (var_decl) = TREE_TYPE (var_decl);
			DECL_EXTERNAL (var_decl) = 0;
			TREE_PUBLIC (var_decl) = 0;
		}

	TREE_ADDRESSABLE (var_decl) = 1;

	TREE_USED (var_decl) = 1;
  DECL_NONLOCAL (var_decl) = 1;
  DECL_ARTIFICIAL (var_decl) = 0;

	return var_decl;
}

size_t
brig_directive_variable_handler::operator() (const BrigBase *base)
{
  const BrigDirectiveVariable *brigVar =
    (const BrigDirectiveVariable*) base;

	size_t var_size, alignment, natural_align;
	tree var_type;
	if (brigVar->type & BRIG_TYPE_ARRAY)
		{
			tree element_type = get_tree_type_for_hsa_type
				(brigVar->type & ~BRIG_TYPE_ARRAY);
			uint64_t element_count = gccbrig_to_uint64_t (brigVar->dim);
			if (element_count == 0)
				error("array variable size cannot be zero");
			var_type = build_array_type_nelts (element_type, element_count);
			size_t element_size = tree_to_uhwi (TYPE_SIZE (element_type));
			natural_align = element_size / 8;
			var_size = element_size * element_count / 8;
		}
	else
		{
			var_type = get_tree_type_for_hsa_type (brigVar->type);
			var_size = tree_to_uhwi (TYPE_SIZE (var_type)) / 8;
			natural_align = var_size;
		}

	size_t def_alignment =
		brigVar->align == BRIG_ALIGNMENT_NONE ?	0 :	1 << (brigVar->align - 1);
	alignment = def_alignment > natural_align ?	def_alignment : natural_align;

  if (brigVar->segment == BRIG_SEGMENT_KERNARG)
    {
      // Do not create a real variable, but only a table of
      // offsets to the kernarg segment buffer passed as the
      // single argument by the kernel launcher for later
      // reference.
      parent_.m_cf->append_kernel_arg (brigVar, var_size, alignment);
      return base->byteCount;
    }
	else if (brigVar->segment == BRIG_SEGMENT_GROUP)
    {
			/* Handle group region variables similarly as kernargs:
				 assign offsets to the group region on the fly when
				 a new module scope or function scope group variable is
				 introduced. These offsets will be then added to the
				 group_base hidden pointer passed to the kernel in order to
				 get the flat address. */
 			parent_.append_group_variable (base, var_size, alignment);
			return base->byteCount;
		}
	else if (brigVar->segment == BRIG_SEGMENT_PRIVATE ||
					 brigVar->segment == BRIG_SEGMENT_SPILL)
    {
			/* Private variables are handled like group variables,
				 except that their offsets are multiplied by the work-item
				 flat id, when accessed. */
			parent_.append_private_variable (brigVar, var_size, alignment);
			return base->byteCount;
		}
	else if (brigVar->segment == BRIG_SEGMENT_GLOBAL ||
					 brigVar->segment == BRIG_SEGMENT_READONLY)
		{
			tree var_decl = build_variable (brigVar);
			// Make all global variables program scope for now
			// so we can get their address from the Runtime API.
			DECL_CONTEXT (var_decl) = NULL_TREE;
			TREE_STATIC (var_decl) = 1;

			parent_.add_global_variable (brigVar, var_decl);
		}
	else if (brigVar->segment == BRIG_SEGMENT_ARG)
		{

			if (parent_.m_cf->generating_arg_block)
				{
					tree var_decl = build_variable (brigVar);
					tree bind_expr = parent_.m_cf->current_bind_expr;

					DECL_CONTEXT (var_decl) = parent_.m_cf->func_decl;
					DECL_CHAIN (var_decl) = BIND_EXPR_VARS (bind_expr);
					BIND_EXPR_VARS (bind_expr) = var_decl;
					TREE_PUBLIC (var_decl) = 0;

					parent_.m_cf->add_arg_variable (brigVar, var_decl);
				}
			else
				{
					// Must be an incoming function argument which has
					// been parsed in brig-function-handler.cc. No
					// need to generate anything here.
				}
		}
	else
		internal_error ("Unimplemented variable segment %x.",
										brigVar->segment);

  return base->byteCount;
}
