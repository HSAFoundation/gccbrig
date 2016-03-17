/* brig-code-entry-handler.cc -- a gccbrig base class
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
 * @author pekka.jaaskelainen@parmance.com for General Processor Tech.
 */

#include "brig-code-entry-handler.h"

#include "stringpool.h"
#include "tree-iterator.h"
#include "toplev.h"
#include "diagnostic.h"
#include "brig-machine.h"
#include "brig-util.h"
#include "errors.h"
#include "real.h"
#include "print-tree.h"
#include "tree-pretty-print.h"
#include "target.h"
#include "langhooks.h"
#include "gimple-expr.h"
#include "convert.h"

brig_code_entry_handler::builtin_map brig_code_entry_handler::s_custom_builtins;

brig_code_entry_handler::brig_code_entry_handler (brig_to_generic& parent)
  : brig_entry_handler (parent)
{
  if (s_custom_builtins.size() > 0) return;

  tree u32_type = get_tree_type_for_hsa_type (BRIG_TYPE_U32);
  tree s32_type = get_tree_type_for_hsa_type (BRIG_TYPE_S32);
  tree u64_type = get_tree_type_for_hsa_type (BRIG_TYPE_U64);
  tree s64_type = get_tree_type_for_hsa_type (BRIG_TYPE_S64);
  tree f32_type = get_tree_type_for_hsa_type (BRIG_TYPE_F32);
  tree f64_type = get_tree_type_for_hsa_type (BRIG_TYPE_F64);

  tree u8x4_type = get_tree_type_for_hsa_type (BRIG_TYPE_U8X4);
  tree u16x2_type = get_tree_type_for_hsa_type (BRIG_TYPE_U16X2);

  // Define the needed builtin fingerprints.
  tree f_f_fn =
    build_function_type_list (float_type_node, float_type_node, NULL_TREE);
  tree f_f_f_fn =
    build_function_type_list
    (float_type_node, float_type_node, float_type_node, NULL_TREE);
  tree d_d_fn =
    build_function_type_list (double_type_node, double_type_node, NULL_TREE);
  tree u_u_fn =
    build_function_type_list (unsigned_type_node, unsigned_type_node,
			      NULL_TREE);

  // The standard libgcc builtins used by HSAIL:
  tree decl = add_builtin_function ("__builtin_ceilf", f_f_fn,
				    BUILT_IN_CEILF,	BUILT_IN_NORMAL,
				    "ceilf", NULL_TREE);
  set_builtin_decl (BUILT_IN_CEILF, decl, true);

  decl = add_builtin_function ("__builtin_ceil", d_d_fn,
			       BUILT_IN_CEIL,	BUILT_IN_NORMAL,
			       "ceil", NULL_TREE);
  set_builtin_decl (BUILT_IN_CEIL, decl, true);

  decl = add_builtin_function ("__builtin_copysignf", f_f_f_fn,
			       BUILT_IN_COPYSIGNF,	BUILT_IN_NORMAL,
			       "copysignf", NULL_TREE);
  set_builtin_decl (BUILT_IN_COPYSIGNF, decl, true);

  decl = add_builtin_function ("__builtin_floorf", f_f_fn,
			       BUILT_IN_FLOORF,	BUILT_IN_NORMAL,
			       "floorf", NULL_TREE);
  set_builtin_decl (BUILT_IN_FLOORF, decl, true);

  decl = add_builtin_function ("__builtin_floor", d_d_fn,
			       BUILT_IN_FLOOR,	BUILT_IN_NORMAL,
			       "floor", NULL_TREE);
  set_builtin_decl (BUILT_IN_FLOOR, decl, true);

  decl = add_builtin_function ("__builtin_fmaf", f_f_fn,
			       BUILT_IN_FMAF,	BUILT_IN_NORMAL,
			       "fmaf", NULL_TREE);
  set_builtin_decl (BUILT_IN_FMAF, decl, true);

  decl = add_builtin_function ("__builtin_fma", d_d_fn,
			       BUILT_IN_FMA,	BUILT_IN_NORMAL,
			       "fma", NULL_TREE);
  set_builtin_decl (BUILT_IN_FMA, decl, true);

  decl = add_builtin_function ("__builtin_sqrt", d_d_fn,
			       BUILT_IN_SQRT,	BUILT_IN_NORMAL,
			       "sqrt", NULL_TREE);
  set_builtin_decl (BUILT_IN_SQRT, decl, true);

  decl = add_builtin_function ("__builtin_sqrtf", f_f_fn,
			       BUILT_IN_SQRTF,	BUILT_IN_NORMAL,
			       "sqrtf", NULL_TREE);
  set_builtin_decl (BUILT_IN_SQRTF, decl, true);

  decl = add_builtin_function ("__builtin_sinf", f_f_fn,
			       BUILT_IN_SINF,	BUILT_IN_NORMAL,
			       "sinf", NULL_TREE);
  set_builtin_decl (BUILT_IN_SINF, decl, true);

  decl = add_builtin_function ("__builtin_cosf", f_f_fn,
			       BUILT_IN_COSF,	BUILT_IN_NORMAL,
			       "cosf", NULL_TREE);
  set_builtin_decl (BUILT_IN_COSF, decl, true);

  decl = add_builtin_function ("__builtin_log2f", f_f_fn,
			       BUILT_IN_LOGF,	BUILT_IN_NORMAL,
			       "log2f", NULL_TREE);
  set_builtin_decl (BUILT_IN_LOG2F, decl, true);

  decl = add_builtin_function ("__builtin_exp2f", f_f_fn,
			       BUILT_IN_EXP2F,	BUILT_IN_NORMAL,
			       "exp2f", NULL_TREE);
  set_builtin_decl (BUILT_IN_EXP2F, decl, true);

  decl = add_builtin_function ("__builtin_fmaf", f_f_fn,
			       BUILT_IN_FMAF,	BUILT_IN_NORMAL,
			       "fmaf", NULL_TREE);
  set_builtin_decl (BUILT_IN_FMAF, decl, true);

  decl = add_builtin_function ("__builtin_rintf", f_f_fn,
			       BUILT_IN_RINTF,	BUILT_IN_NORMAL,
			       "rintf", NULL_TREE);
  set_builtin_decl (BUILT_IN_RINTF, decl, true);

  decl = add_builtin_function ("__builtin_rint", d_d_fn,
			       BUILT_IN_RINT,	BUILT_IN_NORMAL,
			       "rint", NULL_TREE);
  set_builtin_decl (BUILT_IN_RINT, decl, true);

  decl = add_builtin_function ("__builtin_truncf", f_f_fn,
			       BUILT_IN_TRUNCF,	BUILT_IN_NORMAL,
			       "truncf", NULL_TREE);
  set_builtin_decl (BUILT_IN_TRUNCF, decl, true);

  decl = add_builtin_function ("__builtin_trunc", d_d_fn,
			       BUILT_IN_TRUNC,	BUILT_IN_NORMAL,
			       "trunc", NULL_TREE);
  set_builtin_decl (BUILT_IN_TRUNC, decl, true);

  decl = add_builtin_function ("__builtin_popcount", u_u_fn,
			       BUILT_IN_POPCOUNT,	BUILT_IN_NORMAL,
			       "popcount", NULL_TREE);
  set_builtin_decl (BUILT_IN_POPCOUNT, decl, true);

  // phsail-specific builtins opcodes
  // TO DO: Convert these to "official" gcc builtins when upstreaming
  // gccbrig

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMABSID, BRIG_TYPE_U32,
     "__phsa_builtin_workitemabsid", 2,
     uint32_type_node, uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_GRIDSIZE, BRIG_TYPE_U32,
     "__phsa_builtin_gridsize", 2,
     uint32_type_node, uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMFLATABSID, BRIG_TYPE_U32,
     "__phsa_builtin_workitemflatabsid_u32", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMFLATABSID, BRIG_TYPE_U64,
     "__phsa_builtin_workitemflatabsid_u64", 1,
     uint64_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMFLATID, BRIG_TYPE_U32,
     "__phsa_builtin_workitemflatid", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMID, BRIG_TYPE_U32,
     "__phsa_builtin_workitemid", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKGROUPID, BRIG_TYPE_U32,
     "__phsa_builtin_workgroupid", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_CURRENTWORKITEMFLATID, BRIG_TYPE_U32,
     "__phsa_builtin_currentworkitemflatid", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKITEMABSID, BRIG_TYPE_U64,
     "__phsa_builtin_workitemabsid", 1,
     uint64_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_PACKETID, BRIG_TYPE_U64,
     "__phsa_builtin_packetid", 1,
     uint64_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_PACKETCOMPLETIONSIG, BRIG_TYPE_SIG64,
     "__phsa_builtin_packetcompletionsig_sig64", 1,
     uint64_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_PACKETCOMPLETIONSIG, BRIG_TYPE_SIG32,
     "__phsa_builtin_packetcompletionsig_sig32", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_CURRENTWORKGROUPSIZE, BRIG_TYPE_U32,
     "__phsa_builtin_currentworkgroupsize", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WORKGROUPSIZE, BRIG_TYPE_U32,
     "__phsa_builtin_workgroupsize", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_DIM, BRIG_TYPE_U32,
     "__phsa_builtin_dim", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_GRIDGROUPS, BRIG_TYPE_U32,
     "__phsa_builtin_gridgroups", 1,
     uint32_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_BITEXTRACT, BRIG_TYPE_S32, "__phsa_builtin_bitextract_s32", 3,
     s32_type, s32_type, u32_type, u32_type);

  // TO OPTIMIZE: these often map to ISA, but when not, would produce quite
  // short GENERIC trees.
  add_custom_builtin
    (BRIG_OPCODE_BITEXTRACT, BRIG_TYPE_U32, "__phsa_builtin_bitextract_u32", 3,
     u32_type, u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITEXTRACT, BRIG_TYPE_S64, "__phsa_builtin_bitextract_s64", 3,
     s64_type, s64_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITEXTRACT, BRIG_TYPE_U64, "__phsa_builtin_bitextract_u64", 3,
     u64_type, u64_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITINSERT, BRIG_TYPE_S32, "__phsa_builtin_bitinsert_u32", 4,
     u32_type, u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITINSERT, BRIG_TYPE_U32, "__phsa_builtin_bitinsert_u32", 4,
     u32_type, u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITINSERT, BRIG_TYPE_S64, "__phsa_builtin_bitinsert_u64", 4,
     u64_type, u64_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITINSERT, BRIG_TYPE_U64, "__phsa_builtin_bitinsert_u64", 4,
     u64_type, u64_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITMASK, BRIG_TYPE_B32, "__phsa_builtin_bitmask_u32", 2,
     u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITMASK, BRIG_TYPE_B64, "__phsa_builtin_bitmask_u64", 2,
     u64_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITREV, BRIG_TYPE_B32, "__phsa_builtin_bitrev_u32", 1,
     u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITREV, BRIG_TYPE_B64, "__phsa_builtin_bitrev_u64", 1,
     u64_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_BITSELECT, BRIG_TYPE_B32, "__phsa_builtin_bitselect_u32", 3,
     u32_type, u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BITSELECT, BRIG_TYPE_B64, "__phsa_builtin_bitselect_u64", 3,
     u64_type, u64_type, u64_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_FIRSTBIT, BRIG_TYPE_U32, "__phsa_builtin_firstbit_u32", 1,
     u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_FIRSTBIT, BRIG_TYPE_S32, "__phsa_builtin_firstbit_s32", 1,
     u32_type, s32_type);

  add_custom_builtin
    (BRIG_OPCODE_FIRSTBIT, BRIG_TYPE_U64, "__phsa_builtin_firstbit_u64", 1,
     u32_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_FIRSTBIT, BRIG_TYPE_S64, "__phsa_builtin_firstbit_s64", 1,
     u32_type, s64_type);

  add_custom_builtin
    (BRIG_OPCODE_LASTBIT, BRIG_TYPE_U32, "__phsa_builtin_lastbit_u32", 1,
     u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_LASTBIT, BRIG_TYPE_S32, "__phsa_builtin_lastbit_u32", 1,
     u32_type, s32_type);

  add_custom_builtin
    (BRIG_OPCODE_LASTBIT, BRIG_TYPE_U64, "__phsa_builtin_lastbit_u64", 1,
     u32_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_LASTBIT, BRIG_TYPE_S64, "__phsa_builtin_lastbit_u64", 1,
     u32_type, s64_type);

  add_custom_builtin
    (BRIG_OPCODE_BORROW, BRIG_TYPE_U32, "__phsa_builtin_borrow_u32", 2,
     u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BORROW, BRIG_TYPE_S32, "__phsa_builtin_borrow_u32", 2,
     s32_type, s32_type, s32_type);

  add_custom_builtin
    (BRIG_OPCODE_BORROW, BRIG_TYPE_U64, "__phsa_builtin_borrow_u64", 2,
     u64_type, u64_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_BORROW, BRIG_TYPE_S64, "__phsa_builtin_borrow_u64", 2,
     s64_type, s64_type, s64_type);

  add_custom_builtin
    (BRIG_OPCODE_BORROW, BRIG_TYPE_U32, "__phsa_builtin_borrow_u32", 2,
     u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_CARRY, BRIG_TYPE_S32, "__phsa_builtin_carry_u32", 2,
     s32_type, s32_type, s32_type);

  add_custom_builtin
    (BRIG_OPCODE_CARRY, BRIG_TYPE_U64, "__phsa_builtin_carry_u64", 2,
     u64_type, u64_type, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_CARRY, BRIG_TYPE_S64, "__phsa_builtin_carry_u64", 2,
     s64_type, s64_type, s64_type);

  add_custom_builtin
    (BRIG_OPCODE_CARRY, BRIG_TYPE_U32, "__phsa_builtin_carry_u32", 2,
     u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_REM, BRIG_TYPE_S32, "__phsa_builtin_rem_s32", 2,
     s32_type, s32_type, s32_type);

  add_custom_builtin
    (BRIG_OPCODE_REM, BRIG_TYPE_S64, "__phsa_builtin_rem_s64", 2,
     s64_type, s64_type, s64_type);

  add_custom_builtin
    (BRIG_OPCODE_MIN, BRIG_TYPE_F32, "__phsa_builtin_min_f32", 2,
     f32_type, f32_type, f32_type);

  add_custom_builtin
    (BRIG_OPCODE_MIN, BRIG_TYPE_F64, "__phsa_builtin_min_f64", 2,
     f64_type, f64_type, f64_type);

  add_custom_builtin
    (BRIG_OPCODE_MAX, BRIG_TYPE_F32, "__phsa_builtin_max_f32", 2,
     f32_type, f32_type, f32_type);

  add_custom_builtin
    (BRIG_OPCODE_CLASS, BRIG_TYPE_F32, "__phsa_builtin_class_f32", 2,
     u32_type, f32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_CLASS, BRIG_TYPE_F16, "__phsa_builtin_class_f32_f16", 2,
     u32_type, f32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_MAX, BRIG_TYPE_F64, "__phsa_builtin_max_f64", 2,
     f64_type, f64_type, f64_type);

  add_custom_builtin
    (BRIG_OPCODE_FRACT, BRIG_TYPE_F32, "__phsa_builtin_fract_f32", 1,
     f32_type, f32_type);

  add_custom_builtin
    (BRIG_OPCODE_FRACT, BRIG_TYPE_F64, "__phsa_builtin_fract_f64", 1,
     f64_type, f64_type);

  add_custom_builtin
    (BRIG_OPCODE_BARRIER, BRIG_TYPE_NONE, "__phsa_builtin_barrier", 1,
     void_type_node, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_INITFBAR, BRIG_TYPE_NONE, "__phsa_builtin_initfbar", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_JOINFBAR, BRIG_TYPE_NONE, "__phsa_builtin_joinfbar", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_WAITFBAR, BRIG_TYPE_NONE, "__phsa_builtin_waitfbar", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_ARRIVEFBAR, BRIG_TYPE_NONE, "__phsa_builtin_arrivefbar", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_LEAVEFBAR, BRIG_TYPE_NONE, "__phsa_builtin_leavefbar", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_RELEASEFBAR, BRIG_TYPE_NONE, "__phsa_builtin_releasefbar",
     2, void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_BITALIGN, BRIG_TYPE_B32, "__phsa_builtin_bitalign",
     3, u32_type, u64_type, u64_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_BYTEALIGN, BRIG_TYPE_B32, "__phsa_builtin_bytealign",
     3, u32_type, u64_type, u64_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_LERP, BRIG_TYPE_U8X4, "__phsa_builtin_lerp",
     3, u8x4_type, u8x4_type, u8x4_type, u8x4_type);

  add_custom_builtin
    (BRIG_OPCODE_PACKCVT, BRIG_TYPE_U8X4, "__phsa_builtin_packcvt",
     4, u8x4_type, f32_type, f32_type, f32_type, f32_type);

  add_custom_builtin
    (BRIG_OPCODE_UNPACKCVT, BRIG_TYPE_F32, "__phsa_builtin_unpackcvt",
     2, f32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_SAD, BRIG_TYPE_U16X2, "__phsa_builtin_sad_u16x2",
     3, u16x2_type, u16x2_type, u16x2_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_SAD, BRIG_TYPE_U32, "__phsa_builtin_sad_u32",
     3, u32_type, u32_type, u32_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_SAD, BRIG_TYPE_U8X4, "__phsa_builtin_sad_u8x4",
     3, u8x4_type, u8x4_type, u8x4_type, u32_type);

  add_custom_builtin
    (BRIG_OPCODE_SADHI, BRIG_TYPE_U16X2, "__phsa_builtin_sadhi_u16x2_u8x4",
     3, u16x2_type, u8x4_type, u8x4_type, u16x2_type);

  // TODO: clock has code motion constraints. Should add function attributes
  // to prevent reordering with mem instructions or other clock() calls.
  add_custom_builtin
    (BRIG_OPCODE_CLOCK, BRIG_TYPE_U64, "__phsa_builtin_clock", 0, u64_type);

  add_custom_builtin
    (BRIG_OPCODE_CUID, BRIG_TYPE_U32, "__phsa_builtin_cuid", 1,
     u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_MAXCUID, BRIG_TYPE_U32, "__phsa_builtin_maxcuid", 1,
     u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_DEBUGTRAP, BRIG_TYPE_U32, "__phsa_builtin_debugtrap", 2,
     void_type_node, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_GROUPBASEPTR, BRIG_TYPE_U32,
     "__phsa_builtin_groupbaseptr", 1, u32_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_KERNARGBASEPTR, BRIG_TYPE_U64,
     "__phsa_builtin_kernargbaseptr_u64", 1, u64_type, ptr_type_node);

  add_custom_builtin
    (BRIG_OPCODE_KERNARGBASEPTR, BRIG_TYPE_U32,
     "__phsa_builtin_kernargbaseptr_u32", 1, u32_type, ptr_type_node);

}

