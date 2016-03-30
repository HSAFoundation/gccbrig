/* brig-code-entry-handler.h -- a gccbrig base class
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

#ifndef GCC_BRIG_CODE_ENTRY_HANDLER_H
#define GCC_BRIG_CODE_ENTRY_HANDLER_H

#include "brig_to_generic.h"

#include <map>
#include <vector>

class tree_element_unary_visitor;

/// An interface to organize the different types of element handlers
/// for the code section.
class brig_code_entry_handler : public brig_entry_handler
{
private:
  typedef std::map<std::pair<BrigOpcode16_t, BrigType16_t>, tree> builtin_map;

public:
  brig_code_entry_handler (brig_to_generic &parent);
  // Handles the brig_code data at the given pointer and adds it to the
  // currently built tree. Returns the number of consumed bytes;
  virtual size_t operator () (const BrigBase *base) = 0;

  void append_statement (tree stmt);

protected:
  // Returns a GENERIC storage type for the given HSA type. Returns
  // the element type in case of vector instructions.
  tree get_tree_type_for_hsa_type (BrigType16_t brig_type) const;
  // Returns a GENERIC expression type for the given HSA type.
  // This differs from the storage type in case of f32 emulated f16s.
  tree get_tree_expr_type_for_hsa_type (BrigType16_t brig_type) const;

  tree get_tree_cst_for_hsa_operand (const BrigOperandConstantBytes *brigConst,
				     tree type) const;
  // Produce a tree code for the given BRIG opcode.
  // Return NULL_TREE in case the opcode cannot be
  // mapped to the tree directly, but should be either
  // emulated with a number of tree nodes or a builtin.
  tree_code get_tree_code_for_hsa_opcode (BrigOpcode16_t brig_opcode,
					  BrigType16_t brig_type) const;

  // Return a builtin function node that matches the given brig opcode.
  tree get_builtin_for_hsa_opcode (tree type, BrigOpcode16_t brig_opcode,
				   BrigType16_t brig_type) const;

  // Return a type for storing a comparison result given the SOURCE_TYPE
  // operand type.
  tree get_comparison_result_type (tree source_type);

  tree build_code_ref (const BrigBase &ref);

  tree build_tree_operand (const BrigInstBase &brig_inst,
			   const BrigBase &operand,
			   tree operand_type = NULL_TREE,
			   bool is_input = false);

  tree build_address_operand (const BrigInstBase &brig_inst,
			      const BrigOperandAddress &addr_operand);

  tree build_tree_operand_from_brig (const BrigInstBase *brig_inst,
				     tree operand_type, size_t operand_index);

  tree build_tree_cst_element (BrigType16_t element_type,
			       const unsigned char *next_data) const;

  bool needs_workitem_context_data (BrigOpcode16_t brig_opcode) const;

  // Unpack/pack a vector value to/from its elements.
  void unpack (tree value, tree_stl_vec &elements);
  tree pack (tree_stl_vec &elements);

  bool can_expand_builtin (BrigOpcode16_t brig_opcode) const;
  tree expand_builtin (BrigOpcode16_t brig_opcode, tree arith_type,
		       tree_stl_vec &operands);

  tree expand_or_call_builtin (BrigOpcode16_t brig_opcode,
			       BrigType16_t brig_type, tree arith_type,
			       tree_stl_vec &operands);

  tree add_temp_var (std::string name, tree expr);

  // Creates a FP32 to FP16 conversion call, assuming the source and destination
  // are FP32 type variables.
  tree build_f2h_conversion (tree source);
  // Creates a FP16 to FP32 conversion call, assuming the source and destination
  // are FP32 type variables.
  tree build_h2f_conversion (tree source);
  // Builds and "normalizes" the dest and source operands for the instruction
  // execution; converts the input operands to the expected instruction type,
  // performs half to float conversions, constant to correct type variable,
  // and flush to zero (if applicable).
  tree_stl_vec build_operands (const BrigInstBase &brig_inst);
  // Builds the final assignment to the output (register) variable along
  // with all required bitcasts and fp32 to fp16 conversions.
  tree build_output_assignment (const BrigInstBase &brig_inst, tree output,
				tree inst_expr);

  // Applies the given Visitor to all (vector) elements of the operand.
  // Scalars are considered single element vectors.
  tree apply_to_all_elements (tree_element_unary_visitor &visitor,
			      tree operand);

  tree build_builtin (const char *name, int nargs, tree rettype, ...);

  tree vbuild_builtin (const char *name, int nargs, tree rettype, va_list ap);

  HOST_WIDE_INT int_constant_value (tree node);

  tree extend_int (tree input, tree dest_type, tree src_type);

private:
  tree add_custom_builtin (BrigOpcode16_t brig_opcode, BrigType16_t itype,
			   const char *name, int nargs, tree rettype, ...);

  tree get_raw_tree_type (tree original_type);

  // HSAIL-specific builtin functions not yet integrated to gcc.
  static builtin_map s_custom_builtins;
};

class tree_element_unary_visitor
{
public:
  tree operator () (brig_code_entry_handler &handler, tree operand);

  // Performs an action to a single element, which can have originally
  // been a vector element or a scalar.
  virtual tree visit_element (brig_code_entry_handler &handler, tree operand)
    = 0;
};

class tree_element_binary_visitor
{
public:
  tree operator () (brig_code_entry_handler &handler, tree operand0,
		   tree operand1);

  // Performs an action to a pair of elements, which can have originally
  // been a vector element or a scalar.
  virtual tree visit_element (brig_code_entry_handler &handler, tree operand0,
			      tree operand1)
    = 0;
};

// Flushes real elements to zero.
class flush_to_zero : public tree_element_unary_visitor
{
public:
  flush_to_zero (bool fp16) : m_fp16 (fp16) {}

  virtual tree visit_element (brig_code_entry_handler &caller, tree operand);

private:
  // If the value should be flushed according to fp16 limits.
  bool m_fp16;
};

// Converts F16 elements to F32.
class half_to_float : public tree_element_unary_visitor
{
public:
  virtual tree visit_element (brig_code_entry_handler &caller, tree operand);
};

// Converts F32 elements to F16.
class float_to_half : public tree_element_unary_visitor
{
public:
  virtual tree visit_element (brig_code_entry_handler &caller, tree operand);
};

// A base class for instruction types that support floating point
// modifiers.
//
// operator () delegates to subclasses (template method pattern) in
// type specific parts.
class brig_inst_mod_handler : public brig_code_entry_handler
{
public:
  brig_inst_mod_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  virtual size_t generate (const BrigBase *base);
  virtual const BrigAluModifier *modifier (const BrigBase *base) const;
  virtual const BrigRound8_t *round (const BrigBase *base) const;

  size_t operator () (const BrigBase *base);
};

class brig_directive_function_handler : public brig_code_entry_handler
{
public:
  brig_directive_function_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }
  size_t operator () (const BrigBase *base);
};

class brig_directive_control_handler : public brig_code_entry_handler
{
public:
  brig_directive_control_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_directive_variable_handler : public brig_code_entry_handler
{
public:
  brig_directive_variable_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);

  tree build_variable (const BrigDirectiveVariable *brigVar,
		       tree_code m_var_decltype = VAR_DECL);
};

class brig_directive_fbarrier_handler : public brig_code_entry_handler
{
public:
  brig_directive_fbarrier_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_directive_label_handler : public brig_code_entry_handler
{
public:
  brig_directive_label_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_directive_comment_handler : public brig_code_entry_handler
{
public:
  brig_directive_comment_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_directive_arg_block_handler : public brig_code_entry_handler
{
public:
  brig_directive_arg_block_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_basic_inst_handler : public brig_code_entry_handler
{
public:
  brig_basic_inst_handler (brig_to_generic &parent);

  size_t operator () (const BrigBase *base);

private:
  // Builds a broadcast of the lowest element in the given vector operand.
  tree build_lower_element_broadcast (tree vec_operand);

  bool must_be_scalarized (const BrigInstBase *brig_inst,
			   tree instr_type) const;

  tree build_instr_expr (BrigOpcode16_t brig_opcode, BrigType16_t brig_type,
			 tree arith_type, tree_stl_vec &operands);

  tree build_shuffle (tree arith_type, tree_stl_vec &operands);
  tree build_unpack (tree_stl_vec &operands);
  tree build_pack (tree_stl_vec &operands);

  tree build_unpack_lo_or_hi (BrigOpcode16_t brig_opcode, tree arith_type,
			      tree_stl_vec &operands);

  tree get_raw_type (tree orig_type);
};

class brig_cvt_inst_handler : public brig_inst_mod_handler
{
public:
  brig_cvt_inst_handler (brig_to_generic &parent)
    : brig_inst_mod_handler (parent)
  {
  }

  virtual size_t generate (const BrigBase *base);
  virtual const BrigAluModifier *modifier (const BrigBase *base) const;
  virtual const BrigRound8_t *round (const BrigBase *base) const;
};

class brig_branch_inst_handler : public brig_code_entry_handler
{
public:
  brig_branch_inst_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_mem_inst_handler : public brig_code_entry_handler
{
public:
  brig_mem_inst_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);

private:
  tree build_mem_access (const BrigInstBase *brig_inst, tree addr, tree data);
};

class brig_copy_move_inst_handler : public brig_code_entry_handler
{
public:
  brig_copy_move_inst_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_atomic_inst_handler : public brig_code_entry_handler
{
private:
  typedef std::map<std::string, tree> atomic_builtins_map;

public:
  brig_atomic_inst_handler (brig_to_generic &parent);

  size_t operator () (const BrigBase *base);

protected:
  size_t generate_tree (const BrigInstBase &inst,
			BrigAtomicOperation8_t atomic_opcode);

private:
  void add_custom_atomic_builtin (const char *name, int nargs, tree rettype,
				  ...);
  // __sync*() builtin func declarations.
  static atomic_builtins_map s_atomic_builtins;
};

class brig_signal_inst_handler : public brig_atomic_inst_handler
{
public:
  brig_signal_inst_handler (brig_to_generic &parent)
    : brig_atomic_inst_handler (parent)
  {
  }
  size_t operator () (const BrigBase *base);
};

class brig_cmp_inst_handler : public brig_code_entry_handler
{
public:
  brig_cmp_inst_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};

class brig_seg_inst_handler : public brig_code_entry_handler
{
public:
  brig_seg_inst_handler (brig_to_generic &parent);

  size_t operator () (const BrigBase *base);
};

class brig_lane_inst_handler : public brig_code_entry_handler
{
public:
  brig_lane_inst_handler (brig_to_generic &parent);

  size_t operator () (const BrigBase *base);
};

class brig_queue_inst_handler : public brig_code_entry_handler
{
public:
  brig_queue_inst_handler (brig_to_generic &parent);

  size_t operator () (const BrigBase *base);
};

class brig_directive_module_handler : public brig_code_entry_handler
{
public:
  brig_directive_module_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t operator () (const BrigBase *base);
};


#endif
