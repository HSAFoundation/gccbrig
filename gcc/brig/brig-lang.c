/* brig-lang.c -- brig (HSAIL) input gcc interface.
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

#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "print-tree.h"
#include "stringpool.h"
#include "basic-block.h"
#include "gimple-expr.h"
#include "gimplify.h"
#include "dumpfile.h"
#include "cgraph.h"
#include "stor-layout.h"
#include "toplev.h"
#include "debug.h"
#include "options.h"
#include "flags.h"
#include "convert.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "target.h"
#include "vec.h"
#include "brigfrontend/brig_to_generic.h"
#include "machmode.h"

#include "common/common-target.h"

#include <mpfr.h>

#include "brig-c.h"

/* If -v set.  */
int gccbrig_verbose = 0;

/* Language-dependent contents of a type.  */

struct GTY (()) lang_type
{
  char dummy;
};

/* Language-dependent contents of a decl.  */

struct GTY ((variable_size)) lang_decl
{
  char dummy;
};

/* Language-dependent contents of an identifier.  This must include a
   tree_identifier.  */

struct GTY (()) lang_identifier
{
  struct tree_identifier common;
};

/* The resulting tree type.  */

union GTY ((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
	    chain_next ("CODE_CONTAINS_STRUCT (TREE_CODE (&%h.generic), "
			"TS_COMMON) ? ((union lang_tree_node *) TREE_CHAIN "
			"(&%h.generic)) : NULL"))) lang_tree_node
{
  union tree_node GTY ((tag ("0"), desc ("tree_node_structure (&%h)"))) generic;
  struct lang_identifier GTY ((tag ("1"))) identifier;
};

/* We don't use language_function.  */

struct GTY (()) language_function
{
  int dummy;
};

/* Language hooks.  */

static bool
brig_langhook_init (void)
{
  build_common_tree_nodes (false, false);

  /* From Go: I don't know why this has to be done explicitly.  */
  void_list_node = build_tree_list (NULL_TREE, void_type_node);

  targetm.init_builtins ();
  build_common_builtin_nodes ();

  return true;
}

/* The option mask.  */

static unsigned int
brig_langhook_option_lang_mask (void)
{
  return CL_BRIG;
}

/* Initialize the options structure.  */

static void
brig_langhook_init_options_struct (struct gcc_options *opts)
{
  /* Signed overflow is precisely defined.  */
  opts->x_flag_wrapv = 1;

  opts->x_optimize = 3;

  /* If we set this to one, the whole program optimizations internalize
     all global variables, making them invisible to the .so loader (and
     thus the Portable HSA Runtime API).  */
  opts->x_flag_whole_program = 0;

  /* The builtin math functions should not set errno.  */
  opts->x_flag_errno_math = 0;
  opts->frontend_set_flag_errno_math = false;

  opts->x_flag_exceptions = 0;
  opts->x_flag_non_call_exceptions = 0;

  opts->x_flag_finite_math_only = 0;
  opts->x_flag_signed_zeros = 1;
}

/* Handle Brig specific options.  Return 0 if we didn't do anything.  */

static bool
brig_langhook_handle_option (
  size_t scode ATTRIBUTE_UNUSED, const char *arg ATTRIBUTE_UNUSED,
  int value ATTRIBUTE_UNUSED, int kind ATTRIBUTE_UNUSED,
  location_t loc ATTRIBUTE_UNUSED,
  const struct cl_option_handlers *handlers ATTRIBUTE_UNUSED)
{
  enum opt_code code = (enum opt_code) scode;
  switch (code)
    {
    case OPT_v:
      gccbrig_verbose = 1;
      break;
    default:
      break;
    }
  return 1;
}

/* Run after parsing options.  */

static bool
brig_langhook_post_options (const char **pfilename ATTRIBUTE_UNUSED)
{
  if (flag_excess_precision_cmdline == EXCESS_PRECISION_DEFAULT)
    flag_excess_precision_cmdline = EXCESS_PRECISION_STANDARD;

  /* gccbrig casts pointers around like crazy, TBAA produces
	   broken code if not force disabling it.  */
  flag_strict_aliasing = 0;

  /* Returning false means that the backend should be used.  */
  return false;
}

static size_t
get_file_size (FILE *file)
{
  size_t size;
  fseek (file, 0, SEEK_END);
  size = (size_t) ftell (file);
  fseek (file, 0, SEEK_SET);
  return size;
}

static brig_to_generic *brig_to_gen = NULL;