tree
brig_code_entry_handler::build_code_ref (const BrigBase &ref)
{
  if (ref.kind == BRIG_KIND_DIRECTIVE_LABEL)
    {
      const BrigDirectiveLabel *brig_label =
	(const BrigDirectiveLabel*)&ref;

      const BrigData *label_name =
	parent_.get_brig_data_entry (brig_label->name);

      std::string label_str
	((const char*) (label_name->bytes), label_name->byteCount);
      return parent_.m_cf->label (label_str);
    }
  else if (ref.kind == BRIG_KIND_DIRECTIVE_FUNCTION)
    {
      const BrigDirectiveExecutable *func =
	(const BrigDirectiveExecutable*)&ref;

      const BrigData *func_name =
	parent_.get_brig_data_entry (func->name);
      // Drop the leading '&' from the name.
      std::string func_name_str
	((const char*) func_name->bytes + 1, func_name->byteCount - 1);
      return parent_.function_decl (func_name_str);
    }
  else if (ref.kind == BRIG_KIND_DIRECTIVE_FBARRIER)
    {
      uint64_t offset =	parent_.group_variable_segment_offset (&ref);
      return build_int_cst (uint32_type_node, offset);
    }
  else
    internal_error ("Unimplemented code ref %x.\n", ref.kind);
}

