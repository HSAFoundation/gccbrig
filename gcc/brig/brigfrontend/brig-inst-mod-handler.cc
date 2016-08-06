/* brig-inst-mod-handler.cc -- brig rounding moded instruction handling
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

#include "brig-code-entry-handler.h"

#include "gimple-expr.h"
#include "errors.h"

size_t
brig_inst_mod_handler::generate (const BrigBase *base)
{
  brig_basic_inst_handler basic_handler (m_parent);
  return basic_handler (base);
}

const BrigAluModifier8_t *
brig_inst_mod_handler::modifier (const BrigBase *base) const
{
  const BrigInstMod *inst = (const BrigInstMod *) base;
  return &inst->modifier;
}

const BrigRound8_t *
brig_inst_mod_handler::round (const BrigBase *base) const
{
  const BrigInstMod *inst = (const BrigInstMod *) base;
  return &inst->round;
}

/* This used to inject fesetround () calls to control the rounding mode of the
   actual executed floating point operation.  It turned out that supporting
   conversions using fesetround calls won't work in gcc due to it not being able
   to restrict code motions across calls at the moment.  This functionality is
   therefore disabled for now until a better solution is found or if
   fesetround () is fixed in gcc.  */
size_t
brig_inst_mod_handler::operator () (const BrigBase *base)
{
  return generate (base);

#if 0
  const BrigAluModifier *inst_modifier = modifier (base);
  const bool FTZ = inst_modifier != NULL
    && inst_modifier->allBits & BRIG_ALU_FTZ;
  if (FTZ)
    {
      static tree built_in = NULL_TREE;
      tree call = call_builtin (&built_in, "__hsail_enable_ftz", 0,
				void_type_node);
      m_parent.append_statement (call);
      DECL_IS_NOVOPS (built_in) = 1;
      TREE_SIDE_EFFECTS (call) = 1;
    }

  const BrigRound8_t *round_modifier = round (base);

  /* TODO: Set the default rounding mode once in the function entry (and
     restore at function exit), and assume it's set by default so we don't
     have to reset it for each instruction.  */
  BrigRound8_t brig_rounding_mode = m_parent.default_float_rounding_mode;
  if (round_modifier != NULL)
    brig_rounding_mode = *round_modifier;

  tree new_mode = NULL_TREE;
  switch (brig_rounding_mode)
    {
      /* TODO: these are from fenv.h for x86-64/Linux, there should
	 be a target hook for querying the parameters or getting
	 the trees to change the modes directly.  */
    case BRIG_ROUND_INTEGER_NEAR_EVEN:
    case BRIG_ROUND_FLOAT_NEAR_EVEN: /* FE_TONEAREST */
      new_mode = build_int_cst (integer_type_node, 0);
      break;
    case BRIG_ROUND_FLOAT_ZERO: /* FE_TOWARDZERO */
      new_mode = build_int_cst (integer_type_node, 0xc00);
      break;
    case BRIG_ROUND_FLOAT_PLUS_INFINITY: /* FE_UPWARD */
      new_mode = build_int_cst (integer_type_node, 0x800);
      break;
    case BRIG_ROUND_FLOAT_MINUS_INFINITY: /* FE_DOWNWARD */
      new_mode = build_int_cst (integer_type_node, 0x400);
      break;
    case BRIG_ROUND_NONE:
      new_mode = NULL_TREE;
      break;
    case BRIG_ROUND_FLOAT_DEFAULT:
      break;
    default:
      gcc_unreachable ();
      break;
    }

    tree old_mode = NULL_TREE;
    if (new_mode != NULL_TREE)
      {
	/* TODO: the target might have rounding
	   modes per instruction, then the mode switching should
	   not be used but the correct opcode should be called instead.

	   Emit a call to fegetround () to save the current
	   rounding mode to a temporary variable.
	   atomic_assign_expand_fenv () target hook
	   is close to what is wanted here, but it is meant for
	   disabling exceptions during an atomic operation.  */

	tree save_call = build_call_expr (m_parent.fegetround_fn, 0);
	tree ret_type = TREE_TYPE (TREE_TYPE (m_parent.fegetround_fn));
	old_mode = create_tmp_var (ret_type, 0);

	tree assign_temp
	  = build2 (MODIFY_EXPR, TREE_TYPE (old_mode), old_mode, save_call);
	m_parent.append_statement (assign_temp);

	/* Switch the rounding mode to what is requested
	   by the HSAIL instruction.  */
	tree set_call = build_call_expr (m_parent.fesetround_fn, 1, new_mode);
	m_parent.append_statement (set_call);
      }

    /* Delegate the generation of the actual instruction to
       the base instruction handler.  */
    size_t count = generate (base);

    if (new_mode != NULL_TREE)
    {
      /* TODO: Emit a call to fesetround (int rounding_mode) to
	 set the rounding mode to the one stated in the modifier.  */

      tree restore_call = build_call_expr (m_parent.fesetround_fn, 1,
					   old_mode);
      m_parent.append_statement (restore_call);
    }

    if (FTZ)
      {
	static tree built_in = NULL_TREE;
	tree call = call_builtin (&built_in, "__hsail_disable_ftz", 0,
				  void_type_node);
	m_parent.append_statement (call);
	DECL_IS_NOVOPS (built_in) = 1;
	TREE_SIDE_EFFECTS (call) = 1;
      }

    /* OPTIMIZE: Remove unneeded rounding mode switches, e.g. for successive
       instructions with the same mode in the same BB.  */
  return count;
#endif
}