static void
brig_langhook_parse_file (void)
{
  if (brig_to_gen == NULL)
    brig_to_gen = new brig_to_generic;

  for (unsigned int i = 0; i < num_in_fnames; ++i)
    {

      FILE *f;
      f = fopen (in_fnames[i], "r");
      size_t fsize = get_file_size (f);
      char *brig_blob = new char[fsize];
      if (fread (brig_blob, 1, fsize, f) != fsize)
	{
	  error ("Could not read the BRIG file.");
	  exit (1);
	}
      brig_to_gen->parse (brig_blob);
      fclose (f);
    }
}

static tree
brig_langhook_type_for_size (unsigned int bits ATTRIBUTE_UNUSED,
			     int unsignedp ATTRIBUTE_UNUSED)
{
  if (bits == 64)
    return unsignedp ? uint64_type_node : long_integer_type_node;

  printf ("brig: type for size %u %u\n", bits, unsignedp);
  internal_error ("TODO.");
  return NULL_TREE;
}

// Most of the implementation adapted (stolen) from gogo-tree.cc and
// go-lang.c.
static tree
brig_langhook_type_for_mode (enum machine_mode mode, int unsignedp)
{
  if (mode == TYPE_MODE (void_type_node))
    return void_type_node;

  if (VECTOR_MODE_P (mode))
    {
      tree inner;

      inner = brig_langhook_type_for_mode (GET_MODE_INNER (mode), unsignedp);
      if (inner != NULL_TREE)
	return build_vector_type_for_mode (inner, mode);
      internal_error ("unsupported vector mode %s unsignedp %d\n",
		      GET_MODE_NAME (mode), unsignedp);

      return NULL_TREE;
    }

  // FIXME: This static_cast should be in machmode.h.
  enum mode_class mc = (enum mode_class) (GET_MODE_CLASS (mode));
  if (mc == MODE_FLOAT)
    {
      switch (GET_MODE_BITSIZE (mode))
	{
	case 32:
	  return float_type_node;
	case 64:
	  return double_type_node;
	default:
	  // We have to check for long double in order to support
	  // i386 excess precision.
	  if (mode == TYPE_MODE (long_double_type_node))
	    return long_double_type_node;

	  internal_error ("unsupported float mode %s unsignedp %d\n",
			  GET_MODE_NAME (mode), unsignedp);
	  return NULL_TREE;
	}
    }
  else if (mc == MODE_INT)
    {
      switch (GET_MODE_BITSIZE (mode))
	{
	case 8:
	  gcc_assert (int_size_in_bytes (signed_char_type_node) == 1);
	  return unsignedp ? unsigned_char_type_node : signed_char_type_node;
	case 16:
	  gcc_assert (int_size_in_bytes (short_integer_type_node) == 2);
	  return unsignedp ? uint16_type_node : short_integer_type_node;
	case 32:
	  gcc_assert (int_size_in_bytes (integer_type_node) == 4);
	  return unsignedp ? uint32_type_node : integer_type_node;
	case 64:
	  gcc_assert (int_size_in_bytes (long_integer_type_node) == 8);
	  return unsignedp ? uint64_type_node : long_integer_type_node;
	case 128:
	  return unsignedp ? int128_unsigned_type_node
			   : int128_integer_type_node;
	default:
	  internal_error ("unsupported int mode %s unsignedp %d size %d\n",
			  GET_MODE_NAME (mode), unsignedp,
			  GET_MODE_BITSIZE (mode));
	  return NULL_TREE;
	}
    }
  else
    {
      /* E.g., build_common_builtin_nodes () asks for modes/builtins
	       we do not generate or need.  Just ignore them silently for now.
      internal_error ("unsupported mode %s unsignedp %d\n",
		      GET_MODE_NAME (mode), unsignedp);
      */
      return void_type_node;
    }
  return NULL_TREE;
}

/* Record a builtin function.  We just ignore builtin functions.  */

static tree
brig_langhook_builtin_function (tree decl)
{
  return decl;
}

/* Return true if we are in the global binding level.  */

static bool
brig_langhook_global_bindings_p (void)
{
  return current_function_decl == NULL_TREE;
}

/* Push a declaration into the current binding level.  From Go: We can't
   usefully implement this since we don't want to convert from tree
   back to one of our internal data structures.  I think the only way
   this is used is to record a decl which is to be returned by
   getdecls, and we could implement it for that purpose if
   necessary.  */

static tree
brig_langhook_pushdecl (tree decl ATTRIBUTE_UNUSED)
{
  gcc_unreachable ();
}

/* This hook is used to get the current list of declarations as trees.
   From Go: We don't support that; instead we use the write_globals hook.  This
   can't simply crash because it is called by -gstabs.  */
static tree
brig_langhook_getdecls (void)
{
  return NULL;
}

/* Write out globals.  */

static void
brig_langhook_write_globals (void)
{
  gcc_assert (brig_to_gen != NULL);
  brig_to_gen->write_globals ();
}