// Produce a tree operand for the given brig OPERAND.
// OPERAND_TYPE should be the operand type in case it should not
// be dictated by the BrigBase.
tree
brig_code_entry_handler::build_tree_operand (
					     const BrigInstBase &brig_inst, const BrigBase &operand, tree operand_type,
					     bool is_input)
{
  if (operand.kind == BRIG_KIND_OPERAND_OPERAND_LIST)
    {
      vec<constructor_elt, va_gc> *constructor_vals = NULL;
      const BrigOperandOperandList &oplist =
	(const BrigOperandOperandList&) operand;
      const BrigData *data = parent_.get_brig_data_entry (oplist.elements);
      size_t bytes = data->byteCount;
      const BrigOperandOffset32_t *operand_ptr =
	(const BrigOperandOffset32_t *) data->bytes;
      while (bytes > 0)
	{
	  BrigOperandOffset32_t offset = *operand_ptr;
	  const BrigBase *operand_element =
	    parent_.get_brig_operand_entry (offset);
	  tree element =
	    build_tree_operand (brig_inst, *operand_element, operand_type);

	  // In case a vector is used an input, cast the elements to
	  // correct size here so we don't need a separate unpack/pack for it.
	  // fp16-fp32 conversion is done in build_operands().
	  if (is_input && TREE_TYPE (element) != operand_type)
	    {
	      if (int_size_in_bytes (TREE_TYPE (element)) ==
		  int_size_in_bytes (operand_type) &&
		  !INTEGRAL_TYPE_P (operand_type))
		element = build1 (VIEW_CONVERT_EXPR, operand_type, element);
	      else
		element = convert (operand_type, element);
	    }

	  CONSTRUCTOR_APPEND_ELT (constructor_vals, NULL_TREE, element);
	  ++operand_ptr;
	  bytes -= 4;
	}
      size_t element_count = data->byteCount / 4;
      tree vec_type = build_vector_type (operand_type, element_count);

      return build_constructor (vec_type, constructor_vals);
    }
  else if (operand.kind == BRIG_KIND_OPERAND_CODE_LIST)
    {
      // Build a TREE_VEC of code expressions.

      const BrigOperandCodeList &oplist =
	(const BrigOperandCodeList &) operand;
      const BrigData *data = parent_.get_brig_data_entry (oplist.elements);
      size_t bytes = data->byteCount;
      const BrigOperandOffset32_t *operand_ptr =
	(const BrigOperandOffset32_t *) data->bytes;

      size_t case_index = 0;
      size_t element_count = data->byteCount / 4;

      // Create a TREE_VEC out of the labels in the list.
      tree vec = make_tree_vec (element_count);

      while (bytes > 0)
	{
	  BrigOperandOffset32_t offset = *operand_ptr;
	  const BrigBase *ref =	parent_.get_brig_code_entry (offset);
	  tree element = build_code_ref (*ref);

	  gcc_assert (case_index < element_count);
	  TREE_VEC_ELT (vec, case_index) = element;
	  case_index++;

	  ++operand_ptr;
	  bytes -= 4;
	}
      return vec;
    }

  switch (operand.kind)
    {
    case BRIG_KIND_OPERAND_REGISTER:
      {
	const BrigOperandRegister *brig_reg =
	  (const BrigOperandRegister*) &operand;
	return parent_.m_cf->get_var_decl_for_reg (brig_reg);
      }
    case BRIG_KIND_OPERAND_CONSTANT_BYTES:
      {
	const BrigOperandConstantBytes* brigConst =
	  (const BrigOperandConstantBytes*) &operand;
	// The constants can be of different type than the instruction
	// and are implicitly casted to the input operand.
	return get_tree_cst_for_hsa_operand	(brigConst, NULL_TREE);
      }
    case BRIG_KIND_OPERAND_WAVESIZE:
      {
	if (!INTEGRAL_TYPE_P (operand_type))
	  {
	    debug_tree (operand_type);
	    error ("%s", "non-integer operand_type with WAVESIZE");
	    return NULL_TREE;
	  }
	return build_int_cstu (operand_type, gccbrig_get_target_wavesize ());
      }
    case BRIG_KIND_OPERAND_CODE_REF:
      {
	const BrigOperandCodeRef *brig_code_ref =
	  (const BrigOperandCodeRef*) &operand;

	const BrigBase* ref = parent_.get_brig_code_entry	(brig_code_ref->ref);

	return build_code_ref (*ref);
	break;
      }
    case BRIG_KIND_OPERAND_ADDRESS:
      {
	return build_address_operand
	  (brig_inst, (const BrigOperandAddress&) operand);
      }
    default:
      internal_error ("unimplemented operand type %x", operand.kind);
      break;
    }
  return NULL_TREE;
}

tree
brig_code_entry_handler::build_address_operand (
						const BrigInstBase &brig_inst, const BrigOperandAddress &addr_operand)
{
  tree instr_type = get_tree_type_for_hsa_type (brig_inst.type);

  BrigSegment8_t segment = BRIG_SEGMENT_GLOBAL;
  if (brig_inst.opcode == BRIG_OPCODE_LDA)
    segment = ((const BrigInstAddr&) brig_inst).segment;
  else if (brig_inst.base.kind == BRIG_KIND_INST_MEM)
    segment = ((const BrigInstMem&) brig_inst).segment;
  else if (brig_inst.base.kind == BRIG_KIND_INST_ATOMIC)
    segment = ((const BrigInstAtomic&) brig_inst).segment;

  tree ptr_base = NULL_TREE;
  tree ptr_offset = NULL_TREE;
  tree symbol_base = NULL_TREE;
  if (addr_operand.symbol != 0)
    {
      const BrigDirectiveVariable* arg_symbol =
	(const BrigDirectiveVariable*) parent_.get_brig_code_entry (
								    addr_operand.symbol);
      gcc_assert (arg_symbol->base.kind == BRIG_KIND_DIRECTIVE_VARIABLE);
      if (segment == BRIG_SEGMENT_KERNARG)
	{
	  // Find the offset to the kernarg buffer for the given
	  // kernel argument variable.
	  tree func = parent_.m_cf->func_decl;
	  // __args is the first parameter in kernel functions.
	  ptr_base = DECL_ARGUMENTS (func);
	  uint64_t offset =
	    parent_.m_cf->kernel_arg_offset (arg_symbol);
	  if (offset > 0)
	    ptr_offset = build_int_cst (size_type_node, offset);
	}
      else if (segment == BRIG_SEGMENT_GROUP)
	{
	  uint64_t offset =
	    parent_.group_variable_segment_offset (&arg_symbol->base);
	  ptr_offset = build_int_cst (size_type_node, offset);
	}
      else if (segment == BRIG_SEGMENT_PRIVATE ||
	       segment == BRIG_SEGMENT_SPILL)
	{
	  uint32_t offset =
	    parent_.private_variable_segment_offset (arg_symbol);

	  /* Compute the offset to the work item's copy:

	     single-wi-offset * local_size + wiflatid * varsize

	     This way the work items have the same variable in
	     successive elements to each other in the segment,
	     helping to achieve autovectorization of loads/stores
	     with stride 1. */

	  tree_stl_vec uint32_0 =
	    tree_stl_vec (1, build_int_cst (uint32_type_node, 0));

	  tree_stl_vec uint32_1 =
	    tree_stl_vec (1, build_int_cst (uint32_type_node, 1));

	  tree local_size =
	    build2 (MULT_EXPR, uint32_type_node,
		    expand_or_call_builtin
		    (BRIG_OPCODE_WORKGROUPSIZE,
		     BRIG_TYPE_U32,
		     uint32_type_node,
		     uint32_0),
		    expand_or_call_builtin
		    (BRIG_OPCODE_WORKGROUPSIZE,
		     BRIG_TYPE_U32,
		     uint32_type_node,
		     uint32_1));

	  local_size =
	    build2 (MULT_EXPR, uint32_type_node,
		    expand_or_call_builtin
		    (BRIG_OPCODE_WORKGROUPSIZE,
		     BRIG_TYPE_U32,
		     uint32_type_node,
		     uint32_1),
		    local_size);

	  tree var_region =
	    build2 (MULT_EXPR, uint32_type_node,
		    build_int_cst (uint32_type_node, offset),
		    local_size);

	  tree_stl_vec operands;
	  tree pos =
	    build2 (MULT_EXPR, uint32_type_node,
		    build_int_cst
		    (uint32_type_node,
		     parent_.private_variable_size (arg_symbol)),
		    expand_or_call_builtin
		    (BRIG_OPCODE_WORKITEMFLATID,
		     BRIG_TYPE_U32,
		     uint32_type_node,
		     operands));

	  tree var_offset =
	    build2 (PLUS_EXPR, uint32_type_node, var_region, pos);
	  symbol_base = add_temp_var ("priv_var_offset",
				      convert (uint64_type_node, var_offset));

	  //debug_generic_expr (symbol_base);
	}
      else if (segment == BRIG_SEGMENT_ARG)
	{
	  tree arg_var_decl =	parent_.m_cf->arg_variable (arg_symbol);

	  gcc_assert (arg_var_decl != NULL_TREE);

	  tree ptype = build_pointer_type (instr_type);

	  if (arg_symbol->type & BRIG_TYPE_ARRAY)
	    {
	      if (POINTER_TYPE_P (TREE_TYPE (arg_var_decl)))
		symbol_base = build_reinterpret_cast (ptype, arg_var_decl);
	      else
		{
		  // In case we are referring to an array (the argument in
		  // call site), use its element zero as the base address.
		  tree element_zero =
		    build4 (ARRAY_REF, TREE_TYPE (TREE_TYPE (arg_var_decl)),
			    arg_var_decl, integer_zero_node, NULL_TREE,
			    NULL_TREE);
		  symbol_base = build1 (ADDR_EXPR, ptype, element_zero);
		}
	    }
	  else
	    symbol_base = build1 (ADDR_EXPR, ptype, arg_var_decl);
	}
      else
	{
	  tree global_var_decl = parent_.global_variable (arg_symbol);
	  gcc_assert (global_var_decl != NULL_TREE);

	  tree ptype = build_pointer_type (instr_type);

	  symbol_base = build1 (ADDR_EXPR, ptype, global_var_decl);
	}
    }

  if (brig_inst.opcode != BRIG_OPCODE_LDA)
    {
      // In case of lda_* we want to return the segment address
      // because it's used as a value, perhaps in address
      // computation and later converted explicitly to
      // a flat address.
      // In case of other instructions with memory operands
      // we produce the flat address directly here
      // (assuming the target does not have a separate address space
      // for group/private segments for now).
      if (segment == BRIG_SEGMENT_GROUP)
	{
	  symbol_base = parent_.m_cf->group_base_arg;
	}
      else if (segment == BRIG_SEGMENT_PRIVATE ||
	       segment == BRIG_SEGMENT_SPILL)
	{
	  if (symbol_base != NULL_TREE)
	    symbol_base = build2 (PLUS_EXPR, uint64_type_node,
				  convert (uint64_type_node,
					   parent_.m_cf->private_base_arg),
				  symbol_base);
	  else
	    symbol_base = parent_.m_cf->private_base_arg;
	}
    }

  if (addr_operand.reg != 0)
    {
      const BrigOperandRegister *mem_base_reg =
	(const BrigOperandRegister*) parent_.get_brig_operand_entry	(
									 addr_operand.reg);
      // BRIG offsets are always bytes, therefore always cast the reg
      // variable to a char* for the pointer arithmetics.
      tree ptr_type = build_pointer_type (char_type_node);
      tree base_reg_var =	parent_.m_cf->get_var_decl_for_reg (mem_base_reg);

      // Cast the register variable to a pointer. If the reg
      // width is smaller than the pointer width, need to extend.

      if (int_size_in_bytes (ptr_type) >
	  int_size_in_bytes (TREE_TYPE (base_reg_var)))
	{
	  tree conv = convert_to_integer (size_type_node, base_reg_var);
	  tree tmp = create_tmp_var (ptr_type, NULL);
	  tree assign = build2
	    (MODIFY_EXPR, ptr_type, tmp,
	     build_reinterpret_cast (ptr_type, conv));
	  parent_.append_statement (assign);
	  ptr_base = tmp;
	}
      else
	ptr_base = build_reinterpret_cast (ptr_type, base_reg_var);

      gcc_assert (ptr_base != NULL_TREE);
    }
  uint64_t offs = gccbrig_to_uint64_t (addr_operand.offset);
  if (offs > 0)
    ptr_offset = build_int_cst (size_type_node, offs);
  // The pointer type we use to access the memory. Should be of the
  // width of the load/store instruction, not the target/data
  // register.
  tree ptype = build_pointer_type (instr_type);

  gcc_assert (ptype != NULL_TREE);

  tree u64t = uint64_type_node;

  // Sum up the base + offset separately to ensure byte-based ptr
  // arithmetics. In case of symbol + offset or reg + offset,
  // this is enough. Note: the base can be actually a separate
  // (base + offset) in case of accessing private or group
  // memory through offsetting the hidden base pointer argument.
  tree addr = ptr_base;
  if (addr == NULL_TREE)
    addr = symbol_base;
  if (addr != NULL_TREE && ptr_offset != NULL_TREE)
    {
      tree base_plus_offset =
	build2 (PLUS_EXPR, u64t,
		build_reinterpret_cast (u64t, addr),
		build_reinterpret_cast (u64t, ptr_offset));
      addr = build_reinterpret_cast (ptype, base_plus_offset);
    }

  if (symbol_base != NULL_TREE && ptr_base != NULL_TREE)
    {
      // The most complex addressing mode: symbol + reg [+ offset]:
      // Additional pointer arithmetics using the u64 type.
      addr =
	build2 (PLUS_EXPR, u64t,
		build_reinterpret_cast (u64t, symbol_base),
		build_reinterpret_cast (u64t, addr));
    }
  else if (addr == NULL_TREE && ptr_offset != NULL_TREE)
    {
      // At least direct module-scope global group symbol access with LDA
      // has only the ptr_offset. Group base ptr is not added as LDA should
      // return the segment address, not the flattened one.
      addr = ptr_offset;
    }
  else if (addr == NULL_TREE)
    internal_error ("Illegal address operand.");

  addr = build_reinterpret_cast (ptype, addr);

  return addr;
}

// Builds a tree operand with the given OPERAND_INDEX for the given
// BRIG_INST.
tree
brig_code_entry_handler::build_tree_operand_from_brig (
						       const BrigInstBase *brig_inst, tree operand_type, size_t operand_index)
{
  const BrigData *operand_entries =
    parent_.get_brig_data_entry (brig_inst->operands);

  uint32_t operand_offset =
    ((const uint32_t*) &operand_entries->bytes)[operand_index];
  const BrigBase *operand_data =
    parent_.get_brig_operand_entry (operand_offset);
  return build_tree_operand (*brig_inst, *operand_data, operand_type);
}

// Builds a single (scalar) constant initialized element of type
// ELEMENT_TYPE from the buffer pointed to by DATA.
tree
brig_code_entry_handler::build_tree_cst_element (
						 BrigType16_t element_type, const unsigned char *next_data) const
{

  tree tree_element_type = get_tree_type_for_hsa_type (element_type);

  tree cst;
  switch (element_type)
    {
    case BRIG_TYPE_F16:
      {
	HOST_WIDE_INT low = *(const uint16_t*)next_data;
	cst = build_int_cst (uint16_type_node, low);
	break;
      }
    case BRIG_TYPE_F32:
      {
	REAL_VALUE_TYPE val;
	ieee_single_format.decode
	  (&ieee_single_format, &val, (const long*)next_data);
	cst = build_real (tree_element_type, val);
	break;
      }
    case BRIG_TYPE_F64:
      {
	long data[2];
	data[0] = *(const uint32_t*)next_data;
	data[1] = *(const uint32_t*)(next_data + 4);
	REAL_VALUE_TYPE val;
	ieee_double_format.decode (&ieee_double_format, &val, data);
	cst = build_real (tree_element_type, val);
	break;
      }
    case BRIG_TYPE_S8:
    case BRIG_TYPE_S16:
    case BRIG_TYPE_S32:
    case BRIG_TYPE_S64:
      {
	HOST_WIDE_INT low = *(const int64_t*)next_data;
	cst = build_int_cst (tree_element_type, low);
	break;
      }
    case BRIG_TYPE_U8:
    case BRIG_TYPE_U16:
    case BRIG_TYPE_U32:
    case BRIG_TYPE_U64:
      {
	unsigned HOST_WIDE_INT low = *(const uint64_t*)next_data;
	cst = build_int_cstu (tree_element_type, low);
	break;
      }
    case BRIG_TYPE_SIG64:
      {
	unsigned HOST_WIDE_INT low = *(const uint64_t*)next_data;
	cst = build_int_cstu (uint64_type_node, low);
	break;
      }
    case BRIG_TYPE_SIG32:
      {
	unsigned HOST_WIDE_INT low = *(const uint64_t*)next_data;
	cst = build_int_cstu (uint32_type_node, low);
	break;
      }
    default:
      fprintf (stderr, "tree_element_type:\n");
      debug_tree (tree_element_type);
      internal_error ("Unimplemented const operand type %u.", element_type);
      return NULL_TREE;
    }
  return cst;
}

// Produce a tree constant type for the given BRIG constant.
// TYPE should be the instruction type in case it should not
// be dictated by the brigConst.
tree
brig_code_entry_handler::get_tree_cst_for_hsa_operand (
						       const BrigOperandConstantBytes* brigConst, tree type) const
{
  const BrigData *data = parent_.get_brig_data_entry (brigConst->bytes);

  tree cst = NULL_TREE;

  if (type == NULL_TREE)
    {
      type = get_tree_type_for_hsa_type (brigConst->type);
    }

  // The type of a single (scalar) element inside an array,
  // vector or an array of vectors.
  BrigType16_t scalar_element_type = brigConst->type & 0x01F;
  tree tree_element_type = type;

  vec<constructor_elt, va_gc> *constructor_vals = NULL;

  if (type != NULL && TREE_CODE (type) == ARRAY_TYPE)
    tree_element_type = TREE_TYPE (type);

  size_t bytes_left = data->byteCount;
  const unsigned char *next_data = data->bytes;
  size_t scalar_element_size =
    gccbrig_hsa_type_bit_size (scalar_element_type) / 8;

  while (bytes_left > 0)
    {
      if (VECTOR_TYPE_P (tree_element_type))
	{
	  // In case of vector type elements (or sole vectors),
	  // create a vector ctor.
	  size_t element_count = TYPE_VECTOR_SUBPARTS (tree_element_type);
	  if (bytes_left < scalar_element_size * element_count)
	    fatal_error
	      ("Not enough bytes left for the initializer (%lu need %lu).",
	       bytes_left, scalar_element_size * element_count);

	  vec<constructor_elt, va_gc> *vec_els = NULL;
	  for (size_t i = 0; i < element_count; ++i)
	    {
	      tree element = build_tree_cst_element (scalar_element_type, next_data);
	      CONSTRUCTOR_APPEND_ELT (vec_els, NULL_TREE, element);
	      bytes_left -= scalar_element_size;
	      next_data += scalar_element_size;
	    }
	  cst = build_vector_from_ctor (tree_element_type, vec_els);
	}
      else
	{
	  if (bytes_left < scalar_element_size)
	    fatal_error
	      ("Not enough bytes left for the initializer (%lu need %lu).",
	       bytes_left, scalar_element_size);
	  cst = build_tree_cst_element (scalar_element_type, next_data);
	  bytes_left -= scalar_element_size;
	  next_data += scalar_element_size;
	}
      CONSTRUCTOR_APPEND_ELT (constructor_vals, NULL_TREE, cst);
    }

  if (TREE_CODE (type) == ARRAY_TYPE)
    return build_constructor (type, constructor_vals);
  else
    return cst;
}

// Produce a tree type for the given BRIG type.
tree
brig_code_entry_handler::get_tree_type_for_hsa_type (
						     BrigType16_t brig_type) const
{
  tree tree_type = NULL_TREE;

  if (brig_type &
      (BRIG_TYPE_PACK_32 |
       BRIG_TYPE_PACK_64 |
       BRIG_TYPE_PACK_128))
    {
      // The element type is encoded in the bottom 5 bits.
      BrigType16_t inner_brig_type = brig_type & 0x01F;

      unsigned full_size = gccbrig_hsa_type_bit_size (brig_type);

      if (inner_brig_type == BRIG_TYPE_F16)
	return build_vector_type
	  (get_tree_type_for_hsa_type (BRIG_TYPE_U16),
	   full_size / 16);

      tree inner_type = get_tree_type_for_hsa_type (inner_brig_type);

      unsigned inner_size = gccbrig_hsa_type_bit_size (inner_brig_type);
      unsigned nunits = full_size / inner_size;
      tree_type = build_vector_type (inner_type, nunits);
    }
  else
    {
      switch (brig_type)
	{
	case BRIG_TYPE_NONE:
	  tree_type = void_type_node;
	  break;
	case BRIG_TYPE_B1:
	  tree_type = boolean_type_node;
	  break;
	case BRIG_TYPE_S8:
	case BRIG_TYPE_S16:
	case BRIG_TYPE_S32:
	case BRIG_TYPE_S64:
	  // Ensure a fixed width integer.
	  tree_type = build_nonstandard_integer_type
	    (gccbrig_hsa_type_bit_size (brig_type), false);
	  break;
	case BRIG_TYPE_U8:
	  return unsigned_char_type_node;
	case BRIG_TYPE_U16:
	case BRIG_TYPE_U32:
	case BRIG_TYPE_U64:
	case BRIG_TYPE_B8: // handle bit vectors as unsigned ints
	case BRIG_TYPE_B16:
	case BRIG_TYPE_B32:
	case BRIG_TYPE_B64:
	case BRIG_TYPE_B128:
	case BRIG_TYPE_SIG32: // handle signals as integers for now
	case BRIG_TYPE_SIG64:
	  tree_type = build_nonstandard_integer_type
	    (gccbrig_hsa_type_bit_size (brig_type), true);
	  break;
	case BRIG_TYPE_F16:
	  tree_type = uint16_type_node;
	  break;
	case BRIG_TYPE_F32:
	  tree_type = parent_.s_fp32_type;
	  break;
	case BRIG_TYPE_F64:
	  tree_type = parent_.s_fp64_type;
	  break;
	case BRIG_TYPE_SAMP:
	case BRIG_TYPE_ROIMG:
	case BRIG_TYPE_WOIMG:
	case BRIG_TYPE_RWIMG:
	  {
	    // Handle images and samplers as target-specific blobs of data
	    // that should be allocated earlier on from the runtime side.
	    // Create a void* that should be initialized to point to the blobs
	    // by the kernel launcher. Images and samplers are accessed
	    // via builtins that take void* as the reference.
	    // TODO: who and how these arrays should be initialized?
	    tree void_ptr = build_pointer_type (void_type_node);
	    return void_ptr;
	  }
	default:
	  sorry ("brig type 0x%x", brig_type);
	  break;
	}
    }

  // Drop const qualifiers.
  return build_type_variant (tree_type, false, false);
}

tree
brig_code_entry_handler::get_tree_expr_type_for_hsa_type (
							  BrigType16_t brig_type) const
{
  BrigType16_t brig_inner_type = brig_type & 0x01F;
  if (brig_inner_type == BRIG_TYPE_F16)
    {
      if (brig_inner_type == brig_type)
	return parent_.s_fp32_type;
      size_t element_count = gccbrig_hsa_type_bit_size (brig_type) / 16;
      return build_vector_type (parent_.s_fp32_type, element_count);
    }
  else
    return get_tree_type_for_hsa_type (brig_type);
}