static int
brig_langhook_gimplify_expr (tree *expr_p, gimple_seq *pre_p ATTRIBUTE_UNUSED,
			     gimple_seq *post_p ATTRIBUTE_UNUSED)
{

  // Strip off the static chain info that appears to function
  // calls for some strange reason even though we don't add
  // nested functions.  Maybe something wrong with the function
  // declaration contexts?
  if (TREE_CODE (*expr_p) == CALL_EXPR
      && CALL_EXPR_STATIC_CHAIN (*expr_p) != NULL_TREE)
    CALL_EXPR_STATIC_CHAIN (*expr_p) = NULL_TREE;
  return GS_UNHANDLED;
}

/* Return a decl for the exception personality function.  The function
   itself is implemented in libbrig/runtime/brig-unwind.c.  */

static tree
brig_langhook_eh_personality (void)
{
  gcc_unreachable ();
}

/* Functions called directly by the generic backend.  */

/* from go-lang.c */
tree
convert (tree type ATTRIBUTE_UNUSED, tree expr ATTRIBUTE_UNUSED)
{
  if (type == error_mark_node || expr == error_mark_node
      || TREE_TYPE (expr) == error_mark_node)
    return error_mark_node;

  if (type == TREE_TYPE (expr))
    return expr;

  if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (TREE_TYPE (expr)))
    return fold_convert (type, expr);

  switch (TREE_CODE (type))
    {
    case VOID_TYPE:
    case BOOLEAN_TYPE:
      return fold_convert (type, expr);
    case INTEGER_TYPE:
      return fold (convert_to_integer (type, expr));
    case REAL_TYPE:
      return fold (convert_to_real (type, expr));
    case VECTOR_TYPE:
      return build1 (VIEW_CONVERT_EXPR, type, expr);
    default:
      break;
    }

  debug_tree (type);
  debug_tree (expr);
  internal_error ("Cannot convert!");
}

/* FIXME: This is a hack to preserve trees that we create from the
   garbage collector.  */
static GTY (()) tree brig_gc_root;

void
brig_preserve_from_gc (tree t)
{
  brig_gc_root = tree_cons (NULL_TREE, t, brig_gc_root);
}

/* Convert an identifier for use in an error message.  */

const char *
brig_localize_identifier (const char *ident)
{
  return identifier_to_locale (ident);
}

#undef LANG_HOOKS_NAME
#undef LANG_HOOKS_INIT
#undef LANG_HOOKS_OPTION_LANG_MASK
#undef LANG_HOOKS_INIT_OPTIONS_STRUCT
#undef LANG_HOOKS_HANDLE_OPTION
#undef LANG_HOOKS_POST_OPTIONS
#undef LANG_HOOKS_PARSE_FILE
#undef LANG_HOOKS_TYPE_FOR_MODE
#undef LANG_HOOKS_TYPE_FOR_SIZE
#undef LANG_HOOKS_BUILTIN_FUNCTION
#undef LANG_HOOKS_GLOBAL_BINDINGS_P
#undef LANG_HOOKS_PUSHDECL
#undef LANG_HOOKS_GETDECLS
#undef LANG_HOOKS_WRITE_GLOBALS
#undef LANG_HOOKS_GIMPLIFY_EXPR
#undef LANG_HOOKS_EH_PERSONALITY

#define LANG_HOOKS_NAME "GNU Brig"
#define LANG_HOOKS_INIT brig_langhook_init
#define LANG_HOOKS_OPTION_LANG_MASK brig_langhook_option_lang_mask
#define LANG_HOOKS_INIT_OPTIONS_STRUCT brig_langhook_init_options_struct
#define LANG_HOOKS_HANDLE_OPTION brig_langhook_handle_option
#define LANG_HOOKS_POST_OPTIONS brig_langhook_post_options
#define LANG_HOOKS_PARSE_FILE brig_langhook_parse_file
#define LANG_HOOKS_TYPE_FOR_MODE brig_langhook_type_for_mode
#define LANG_HOOKS_TYPE_FOR_SIZE brig_langhook_type_for_size
#define LANG_HOOKS_BUILTIN_FUNCTION brig_langhook_builtin_function
#define LANG_HOOKS_GLOBAL_BINDINGS_P brig_langhook_global_bindings_p
#define LANG_HOOKS_PUSHDECL brig_langhook_pushdecl
#define LANG_HOOKS_GETDECLS brig_langhook_getdecls
#define LANG_HOOKS_WRITE_GLOBALS brig_langhook_write_globals
#define LANG_HOOKS_GIMPLIFY_EXPR brig_langhook_gimplify_expr
#define LANG_HOOKS_EH_PERSONALITY brig_langhook_eh_personality

struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

#include "gt-brig-brig-lang.h"
#include "gtype-brig.h"