// TO CLEANUP: move to brig-basic-inst.handler.cc
tree_code
brig_code_entry_handler::get_tree_code_for_hsa_opcode (
						       BrigOpcode16_t brig_opcode, BrigType16_t brig_type) const
{
  BrigType16_t brig_inner_type = brig_type & 0x01F;
  switch (brig_opcode)
    {
    case BRIG_OPCODE_NOP:
      return NOP_EXPR;
    case BRIG_OPCODE_ADD:
      return PLUS_EXPR;
    case BRIG_OPCODE_CMOV:
      if (brig_inner_type == brig_type)
	return COND_EXPR;
      else
	return VEC_COND_EXPR;
    case BRIG_OPCODE_SUB:
      return MINUS_EXPR;
    case BRIG_OPCODE_MUL:
    case BRIG_OPCODE_MUL24:
      return MULT_EXPR;
    case BRIG_OPCODE_MULHI:
    case BRIG_OPCODE_MUL24HI:
      return MULT_HIGHPART_EXPR;
    case BRIG_OPCODE_DIV:
      if (gccbrig_is_float_type (brig_inner_type))
	return RDIV_EXPR;
      else
	return TRUNC_DIV_EXPR;
    case BRIG_OPCODE_NEG:
      return NEGATE_EXPR;
    case BRIG_OPCODE_MIN:
      if (gccbrig_is_float_type (brig_inner_type))
	return CALL_EXPR;
      else
	return MIN_EXPR;
    case BRIG_OPCODE_MAX:
      if (gccbrig_is_float_type (brig_inner_type))
	return CALL_EXPR;
      else
	return MAX_EXPR;
    case BRIG_OPCODE_FMA:
      return FMA_EXPR;
    case BRIG_OPCODE_ABS:
      return ABS_EXPR;
    case BRIG_OPCODE_SHL:
      return LSHIFT_EXPR;
    case BRIG_OPCODE_SHR:
      return RSHIFT_EXPR;
    case BRIG_OPCODE_OR:
      return BIT_IOR_EXPR;
    case BRIG_OPCODE_XOR:
      return BIT_XOR_EXPR;
    case BRIG_OPCODE_AND:
      return BIT_AND_EXPR;
    case BRIG_OPCODE_NOT:
      return BIT_NOT_EXPR;
    case BRIG_OPCODE_RET:
      return RETURN_EXPR;
    case BRIG_OPCODE_MOV:
      return MODIFY_EXPR;
    case BRIG_OPCODE_LD:
    case BRIG_OPCODE_ST:
      return MEM_REF;
    case BRIG_OPCODE_BR:
      return GOTO_EXPR;
    case BRIG_OPCODE_REM:
      if (brig_type == BRIG_TYPE_U64 ||
	  brig_type == BRIG_TYPE_U32)
	return TRUNC_MOD_EXPR;
      else
	return CALL_EXPR;
    case BRIG_OPCODE_NRCP:
    case BRIG_OPCODE_NRSQRT:
      // Implement as 1/f(x). gcc should pattern detect that and
      // use a native instruction, if available, for it.
      return TREE_LIST;
    case BRIG_OPCODE_FLOOR:
    case BRIG_OPCODE_CEIL:
    case BRIG_OPCODE_SQRT:
    case BRIG_OPCODE_NSQRT:
    case BRIG_OPCODE_RINT:
    case BRIG_OPCODE_TRUNC:
    case BRIG_OPCODE_POPCOUNT:
    case BRIG_OPCODE_COPYSIGN:
    case BRIG_OPCODE_NCOS:
    case BRIG_OPCODE_NSIN:
    case BRIG_OPCODE_NLOG2:
    case BRIG_OPCODE_NEXP2:
    case BRIG_OPCODE_NFMA:
      // Class has type B1 regardless of the float type, thus
      // the below builtin map search cannot find it.
    case BRIG_OPCODE_CLASS:
      // Model the ID etc. special instructions as (builtin) calls.
      return CALL_EXPR;
    default:
      builtin_map::const_iterator i = s_custom_builtins.find
	(std::make_pair (brig_opcode, brig_type));
      if (i != s_custom_builtins.end ())
	return CALL_EXPR;
      else if (s_custom_builtins.find
	       (std::make_pair (brig_opcode, brig_inner_type)) !=
	       s_custom_builtins.end ())
	return CALL_EXPR;
      if (brig_inner_type == BRIG_TYPE_F16 && s_custom_builtins.find
	  (std::make_pair (brig_opcode, BRIG_TYPE_F32)) !=
	  s_custom_builtins.end ())
	return CALL_EXPR;
      break;
    }
  return TREE_LIST; // Emulate using a chain of nodes.
}

tree
brig_code_entry_handler::get_builtin_for_hsa_opcode (
						     tree type, BrigOpcode16_t brig_opcode, BrigType16_t brig_type) const
{
  tree builtin = NULL_TREE;
  tree builtin_type = type;

  // For vector types, first find the scalar version of the
  // builtin.
  if (type != NULL_TREE && VECTOR_TYPE_P (type))
    builtin_type = TREE_TYPE (type);
  BrigType16_t brig_inner_type = brig_type & 0x01F;

  switch (brig_opcode)
    {
    case BRIG_OPCODE_FLOOR:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_FLOOR);
      break;
    case BRIG_OPCODE_CEIL:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_CEIL);
      break;
    case BRIG_OPCODE_SQRT:
    case BRIG_OPCODE_NSQRT:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_SQRT);
      break;
    case BRIG_OPCODE_RINT:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_RINT);
      break;
    case BRIG_OPCODE_TRUNC:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_TRUNC);
      break;
    case BRIG_OPCODE_COPYSIGN:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_COPYSIGN);
      break;
    case BRIG_OPCODE_NSIN:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_SIN);
      break;
    case BRIG_OPCODE_NLOG2:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_LOG2);
      break;
    case BRIG_OPCODE_NEXP2:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_EXP2);
      break;
    case BRIG_OPCODE_NFMA:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_FMA);
      break;
    case BRIG_OPCODE_NCOS:
      builtin = mathfn_built_in (builtin_type, BUILT_IN_COS);
      break;
    case BRIG_OPCODE_POPCOUNT:
      builtin = builtin_decl_explicit (BUILT_IN_POPCOUNT);
      break;
    default:
      builtin_map::const_iterator i = s_custom_builtins.find
	(std::make_pair (brig_opcode, brig_type));
      if (i != s_custom_builtins.end ())
	return (*i).second;

      if (brig_inner_type != brig_type)
	{
	  // Try to find a scalar built-in we could use.
	  i = s_custom_builtins.find
	    (std::make_pair (brig_opcode, brig_inner_type));
	  if (i != s_custom_builtins.end ())
	    return (*i).second;
	}

      // In case this is an fp16 operation that is promoted to fp32,
      // try to find a fp32 scalar built-in.
      if (brig_inner_type == BRIG_TYPE_F16)
	{
	  i = s_custom_builtins.find
	    (std::make_pair (brig_opcode, BRIG_TYPE_F32));
	  if (i != s_custom_builtins.end ())
	    return (*i).second;
	}

      internal_error ("unimplemented opcode %u for type %u inner %u",
		      brig_opcode, brig_type, brig_inner_type);
    }

  if (VECTOR_TYPE_P (type) && builtin != NULL_TREE)
    {
      // Try to find a vectorized version of the built-in.
      tree vec_builtin =
	targetm.vectorize.builtin_vectorized_function
	(builtin, type, type);
      if (vec_builtin != NULL_TREE)
	return vec_builtin;
      else
	return builtin;
    }
  if (builtin == NULL_TREE)
    internal_error ("Couldn't find a built-in for opcode %u",
		    brig_opcode);
  return builtin;
}

tree
brig_code_entry_handler::get_comparison_result_type (tree source_type)
{
  if (VECTOR_TYPE_P (source_type))
    {
      tree element_type = TREE_TYPE (source_type);
      size_t element_size = int_size_in_bytes (element_type);
      size_t element_count = TYPE_VECTOR_SUBPARTS (source_type);
      bool is_int_cmp = INTEGRAL_TYPE_P (element_type);
      if (is_int_cmp)
	return signed_type_for (source_type);
      else if (element_size == 4)
	return build_vector_type (get_tree_type_for_hsa_type
				  (BRIG_TYPE_S32), element_count);
      else if (element_size == 8)
	return build_vector_type (get_tree_type_for_hsa_type
				  (BRIG_TYPE_S64), element_count);
      else error ("Unsupported float element size.");
    }
  return get_tree_type_for_hsa_type (BRIG_TYPE_B1);
}

// Returns true in case the given opcode needs to know about work-item context.
// In such case the context data is passed as a pointer to a work-item context
// object, as the last argument in the builtin call.
bool
brig_code_entry_handler::needs_workitem_context_data (
						      BrigOpcode16_t brig_opcode) const
{
  switch (brig_opcode) {
  case BRIG_OPCODE_WORKITEMABSID:
  case BRIG_OPCODE_WORKITEMFLATABSID:
  case BRIG_OPCODE_WORKITEMFLATID:
  case BRIG_OPCODE_CURRENTWORKITEMFLATID:
  case BRIG_OPCODE_WORKITEMID:
  case BRIG_OPCODE_WORKGROUPID:
  case BRIG_OPCODE_WORKGROUPSIZE:
  case BRIG_OPCODE_CURRENTWORKGROUPSIZE:
  case BRIG_OPCODE_GRIDGROUPS:
  case BRIG_OPCODE_GRIDSIZE:
  case BRIG_OPCODE_DIM:
  case BRIG_OPCODE_PACKETID:
  case BRIG_OPCODE_PACKETCOMPLETIONSIG:
  case BRIG_OPCODE_BARRIER:
  case BRIG_OPCODE_WAVEBARRIER:
  case BRIG_OPCODE_ARRIVEFBAR:
  case BRIG_OPCODE_INITFBAR:
  case BRIG_OPCODE_JOINFBAR:
  case BRIG_OPCODE_LEAVEFBAR:
  case BRIG_OPCODE_RELEASEFBAR:
  case BRIG_OPCODE_WAITFBAR:
  case BRIG_OPCODE_CUID:
  case BRIG_OPCODE_MAXCUID:
  case BRIG_OPCODE_DEBUGTRAP:
  case BRIG_OPCODE_GROUPBASEPTR:
  case BRIG_OPCODE_KERNARGBASEPTR:
    return true;
  default:
    return false;
  };
}


// Returns true in case the given opcode that would normally be generated
// as a builtin call can be expanded to tree nodes.
bool
brig_code_entry_handler::can_expand_builtin (BrigOpcode16_t brig_opcode) const
{
  switch (brig_opcode) {
  case BRIG_OPCODE_WORKITEMFLATABSID:
  case BRIG_OPCODE_WORKITEMFLATID:
  case BRIG_OPCODE_WORKITEMABSID:
    /* TO OPTIMIZE: expand more builtins. At least the (cur)wgsize is already
       available in cf. */
    return true;
  default:
    return false;
  };
}

tree
brig_code_entry_handler::expand_or_call_builtin (
						 BrigOpcode16_t brig_opcode, BrigType16_t brig_type,	tree arith_type,
						 tree_stl_vec& operands)
{
  if (parent_.m_cf->is_kernel && can_expand_builtin (brig_opcode))
    return expand_builtin (brig_opcode, arith_type, operands);
  else
    {
      tree built_in =
	get_builtin_for_hsa_opcode
	(arith_type, brig_opcode, brig_type);

      if (!VECTOR_TYPE_P (TREE_TYPE (TREE_TYPE (built_in))) &&
	  arith_type != NULL_TREE &&
	  VECTOR_TYPE_P (arith_type))
	{
	  // Call the scalar built-in for all elements in the vector.
	  tree_stl_vec operand0_elements;
	  if (operands.size() > 0) unpack (operands [0], operand0_elements);

	  tree_stl_vec operand1_elements;
	  if (operands.size() > 1) unpack (operands [1], operand1_elements);

	  tree_stl_vec result_elements;

	  for (size_t i = 0; i < TYPE_VECTOR_SUBPARTS (arith_type); ++i)
	    {
	      tree_stl_vec call_operands;
	      if (operand0_elements.size () > 0)
		call_operands.push_back (operand0_elements.at(i));

	      if (operand1_elements.size () > 0)
		call_operands.push_back (operand1_elements.at(i));

	      result_elements.push_back
		(expand_or_call_builtin (brig_opcode, brig_type,
					 TREE_TYPE (arith_type), call_operands));
	    }
	  return pack (result_elements);
	}

      tree_stl_vec call_operands;
      tree_stl_vec operand_types;

      for (size_t i = 0; i < operands.size (); ++i)
	{
	  call_operands.push_back (operands [i]);
	  operand_types.push_back (TREE_TYPE (operands [i]));
	}

      if (needs_workitem_context_data (brig_opcode))
	{
	  call_operands.push_back (parent_.m_cf->context_arg);
	  operand_types.push_back (ptr_type_node);
	  parent_.m_cf->has_unexpanded_dp_builtins = true;
	}

      size_t operand_count = call_operands.size ();

      call_operands.resize (4, NULL_TREE);
      operand_types.resize (4, NULL_TREE);
      return call_builtin	(&built_in, NULL, operand_count,
				 TREE_TYPE (TREE_TYPE (built_in)),
				 operand_types [0], call_operands [0],
				 operand_types [1], call_operands [1],
				 operand_types [2], call_operands [2],
				 operand_types [3], call_operands [3]);
    }
}

tree
brig_code_entry_handler::expand_builtin (
					 BrigOpcode16_t brig_opcode, tree /*arith_type*/, tree_stl_vec& operands)
{
  tree_stl_vec uint32_0 =
    tree_stl_vec (1, build_int_cst (uint32_type_node, 0));

  tree_stl_vec uint32_1 =
    tree_stl_vec (1, build_int_cst (uint32_type_node, 1));

  tree_stl_vec uint32_2 =
    tree_stl_vec (1, build_int_cst (uint32_type_node, 2));

  if (brig_opcode == BRIG_OPCODE_WORKITEMFLATABSID)
    {
      tree id0 = expand_builtin (BRIG_OPCODE_WORKITEMABSID, uint32_type_node,
				 uint32_0);
      id0 = convert (uint64_type_node, id0);

      tree id1 = expand_builtin (BRIG_OPCODE_WORKITEMABSID, uint32_type_node,
				 uint32_1);
      id1 = convert (uint64_type_node, id1);

      tree id2 = expand_builtin (BRIG_OPCODE_WORKITEMABSID, uint32_type_node,
				 uint32_2);
      id2 = convert (uint64_type_node, id2);

      tree max0 = convert (uint64_type_node, parent_.m_cf->grid_size_vars [0]);
      tree max1 = convert (uint64_type_node, parent_.m_cf->grid_size_vars [1]);

      tree id2_x_max0_x_max1 = build2 (MULT_EXPR, uint64_type_node, id2, max0);
      id2_x_max0_x_max1 =
	build2 (MULT_EXPR, uint64_type_node, id2_x_max0_x_max1, max1);

      tree id1_x_max0 = build2 (MULT_EXPR, uint64_type_node, id1, max0);

      tree sum = build2 (PLUS_EXPR, uint64_type_node, id0, id1_x_max0);
      sum = build2 (PLUS_EXPR, uint64_type_node, sum, id2_x_max0_x_max1);

      return add_temp_var ("workitemflatabsid", sum);
    }
  else if (brig_opcode == BRIG_OPCODE_WORKITEMABSID)
    {
      HOST_WIDE_INT dim = int_constant_value (operands [0]);

      tree local_id_var = parent_.m_cf->local_id_vars [dim];
      tree wg_id_var = parent_.m_cf->wg_id_vars [dim];
      tree wg_size_var = parent_.m_cf->wg_size_vars [dim];
      tree grid_size_var = parent_.m_cf->grid_size_vars [dim];

      tree wg_id_x_wg_size = build2 (MULT_EXPR, uint32_type_node,
				     convert (uint32_type_node, wg_id_var),
				     convert (uint32_type_node, wg_size_var));
      tree sum = build2 (PLUS_EXPR, uint32_type_node, wg_id_x_wg_size, local_id_var);

      // We need a modulo here because of work-groups which have dimensions
      // larger than the grid size :( TO CHECK: is this really allowed in the
      // specs?
      tree modulo = build2 (TRUNC_MOD_EXPR, uint32_type_node, sum, grid_size_var);

      return add_temp_var (std::string ("workitemabsid_") +
			   (char)((int)'x' + dim), modulo);
    }
  else if (brig_opcode == BRIG_OPCODE_WORKITEMFLATID)
    {
      tree z_x_wgsx_wgsy = build2 (MULT_EXPR, uint32_type_node,
				   parent_.m_cf->local_id_vars [2],
				   parent_.m_cf->wg_size_vars [0]);
      z_x_wgsx_wgsy = build2 (MULT_EXPR, uint32_type_node, z_x_wgsx_wgsy,
			      parent_.m_cf->wg_size_vars [1]);

      tree y_x_wgsx = build2 (MULT_EXPR, uint32_type_node,
			      parent_.m_cf->local_id_vars [1],
			      parent_.m_cf->wg_size_vars [0]);

      tree sum = build2 (PLUS_EXPR, uint32_type_node, y_x_wgsx, z_x_wgsx_wgsy);
      sum = build2 (PLUS_EXPR, uint32_type_node, parent_.m_cf->local_id_vars [0],
		    sum);
      return add_temp_var ("workitemflatid", sum);
    }
  else
    gcc_unreachable ();

  return NULL_TREE;
}

// Appends and returns a new temp variable and an accompanying assignment
// statement that stores the value of the given EXPR and has the given NAME.
tree
brig_code_entry_handler::add_temp_var (std::string name, tree expr)
{
  tree temp_var = create_tmp_var (TREE_TYPE (expr), name.c_str());
  tree assign = build2
    (MODIFY_EXPR, TREE_TYPE (temp_var), temp_var, expr);
  parent_.append_statement (assign);
  return temp_var;
}

tree
brig_code_entry_handler::vbuild_builtin (
					 const char* name, int nargs, tree rettype, va_list ap)
{
  tree fnid = get_identifier (name);

  tree* types = new tree [nargs];

  for (int i = 0; i < nargs; ++i)
    {
      types[i] = va_arg (ap, tree);
      if (types[i] == error_mark_node)
	{
	  delete[] types;
	  return error_mark_node;
	}
    }

  tree argtypes = NULL_TREE;
  tree* pp = &argtypes;
  for (int i = 0; i < nargs; ++i)
    {
      *pp = tree_cons (NULL_TREE, types [i], NULL_TREE);
      pp = &TREE_CHAIN (*pp);
    }
  *pp = void_list_node;

  tree fntype = build_function_type (rettype, argtypes);

  tree builtin = build_decl (UNKNOWN_LOCATION, FUNCTION_DECL, fnid, fntype);

  TREE_STATIC (builtin) = 0;
  DECL_EXTERNAL (builtin) = 1;
  TREE_PUBLIC (builtin) = 1;

  delete[] types;

  return builtin;
}

tree
brig_code_entry_handler::build_builtin
(const char* name, int nargs, tree rettype, ...)
{
  va_list ap;
  va_start (ap, rettype);
  tree builtin = vbuild_builtin (name, nargs, rettype, ap);
  va_end(ap);
  return builtin;
}

tree
brig_code_entry_handler::add_custom_builtin
(BrigOpcode16_t brig_opcode, BrigType16_t itype,
 const char* name, int nargs, tree rettype, ...)
{
  va_list ap;
  va_start (ap, rettype);
  tree builtin = vbuild_builtin (name, nargs, rettype, ap);
  va_end(ap);

  s_custom_builtins [std::make_pair (brig_opcode, itype)] = builtin;
  return builtin;
}

tree
brig_code_entry_handler::build_f2h_conversion (tree source)
{
  return float_to_half () (*this, source);
}

tree
brig_code_entry_handler::build_h2f_conversion (tree source)
{
  return half_to_float () (*this, source);
}

// Returns a "raw type" (one with unsigned int elements)
// corresponding to the size and element count of ORIGINAL_TYPE.
tree
brig_code_entry_handler::get_raw_tree_type (tree original_type)
{
  if (VECTOR_TYPE_P (original_type))
    {
      size_t esize = int_size_in_bytes (TREE_TYPE (original_type))*8;
      size_t ecount = TYPE_VECTOR_SUBPARTS (original_type);
      return
	build_vector_type
	(build_nonstandard_integer_type (esize, true),
	 ecount);
    }
  else
    return build_nonstandard_integer_type
      (int_size_in_bytes (original_type)*8, true);
}

std::vector<tree>
brig_code_entry_handler::build_operands (const BrigInstBase &brig_inst)
{
  bool ftz = false; // flush to zero
  const BrigBase *base = &brig_inst.base;

  if (base->kind == BRIG_KIND_INST_MOD)
    {
      const BrigInstMod *mod = (const BrigInstMod*) base;
      ftz = mod->modifier.allBits & BRIG_ALU_FTZ;
    }
  else if (base->kind == BRIG_KIND_INST_CMP)
    {
      const BrigInstCmp *cmp = (const BrigInstCmp*) base;
      ftz = cmp->modifier.allBits & BRIG_ALU_FTZ;
    }

  bool is_vec_instr =
    brig_inst.type &
    (BRIG_TYPE_PACK_32 | BRIG_TYPE_PACK_64 | BRIG_TYPE_PACK_128);

  size_t element_count;
  if (is_vec_instr)
    {
      BrigType16_t brig_element_type = brig_inst.type & 0x01F;
      element_count =
	gccbrig_hsa_type_bit_size (brig_inst.type) /
	gccbrig_hsa_type_bit_size (brig_element_type);
    }
  else
    element_count = 1;

  bool is_fp16_arith = false;

  tree src_type;
  tree dest_type;
  if (base->kind == BRIG_KIND_INST_CMP)
    {
      const BrigInstCmp *cmp_inst = (const BrigInstCmp*) base;
      src_type = get_tree_type_for_hsa_type (cmp_inst->sourceType);
      dest_type = get_tree_type_for_hsa_type (brig_inst.type);
      is_fp16_arith = (cmp_inst->sourceType & 0x01F) == BRIG_TYPE_F16;
    }
  else if (base->kind == BRIG_KIND_INST_SOURCE_TYPE)
    {
      const BrigInstSourceType *src_type_inst =
	(const BrigInstSourceType*) base;
      src_type = get_tree_type_for_hsa_type (src_type_inst->sourceType);
      dest_type = get_tree_type_for_hsa_type (brig_inst.type);
      is_fp16_arith =
	(src_type_inst->sourceType & 0x01F) == BRIG_TYPE_F16 &&
	!gccbrig_is_raw_operation(brig_inst.opcode);
    }
  else if (base->kind == BRIG_KIND_INST_SEG_CVT)
    {
      const BrigInstSegCvt *seg_cvt_inst = (const BrigInstSegCvt*) base;
      src_type = get_tree_type_for_hsa_type (seg_cvt_inst->sourceType);
      dest_type = get_tree_type_for_hsa_type (brig_inst.type);
    }
  else if (base->kind == BRIG_KIND_INST_MEM)
    {
      src_type = get_tree_type_for_hsa_type (brig_inst.type);
      dest_type = src_type;
      // With mem instructions we don't want to cast the fp16
      // back and forth between fp32, because the load/stores
      // are not specific to the data type.
      is_fp16_arith = false;
    }
  else if (base->kind == BRIG_KIND_INST_CVT)
    {
      const BrigInstCvt *cvt_inst = (const BrigInstCvt*) base;

      src_type = get_tree_type_for_hsa_type (cvt_inst->sourceType);
      dest_type = get_tree_type_for_hsa_type (brig_inst.type);
    }
  else
    {
      switch (brig_inst.opcode)
	{
	case BRIG_OPCODE_INITFBAR:
	case BRIG_OPCODE_JOINFBAR:
	case BRIG_OPCODE_WAITFBAR:
	case BRIG_OPCODE_ARRIVEFBAR:
	case BRIG_OPCODE_LEAVEFBAR:
	case BRIG_OPCODE_RELEASEFBAR:
	  src_type = uint32_type_node;
	  break;
	default:
	  src_type = get_tree_type_for_hsa_type (brig_inst.type);
	  break;
	}
      dest_type = src_type;
      is_fp16_arith =
	!gccbrig_is_raw_operation(brig_inst.opcode) &&
	(brig_inst.type & 0x01F) == BRIG_TYPE_F16;
    }

  // Halfs are a tricky special case: their "storage format"
  // is u16, but scalars are stored in 32b regs while packed
  // f16 are... well packed.
  tree half_storage_type =
    element_count > 1 ?
    get_tree_type_for_hsa_type (brig_inst.type) :	uint32_type_node;

  const BrigData *operand_entries =
    parent_.get_brig_data_entry (brig_inst.operands);
  std::vector<tree> operands;
  for (size_t i = 0; i < operand_entries->byteCount / 4; ++i)
    {
      uint32_t operand_offset = ((const uint32_t*) &operand_entries->bytes)[i];
      const BrigBase *operand_data =
	parent_.get_brig_operand_entry (operand_offset);

      const bool is_output =
	gccbrig_hsa_opcode_op_output_p (brig_inst.opcode, i);

      tree operand_type = is_output ? dest_type : src_type;

      bool half_to_float = is_fp16_arith;

      // Special cases for operand types.
      if ((brig_inst.opcode == BRIG_OPCODE_SHL ||
	   brig_inst.opcode == BRIG_OPCODE_SHR) && i == 2)
	{
	  // The shift amount is always a scalar.
	  operand_type = VECTOR_TYPE_P (src_type) ?
	    TREE_TYPE (src_type) : src_type;
	}
      else if (brig_inst.opcode == BRIG_OPCODE_CMOV && i == 1)
	operand_type = get_comparison_result_type (operand_type);
      else if (brig_inst.opcode == BRIG_OPCODE_SHUFFLE)
	{
	  if (i == 3)
	    {
	      // HSAIL shuffle inputs the MASK vector as tightly packed bits
	      // while GENERIC VEC_PERM_EXPR expects the mask elements to be
	      // of the same size as the elements in the input vectors. Let's
	      // cast to a scalar type here and convert to the VEC_PERM_EXPR
	      // format in instruction handling. There are no arbitrary bit width
	      // int types in GENERIC so we cannot use the original vector
	      // type.
	      operand_type = uint32_type_node;
	    }
	  else
	    {
	      // Always treat the element as unsigned ints to avoid
	      // sign extensions/negative offsets with masks, which
	      // are expected to be of the same element type as the
	      // data in VEC_PERM_EXPR. With shuffles the data type
	      // should not matter as it's a "raw operation".
	      operand_type = get_raw_tree_type (operand_type);
	    }
	}
      else if (brig_inst.opcode == BRIG_OPCODE_PACK)
	{
	  if (i == 1)
	    operand_type = get_raw_tree_type (dest_type);
	  else if (i == 2)
	    operand_type = get_raw_tree_type (TREE_TYPE (dest_type));
	  else if (i == 3)
	    operand_type = uint32_type_node;
	}
      else if (brig_inst.opcode == BRIG_OPCODE_UNPACK && i == 2)
	{
	  operand_type = uint32_type_node;
	}
      else if (brig_inst.opcode == BRIG_OPCODE_SAD && i == 3)
	{
	  operand_type = uint32_type_node;
	}
      else if (brig_inst.opcode == BRIG_OPCODE_CLASS && i == 2)
	{
	  operand_type = uint32_type_node;
	  half_to_float = false;
	}
      else if (half_to_float)
	{
	  // Treat the operands as the storage type at this point.
	  operand_type = half_storage_type;
	}

      tree operand = build_tree_operand
	(brig_inst, *operand_data, operand_type, !is_output);
      if (operand == NULL_TREE)
	internal_error ("Unimplemented operand type (opcode %x).",
			brig_inst.opcode);

      // Cast/convert the inputs to correct types as expected by the GENERIC
      // opcode instruction.
      if (!is_output)
	{
	  if (half_to_float)
	    {
	      operand = build_h2f_conversion
		(build_reinterpret_cast (half_storage_type, operand));
	    }
	  else if (!(TREE_CODE (operand) == LABEL_DECL) &&
		   !(TREE_CODE (operand) == TREE_VEC) &&
		   operand_data->kind != BRIG_KIND_OPERAND_ADDRESS &&
		   !VECTOR_TYPE_P (TREE_TYPE (operand)))
	    {
	      size_t reg_width = int_size_in_bytes (TREE_TYPE (operand));
	      size_t instr_width = int_size_in_bytes (operand_type);
	      if (reg_width == instr_width)
		operand = build_reinterpret_cast (operand_type, operand);
	      else if (reg_width > instr_width)
		{
		  // Clip the operand because the instruction's bitwidth
		  // is smaller than the HSAIL reg width.
		  if (INTEGRAL_TYPE_P (operand_type))
		    operand = convert_to_integer
		      (signed_or_unsigned_type_for
		       (TYPE_UNSIGNED (operand_type), operand_type),
		       operand);
		  else
		    operand = build1 (VIEW_CONVERT_EXPR, operand_type, operand);
		}
	      else if (reg_width < instr_width)
		{
		  // At least shift amount operands can be read from smaller
		  // registers than the data operands.
		  operand = convert (operand_type, operand);
		}
	      else
		// Always add a view_convert_expr to ensure correct type for
		// constant operands. For some reason leads to illegal optimizations
		// otherwise.
		operand = build1 (VIEW_CONVERT_EXPR, operand_type, operand);
	    }
	  else if (brig_inst.opcode == BRIG_OPCODE_SHUFFLE)
	    {
	      // Force the operand type to be treated as the raw type.
	      operand = build_reinterpret_cast (operand_type, operand);
	    }

	  if (brig_inst.opcode == BRIG_OPCODE_CMOV &&
	      i == 1)
	    {
	      // gcc expects the lower bit to be 1 (or all ones in case of vectors)
	      // while CMOV assumes false iff 0. Convert the input here
	      // to what gcc likes by generating 'operand = operand != 0'.
	      tree cmp_res_type =	get_comparison_result_type (operand_type);
	      operand =
		build2 (NE_EXPR, cmp_res_type, operand,
			build_zero_cst (cmp_res_type));
	    }

	  if (ftz)
	    operand = flush_to_zero (is_fp16_arith) (*this, operand);
	}

      operands.push_back (operand);
    }
  return operands;
}

tree
brig_code_entry_handler::build_output_assignment (
						  const BrigInstBase &brig_inst, tree output, tree inst_expr)
{
  // The destination type might be different from the output register
  // variable type (which is always an unsigned integer type).
  tree output_type = TREE_TYPE (output);
  tree input_type = TREE_TYPE (inst_expr);
  bool is_fp16 =
    (brig_inst.type & 0x01F) == BRIG_TYPE_F16 &&
    brig_inst.base.kind != BRIG_KIND_INST_MEM &&
    !gccbrig_is_raw_operation (brig_inst.opcode);

  bool ftz = false; // flush to zero
  const BrigBase *base = &brig_inst.base;

  if (base->kind == BRIG_KIND_INST_MOD)
    {
      const BrigInstMod *mod = (const BrigInstMod*)base;
      ftz = mod->modifier.allBits & BRIG_ALU_FTZ;
    }
  else if (base->kind == BRIG_KIND_INST_CMP)
    {
      const BrigInstCmp *cmp = (const BrigInstCmp*)base;
      ftz = cmp->modifier.allBits & BRIG_ALU_FTZ;
    }

  if (TREE_CODE (inst_expr) == CALL_EXPR)
    {
      tree func_decl = TREE_OPERAND (TREE_OPERAND (inst_expr, 1), 0);
      input_type = TREE_TYPE (TREE_TYPE (func_decl));
    }

  if (ftz && (VECTOR_FLOAT_TYPE_P (TREE_TYPE (inst_expr)) ||
	      SCALAR_FLOAT_TYPE_P (TREE_TYPE (inst_expr)) ||
	      is_fp16))
    {
      // Ensure we don't duplicate the arithmetics to the
      // arguments of the bit field reference operators.
      inst_expr = add_temp_var ("before_ftz", inst_expr);
      inst_expr = flush_to_zero (is_fp16)(*this, inst_expr);
    }

  if (is_fp16)
    {
      inst_expr = add_temp_var ("before_f2h", inst_expr);
      tree f2h_output = build_f2h_conversion (inst_expr);
      tree conv_int = convert_to_integer (output_type, f2h_output);
      tree assign = build2 (MODIFY_EXPR, output_type, output, conv_int);
      parent_.append_statement (assign);
      return assign;
    }
  else if (VECTOR_TYPE_P (TREE_TYPE (output)))
    {
      // Expand/unpack the input value to the given vector elements.
      size_t i;
      tree input = inst_expr;
      tree element_type = get_tree_type_for_hsa_type (brig_inst.type);
      tree element;
      tree last_assign = NULL_TREE;
      FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (output), i, element)
	{
	  tree element_ref = build3
	    (BIT_FIELD_REF, element_type, input, TYPE_SIZE (element_type),
	     build_int_cst
	     (uint32_type_node, i*int_size_in_bytes (element_type)*8));

	  last_assign = build_output_assignment (brig_inst, element,
						 element_ref);
	}
      return last_assign;
    }
  else
    {
      // All we do here is to bitcast the result and store it to the
      // 'register' (variable). Mainly need to take care of differing
      // bitwidths.
      size_t src_width = int_size_in_bytes (input_type);
      size_t dst_width = int_size_in_bytes (output_type);

      if (src_width == dst_width)
	{
	  // A simple bitcast should do.
	  tree bitcast = build_reinterpret_cast	(output_type, inst_expr);
	  tree assign = build2
	    (MODIFY_EXPR, output_type, output, bitcast);
	  parent_.append_statement (assign);
	  return assign;
	}
      else
	{
	  if (CONVERT_EXPR_P (inst_expr) && POINTER_TYPE_P (inst_expr))
	    {
	      // convert_to_integer crashes when converting a view convert
	      // expr to a pointer. First cast it to a large enough int
	      // and let the next integer conversion do the clipping.
	      inst_expr = convert_to_integer (size_type_node, inst_expr);
	    }

	  tree conv_int = convert_to_integer (output_type, inst_expr);
	  tree assign = build2 (MODIFY_EXPR, output_type, output, conv_int);
	  parent_.append_statement (assign);
	  return assign;
	}
    }
  return NULL_TREE;
}

void
brig_code_entry_handler::append_statement (tree stmt)
{
  parent_.append_statement (stmt);
}

void
brig_code_entry_handler::unpack (tree value, tree_stl_vec& elements)
{
  size_t vec_size = int_size_in_bytes (TREE_TYPE (value));
  size_t element_size = int_size_in_bytes	(TREE_TYPE (TREE_TYPE (value)))*8;
  size_t element_count = vec_size*8 / element_size;

  tree input_element_type = TREE_TYPE (TREE_TYPE (value));

  value = add_temp_var ("unpack_input", value);

  for (size_t i = 0; i < element_count; ++i)
    {
      tree element = build3 (BIT_FIELD_REF, input_element_type, value,
			     TYPE_SIZE (input_element_type),
			     build_int_cst (unsigned_char_type_node,
					    i*element_size));

      element = add_temp_var ("scalar", element);
      elements.push_back (element);
    }
}

tree
brig_code_entry_handler::pack (tree_stl_vec& elements)
{
  size_t element_count = elements.size ();

  gcc_assert (element_count > 1);

  tree output_element_type = TREE_TYPE (elements.at (0));

  vec<constructor_elt, va_gc> *constructor_vals = NULL;
  for (size_t i = 0; i < element_count; ++i)
    CONSTRUCTOR_APPEND_ELT (constructor_vals, NULL_TREE, elements.at (i));

  tree vec_type = build_vector_type (output_element_type, element_count);

  // build_constructor creates a vector type which is not a vector_cst
  // that requires compile time constant elements.
  tree vec = build_constructor (vec_type, constructor_vals);

  // Add a temp variable for readability.
  tree tmp_var = create_tmp_var (vec_type, "vec_out");
  tree vec_tmp_assign = build2
    (MODIFY_EXPR, TREE_TYPE (tmp_var), tmp_var, vec);
  parent_.append_statement (vec_tmp_assign);
  return tmp_var;
}

tree
tree_element_unary_visitor::operator() (
					brig_code_entry_handler& handler, tree operand)
{
  if (VECTOR_TYPE_P (TREE_TYPE(operand)))
    {
      size_t vec_size = int_size_in_bytes (TREE_TYPE (operand));
      size_t element_size = int_size_in_bytes
	(TREE_TYPE (TREE_TYPE (operand)));
      size_t element_count = vec_size / element_size;

      tree input_element_type = TREE_TYPE (TREE_TYPE (operand));
      tree output_element_type = NULL_TREE;

      vec<constructor_elt, va_gc> *constructor_vals = NULL;
      for (size_t i = 0; i < element_count; ++i)
	{
	  tree element = build3 (BIT_FIELD_REF, input_element_type, operand,
				 TYPE_SIZE (input_element_type),
				 build_int_cst (unsigned_char_type_node,
						i*element_size*8));

	  tree output = visit_element (handler, element);
	  output_element_type = TREE_TYPE (output);

	  CONSTRUCTOR_APPEND_ELT (constructor_vals, NULL_TREE, output);
	}

      tree vec_type = build_vector_type (output_element_type, element_count);

      // build_constructor creates a vector type which is not a vector_cst
      // that requires compile time constant elements.
      tree vec = build_constructor (vec_type, constructor_vals);

      // Add a temp variable for readability.
      tree tmp_var = create_tmp_var (vec_type, "vec_out");
      tree vec_tmp_assign = build2
	(MODIFY_EXPR, TREE_TYPE (tmp_var), tmp_var, vec);
      handler.append_statement (vec_tmp_assign);
      return tmp_var;
    }
  else
    return visit_element (handler, operand);
}

tree
tree_element_binary_visitor::operator() (
					 brig_code_entry_handler& handler, tree operand0, tree operand1)
{
  if (VECTOR_TYPE_P (TREE_TYPE(operand0)))
    {
      gcc_assert (VECTOR_TYPE_P (TREE_TYPE(operand1)));
      size_t vec_size = int_size_in_bytes (TREE_TYPE (operand0));
      size_t element_size = int_size_in_bytes
	(TREE_TYPE (TREE_TYPE (operand0)));
      size_t element_count = vec_size / element_size;

      tree input_element_type = TREE_TYPE (TREE_TYPE (operand0));
      tree output_element_type = NULL_TREE;

      vec<constructor_elt, va_gc> *constructor_vals = NULL;
      for (size_t i = 0; i < element_count; ++i)
	{

	  tree element0 = build3 (BIT_FIELD_REF, input_element_type, operand0,
				  TYPE_SIZE (input_element_type),
				  build_int_cst (unsigned_char_type_node,
						 i*element_size*8));

	  tree element1 = build3 (BIT_FIELD_REF, input_element_type, operand1,
				  TYPE_SIZE (input_element_type),
				  build_int_cst (unsigned_char_type_node,
						 i*element_size*8));

	  tree output = visit_element (handler, element0, element1);
	  output_element_type = TREE_TYPE (output);

	  CONSTRUCTOR_APPEND_ELT (constructor_vals, NULL_TREE, output);
	}

      tree vec_type = build_vector_type (output_element_type, element_count);

      // build_constructor creates a vector type which is not a vector_cst
      // that requires compile time constant elements.
      tree vec = build_constructor (vec_type, constructor_vals);

      // Add a temp variable for readability.
      tree tmp_var = create_tmp_var (vec_type, "vec_out");
      tree vec_tmp_assign = build2
	(MODIFY_EXPR, TREE_TYPE (tmp_var), tmp_var, vec);
      handler.append_statement (vec_tmp_assign);
      return tmp_var;
    }
  else
    return visit_element (handler, operand0, operand1);
}

tree
flush_to_zero::visit_element (brig_code_entry_handler&, tree operand)
{
  size_t size = int_size_in_bytes (TREE_TYPE (operand));
  if (size == 4)
    {
      const char* builtin_fn_name =
	(m_fp16) ? "__phsa_builtin_ftz_f32_f16" :
	"__phsa_builtin_ftz_f32";

      return call_builtin
	(NULL, builtin_fn_name, 1, float_type_node, float_type_node,
	 operand);
    }
  else if (size == 8)
    {
      static tree builtin_ftz_d;
      return call_builtin
	(&builtin_ftz_d, "__phsa_builtin_ftz_f64", 1, double_type_node,
	 double_type_node, operand);
    }
  else
    error ("Unsupported float size %lu for FTZ.", size);
  return NULL_TREE;
}

tree
float_to_half::visit_element (brig_code_entry_handler& caller, tree operand)
{
  static tree gnu_f2h_ieee = NULL_TREE;

  tree casted_operand =
    build_reinterpret_cast (uint32_type_node, operand);

  tree call = call_builtin
    (&gnu_f2h_ieee, "__gnu_f2h_ieee", 1, uint16_type_node,
     uint32_type_node, casted_operand);
  tree output = create_tmp_var (TREE_TYPE (TREE_TYPE (gnu_f2h_ieee)), "fp16out");
  tree assign = build2 (MODIFY_EXPR, TREE_TYPE (output), output, call);
  caller.append_statement (assign);
  return output;
}

tree
half_to_float::visit_element (brig_code_entry_handler& caller, tree operand)
{
  static tree gnu_h2f_ieee = NULL_TREE;
  tree truncated_source =
    convert_to_integer (uint16_type_node, operand);

  tree call = call_builtin
    (&gnu_h2f_ieee, "__gnu_h2f_ieee", 1, uint32_type_node,
     uint16_type_node, truncated_source);

  tree const_fp32_type = build_type_variant (brig_to_generic::s_fp32_type, 1, 0);

  tree output = create_tmp_var (const_fp32_type, "fp32out");
  tree casted_result = build_reinterpret_cast (brig_to_generic::s_fp32_type, call);

  tree assign = build2 (MODIFY_EXPR, TREE_TYPE (output), output, casted_result);

  caller.append_statement (assign);

  return output;
}

// Treats the INPUT as SRC_TYPE and sign or zero extends it to DEST_TYPE.
tree
brig_code_entry_handler::extend_int (tree input, tree dest_type, tree src_type)
{
  // Extend integer conversions according to the destination's
  // ext mode. First we need to clip the input register to
  // the possible smaller integer size to ensure the correct sign
  // bit is extended.
  tree clipped_input = convert_to_integer (src_type, input);
  tree conversion_result;

  if (TYPE_UNSIGNED (src_type))
    conversion_result = convert_to_integer
      (unsigned_type_for (dest_type), clipped_input);
  else
    conversion_result = convert_to_integer
      (signed_type_for (dest_type), clipped_input);

  // Treat the result as unsigned so we do not sign extend to the
  // register width. For some reason this GENERIC sequence sign
  // extends to the s register:
  /*
    D.1541 = (signed char) s1;
    D.1542 = (signed short) D.1541;
    s0 = (unsigned int) D.1542
  */

  // The converted result is then extended to the target register
  // width, using the same sign as the destination. FIXME: is this
  // correct, shouldn't it be TREE_TYPE (output)?
  return convert_to_integer (dest_type, conversion_result);
}

/**
 * Returns the integer constant value of the given node.
 * If it's a cast, looks into the source of the cast.
 */
HOST_WIDE_INT
brig_code_entry_handler::int_constant_value (tree node)
{
  tree n = node;
  if (TREE_CODE (n) == VIEW_CONVERT_EXPR)
    n = TREE_OPERAND (n, 0);
  return int_cst_value (n);
}
