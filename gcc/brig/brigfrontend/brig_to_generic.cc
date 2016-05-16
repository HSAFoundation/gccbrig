/* brig2tree.cc -- brig to gcc generic/gimple tree conversion
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

#include <cassert>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "brig_to_generic.h"
#include "stringpool.h"
#include "tree-iterator.h"
#include "toplev.h"
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
#include "fold-const.h"
#include "cgraph.h"
#include "dumpfile.h"
#include "tree-pretty-print.h"

extern int gccbrig_verbose;

tree brig_to_generic::s_fp16_type;
tree brig_to_generic::s_fp32_type;
tree brig_to_generic::s_fp64_type;

brig_to_generic::brig_to_generic ()
  : m_cf (NULL), m_brig (NULL), m_next_group_offset (0),
    m_next_private_offset (0)
{
  m_globals = NULL_TREE;

  // Initialize the basic REAL types.
  // This doesn't work straight away because most of the targets
  // do not support fp16 natively.  Let's by default convert
  // to fp32 and back before and after each instruction (handle it as
  // a storage format only), and later add an optimization pass
  // that removes the extra converts (in case of multiple fp16 ops
  // in a row).
  s_fp16_type = make_node (REAL_TYPE);
  TYPE_PRECISION (s_fp16_type) = 16;
  TYPE_SIZE (s_fp16_type) = bitsize_int (16);
  TYPE_SIZE_UNIT (s_fp16_type) = size_int (2);
  TYPE_ALIGN (s_fp16_type) = 16;
  layout_type (s_fp16_type);

  // TODO: make sure that the alignment of these types created in tree.c
  // are at least as strict than mandated by HSA, and conform to
  // IEEE (like mandated by HSA)
  s_fp32_type = float_type_node;
  s_fp64_type = double_type_node;

  // TODO: (machine)query the preferred rounding mode that is set by
  // the machine by default.  This can be redefined by each BRIG module
  // header.
  m_default_float_rounding_mode = BRIG_ROUND_FLOAT_ZERO;

  m_dump_file = dump_begin (TDI_original, &m_dump_flags);
}

class unimplemented_entry_handler : public brig_code_entry_handler
{
public:
  unimplemented_entry_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t
  operator () (const BrigBase *base)
  {
    internal_error ("BrigKind 0x%x unimplemented", base->kind);
    return base->byteCount;
  }
};

// Handler for entries that can be (and are) safely skipped
// for the purposes of tree generation.
class skipped_entry_handler : public brig_code_entry_handler
{
public:
  skipped_entry_handler (brig_to_generic &parent)
    : brig_code_entry_handler (parent)
  {
  }

  size_t
  operator () (const BrigBase *base)
  {
    return base->byteCount;
  }
};

void
brig_to_generic::parse (const char *brig_blob)
{
  m_brig = brig_blob;
  m_brig_blobs.push_back (brig_blob);

  const BrigModuleHeader *mheader = (const BrigModuleHeader *) brig_blob;

  m_data = m_code = m_operand = NULL;

  // Find the positions of the different sections.
  for (uint32_t sec = 0; sec < mheader->sectionCount; ++sec)
    {
      uint64_t offset
	= ((const uint64_t *) (brig_blob + mheader->sectionIndex))[sec];

      const BrigSectionHeader *section_header
	= (const BrigSectionHeader *) (brig_blob + offset);

      char *name = strndup ((const char *) (&section_header->name),
			    section_header->nameLength);

      if (sec == BRIG_SECTION_INDEX_DATA
	  && strncmp (name, "hsa_data", section_header->nameLength) == 0)
	{
	  m_data = (const char *) section_header;
	  m_data_size = section_header->byteCount;
	}
      else if (sec == BRIG_SECTION_INDEX_CODE
	       && strncmp (name, "hsa_code", section_header->nameLength) == 0)
	{
	  m_code = (const char *) section_header;
	  m_code_size = section_header->byteCount;
	}
      else if (sec == BRIG_SECTION_INDEX_OPERAND
	       && strncmp (name, "hsa_operand", section_header->nameLength)
		    == 0)
	{
	  m_operand = (const char *) section_header;
	  m_operand_size = section_header->byteCount;
	}
      else
	{
	  sorry ("section %s", name);
	}
      free (name);
    }

  if (m_code == NULL)
    error ("code section not found");
  if (m_data == NULL)
    error ("data section not found");
  if (m_operand == NULL)
    error ("operand section not found");

  brig_basic_inst_handler inst_handler (*this);
  brig_branch_inst_handler branch_inst_handler (*this);
  brig_cvt_inst_handler cvt_inst_handler (*this);
  brig_seg_inst_handler seg_inst_handler (*this);
  brig_copy_move_inst_handler copy_move_inst_handler (*this);
  brig_signal_inst_handler signal_inst_handler (*this);
  brig_atomic_inst_handler atomic_inst_handler (*this);
  brig_cmp_inst_handler cmp_inst_handler (*this);
  brig_mem_inst_handler mem_inst_handler (*this);
  brig_inst_mod_handler inst_mod_handler (*this);
  brig_directive_label_handler label_handler (*this);
  brig_directive_variable_handler var_handler (*this);
  brig_directive_fbarrier_handler fbar_handler (*this);
  brig_directive_comment_handler comment_handler (*this);
  brig_directive_function_handler func_handler (*this);
  brig_directive_control_handler control_handler (*this);
  brig_directive_arg_block_handler arg_block_handler (*this);
  brig_directive_module_handler module_handler (*this);
  brig_lane_inst_handler lane_inst_handler (*this);
  brig_queue_inst_handler queue_inst_handler (*this);
  skipped_entry_handler skipped_handler (*this);
  unimplemented_entry_handler unimplemented_handler (*this);

  struct code_entry_handler_info
  {
    BrigKind kind;
    brig_code_entry_handler *handler;
  };

  // @todo: Convert to a hash table / map.  For now, put the more common
  // entries to the top to keep the scan fast on average.
  code_entry_handler_info handlers[]
    = {{BRIG_KIND_INST_BASIC, &inst_handler},
       {BRIG_KIND_INST_CMP, &cmp_inst_handler},
       {BRIG_KIND_INST_MEM, &mem_inst_handler},
       {BRIG_KIND_INST_MOD, &inst_mod_handler},
       {BRIG_KIND_INST_CVT, &cvt_inst_handler},
       {BRIG_KIND_INST_SEG_CVT, &seg_inst_handler},
       {BRIG_KIND_INST_SEG, &seg_inst_handler},
       {BRIG_KIND_INST_ADDR, &copy_move_inst_handler},
       {BRIG_KIND_INST_SOURCE_TYPE, &copy_move_inst_handler},
       {BRIG_KIND_INST_ATOMIC, &atomic_inst_handler},
       {BRIG_KIND_INST_SIGNAL, &signal_inst_handler},
       {BRIG_KIND_INST_BR, &branch_inst_handler},
       {BRIG_KIND_INST_LANE, &lane_inst_handler},
       {BRIG_KIND_INST_QUEUE, &queue_inst_handler},
       // Assuming fences are not needed.  FIXME: call builtins
       // when porting to a platform where they are.
       {BRIG_KIND_INST_MEM_FENCE, &skipped_handler},
       {BRIG_KIND_DIRECTIVE_LABEL, &label_handler},
       {BRIG_KIND_DIRECTIVE_VARIABLE, &var_handler},
       {BRIG_KIND_DIRECTIVE_ARG_BLOCK_START, &arg_block_handler},
       {BRIG_KIND_DIRECTIVE_ARG_BLOCK_END, &arg_block_handler},
       {BRIG_KIND_DIRECTIVE_FBARRIER, &fbar_handler},
       {BRIG_KIND_DIRECTIVE_COMMENT, &comment_handler},
       {BRIG_KIND_DIRECTIVE_KERNEL, &func_handler},
       {BRIG_KIND_DIRECTIVE_SIGNATURE, &func_handler},
       {BRIG_KIND_DIRECTIVE_FUNCTION, &func_handler},
       {BRIG_KIND_DIRECTIVE_INDIRECT_FUNCTION, &func_handler},
       {BRIG_KIND_DIRECTIVE_MODULE, &module_handler},
       // Skipping debug locations for now as not needed for conformance.
       {BRIG_KIND_DIRECTIVE_LOC, &skipped_handler},
       // There are no supported pragmas at this moment.
       {BRIG_KIND_DIRECTIVE_PRAGMA, &skipped_handler},
       {BRIG_KIND_DIRECTIVE_CONTROL, &control_handler},
       {BRIG_KIND_DIRECTIVE_EXTENSION, &skipped_handler}};

  const BrigSectionHeader *dsection_header = (const BrigSectionHeader *) m_data;

  // Go through the data section just to sanity check the BRIG data section.
  for (size_t b = dsection_header->headerByteCount; b < m_data_size;)
    {
      const BrigData *entry = (const BrigData *) (m_data + b);
      // Rounds upwards towards the closest multiple of 4.
      // The byteCount itself is 4 bytes and included in 7.
      b += ((7 + entry->byteCount) / 4) * 4;

      // There can be zero padding at the end of the section to round the
      // size to a 4 multiple.  Break before trying to read that in as
      // an incomplete BrigData.
      if (m_data_size - b < sizeof (BrigData))
	break;
    }

  const BrigSectionHeader *csection_header = (const BrigSectionHeader *) m_code;

  for (size_t b = csection_header->headerByteCount; b < m_code_size;)
    {
      const BrigBase *entry = (const BrigBase *) (m_code + b);

      brig_code_entry_handler *handler = &unimplemented_handler;

      if (m_cf != NULL && b >= m_cf->m_brig_def->nextModuleEntry)
	finish_function (); // The function definition ended.

      // Find a handler.
      for (size_t i = 0;
	   i < sizeof (handlers) / sizeof (code_entry_handler_info); ++i)
	{
	  if (handlers[i].kind == entry->kind)
	    handler = handlers[i].handler;
	}
      b += (*handler) (entry);
      continue;
    }

  finish_function ();
}

const BrigData *
brig_to_generic::get_brig_data_entry (size_t entry_offset) const
{
  return (const BrigData *) (m_data + entry_offset);
}

const BrigBase *
brig_to_generic::get_brig_operand_entry (size_t entry_offset) const
{
  return (const BrigBase *) (m_operand + entry_offset);
}

const BrigBase *
brig_to_generic::get_brig_code_entry (size_t entry_offset) const
{
  return (const BrigBase *) (m_code + entry_offset);
}

void
brig_to_generic::append_global (tree g)
{
  if (m_globals == NULL_TREE)
    {
      m_globals = g;
      return;
    }
  else
    {
      tree last = tree_last (m_globals);
      TREE_CHAIN (last) = g;
    }
}

tree
brig_to_generic::global_variable (const std::string &name) const
{
  label_index::const_iterator i = m_global_variables.find (name);
  if (i == m_global_variables.end ())
    return NULL_TREE;
  else
    return (*i).second;
}

tree
brig_to_generic::function_decl (const std::string &name)
{
  label_index::const_iterator i = m_function_index.find (name);
  if (i == m_function_index.end ())
    return NULL_TREE;
  return (*i).second;
}

void
brig_to_generic::add_function_decl (const std::string &name, tree func_decl)
{
  m_function_index[name] = func_decl;
}

void
brig_to_generic::add_global_variable (const std::string &name, tree var_decl)
{
  append_global (var_decl);
  m_global_variables[name] = var_decl;

  // If we have generated a host def var ptr for this global variable
  // definition (because there was a declaration earlier which looked
  // like it might have been a host defined variable), we now have
  // to assign its address and make it non-public to allow the
  // references to point to the defined variable instead.
  std::string host_def_var_name
    = std::string (PHSA_HOST_DEF_PTR_PREFIX) + name;
  tree host_def_var = global_variable (host_def_var_name.c_str ());
  if (host_def_var == NULL_TREE)
    return;

  tree ptype = build_pointer_type (TREE_TYPE (var_decl));
  tree var_addr = build1 (ADDR_EXPR, ptype, var_decl);

  DECL_INITIAL (host_def_var) = var_addr;
  TREE_PUBLIC (host_def_var) = 0;
}

// Adds an indirection pointer for a potential host-defined program scope
// variable declaration.
void
brig_to_generic::add_host_def_var_ptr (const std::string &name, tree var_decl)
{
  std::string var_name = std::string (PHSA_HOST_DEF_PTR_PREFIX) + name;

  tree name_identifier = get_identifier (var_name.c_str ());

  tree ptr_var = build_decl (UNKNOWN_LOCATION, VAR_DECL, name_identifier,
			     build_pointer_type (TREE_TYPE (var_decl)));
  DECL_EXTERNAL (ptr_var) = 0;
  DECL_ARTIFICIAL (ptr_var) = 0;

  TREE_PUBLIC (ptr_var) = 1;
  TREE_USED (ptr_var) = 1;
  TREE_ADDRESSABLE (ptr_var) = 1;
  TREE_STATIC (ptr_var) = 1;

  append_global (ptr_var);
  m_global_variables[var_name] = ptr_var;
}

std::string
brig_to_generic::get_mangled_name
(const BrigDirectiveExecutable *func) const
{
  // Strip the leading &.
  std::string func_name = get_string (func->name).substr (1);
  if (func->linkage == BRIG_LINKAGE_MODULE)
    {
      // Mangle the module scope function names with the module name and
      // make them public so they can be queried by the HSA runtime from
      // the produced binary.  Assume it's the currently processed function
      // we are always referring to.
      func_name = "gccbrig." + m_module_name + "." + func_name;
    }
  return func_name;
}

char *
brig_to_generic::get_c_string (size_t entry_offset) const
{
  const BrigData *data_item = get_brig_data_entry (entry_offset);
  return strndup ((const char *) &data_item->bytes, data_item->byteCount);
}

std::string
brig_to_generic::get_string (size_t entry_offset) const
{
  const BrigData *data_item = get_brig_data_entry (entry_offset);
  return std::string ((const char *) &data_item->bytes, data_item->byteCount);
}

// Adapted from c-semantics.c.
tree
build_stmt (enum tree_code code, ...)
{
  tree ret;
  int length, i;
  va_list p;
  bool side_effects;

  /* This function cannot be used to construct variably-sized nodes.  */
  gcc_assert (TREE_CODE_CLASS (code) != tcc_vl_exp);

  va_start (p, code);

  ret = make_node (code);
  TREE_TYPE (ret) = void_type_node;
  length = TREE_CODE_LENGTH (code);

  /* TREE_SIDE_EFFECTS will already be set for statements with
     implicit side effects.  Here we make sure it is set for other
     expressions by checking whether the parameters have side
     effects.  */

  side_effects = false;
  for (i = 0; i < length; i++)
    {
      tree t = va_arg (p, tree);
      if (t && !TYPE_P (t))
	side_effects |= TREE_SIDE_EFFECTS (t);
      TREE_OPERAND (ret, i) = t;
    }

  TREE_SIDE_EFFECTS (ret) |= side_effects;

  va_end (p);
  return ret;
}

tree
build_reinterpret_cast (tree destination_type, tree source)
{

  if (!source || !destination_type || TREE_TYPE (source) == NULL_TREE
      || destination_type == NULL_TREE)
    {
      // TODO: handle void pointers etc.
      internal_error ("illegal type");
      return NULL_TREE;
    }

  tree source_type = TREE_TYPE (source);
  if (TREE_CODE (source) == CALL_EXPR)
    {
      tree func_decl = TREE_OPERAND (TREE_OPERAND (source, 1), 0);
      source_type = TREE_TYPE (TREE_TYPE (func_decl));
    }

  if (destination_type == source_type)
    return source;

  size_t src_size = int_size_in_bytes (source_type);
  size_t dst_size = int_size_in_bytes (destination_type);
  if (src_size <= dst_size)
    {
      tree conv = build1 (VIEW_CONVERT_EXPR, destination_type, source);
      return conv;
    }
  else
    {
      debug_tree (destination_type);
      debug_tree (source);
      internal_error ("Unable to truncate the source (%lu > %lu).", src_size,
		      dst_size);
    }
  return NULL_TREE;
}

// Returns a finished brig_function for the given generic FUNC_DECL,
// or NULL, if not found.
brig_function *
brig_to_generic::get_finished_function (tree func_decl)
{
  std::string func_name
    = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (func_decl)));
  std::map<std::string, brig_function *>::iterator i
    = m_finished_functions.find (func_name);
  if (i != m_finished_functions.end ())
    return (*i).second;
  else
    return NULL;
}

void
brig_to_generic::finish_function ()
{
  if (m_cf == NULL || m_cf->m_func_decl == NULL_TREE)
    {
      // It can be a finished func declaration fingerprint,
      // in that case we don't have m_func_decl.
      m_cf = NULL;
      return;
    }

  //debug_function (m_cf->m_func_decl,
  //		  TDF_VOPS|TDF_MEMSYMS|TDF_VERBOSE|TDF_ADDRESS);
  if (!m_cf->m_is_kernel)
    {
      m_cf->finish ();
      dump_function (m_cf);
      gimplify_function_tree (m_cf->m_func_decl);
      cgraph_node::finalize_function (m_cf->m_func_decl, true);
    }
  pop_cfun ();

  if (m_cf->m_is_kernel)
    m_kernels.push_back (m_cf);
  m_finished_functions[m_cf->m_name] = m_cf;
  m_cf = NULL;
}

void
brig_to_generic::start_function (tree f)
{
  if (DECL_STRUCT_FUNCTION (f) == NULL)
    push_struct_function (f);
  else
    push_cfun (DECL_STRUCT_FUNCTION (f));

  m_cf->m_func_decl = f;
}

void
brig_to_generic::append_group_variable (const std::string &name, size_t size,
					size_t alignment)
{
  size_t align_padding = m_next_group_offset % alignment;
  m_next_group_offset += align_padding;
  m_group_offsets[name] = m_next_group_offset;
  if (alignment > size) size = alignment;
  m_next_group_offset += size + align_padding;
}

size_t
brig_to_generic::group_variable_segment_offset (const std::string &name) const
{
  var_offset_table::const_iterator i = m_group_offsets.find (name);
  gcc_assert (i != m_group_offsets.end ());
  return (*i).second;
}

size_t
brig_to_generic::group_segment_size () const
{
  return m_next_group_offset;
}

void
brig_to_generic::append_private_variable (const std::string &name,
					  size_t size, size_t alignment)
{
  size_t align_padding = m_next_private_offset % alignment;
  m_next_private_offset += align_padding;
  m_private_offsets[name] = m_next_private_offset;
  if (alignment > size) size = alignment;
  m_next_private_offset += size;
  m_private_data_sizes[name] = size + align_padding;
}

size_t
brig_to_generic::private_variable_segment_offset (
  const std::string &name) const
{
  var_offset_table::const_iterator i = m_private_offsets.find (name);
  gcc_assert (i != m_private_offsets.end ());
  return (*i).second;
}

bool
brig_to_generic::has_private_variable (const std::string &name) const
{
  std::map<std::string, size_t>::const_iterator i
    = m_private_data_sizes.find (name);
  return i != m_private_data_sizes.end ();
}

bool
brig_to_generic::has_group_variable (const std::string &name) const
{
  var_offset_table::const_iterator i = m_group_offsets.find (name);
  return i != m_group_offsets.end ();
}

size_t
brig_to_generic::private_variable_size (const std::string &name) const
{
  std::map<std::string, size_t>::const_iterator i
    = m_private_data_sizes.find (name);
  gcc_assert (i != m_private_data_sizes.end ());
  return (*i).second;
}

size_t
brig_to_generic::private_segment_size () const
{
  return m_next_private_offset;
}

// Cached builtins indexed by name.
typedef std::map<std::string, tree> builtin_index;
builtin_index builtin_cache_;

tree
call_builtin (tree *pdecl, const char *name, int nargs, tree rettype, ...)
{
  if (rettype == error_mark_node)
    return error_mark_node;

  tree *types = new tree[nargs];
  tree *args = new tree[nargs];

  va_list ap;
  va_start (ap, rettype);
  for (int i = 0; i < nargs; ++i)
    {
      types[i] = va_arg (ap, tree);
      tree arg = va_arg (ap, tree);
      args[i] = build_reinterpret_cast (types[i], arg);
      if (types[i] == error_mark_node || args[i] == error_mark_node)
	{
	  delete[] types;
	  delete[] args;
	  return error_mark_node;
	}
    }
  va_end (ap);

  tree decl = NULL_TREE;
  if (pdecl == NULL || *pdecl == NULL_TREE)
    {
      builtin_index::const_iterator i = builtin_cache_.find (name);
      if (i != builtin_cache_.end ())
	decl = (*i).second;
    }
  else
    decl = *pdecl;

  if (decl == NULL_TREE)
    {
      tree fnid = get_identifier (name);
      tree argtypes = NULL_TREE;
      tree *pp = &argtypes;
      for (int i = 0; i < nargs; ++i)
	{
	  *pp = tree_cons (NULL_TREE, types[i], NULL_TREE);
	  pp = &TREE_CHAIN (*pp);
	}
      *pp = void_list_node;

      tree fntype = build_function_type (rettype, argtypes);

      decl = build_decl (UNKNOWN_LOCATION, FUNCTION_DECL, fnid, fntype);

      TREE_STATIC (decl) = 0;
      DECL_EXTERNAL (decl) = 1;
      TREE_PUBLIC (decl) = 1;
    }

  tree fnptr = build_fold_addr_expr (decl);

  tree ret = build_call_array (rettype, fnptr, nargs, args);

  if (name != NULL)
    builtin_cache_[name] = decl;
  if (pdecl != NULL)
    *pdecl = decl;

  delete[] types;
  delete[] args;

  return ret;
}

void
brig_to_generic::dump_function (brig_function *f)
{
  /* Dump the BRIG-specific tree IR.  */
  if (m_dump_file)
    {
      fprintf (m_dump_file, "\n;; Function %s", f->m_name.c_str ());
      fprintf (m_dump_file, "\n;; enabled by -%s\n\n",
	       dump_flag_name (TDI_original));
      print_generic_decl (m_dump_file, f->m_func_decl, 0);
      print_generic_expr (m_dump_file, f->m_current_bind_expr, 0);
      fprintf (m_dump_file, "\n");
    }
}

void
brig_to_generic::write_globals ()
{
  // Now that the whole BRIG module has been processed, build a launcher
  // and a metadata section for each built kernel.
  for (size_t i = 0; i < m_kernels.size (); ++i)
    {

      brig_function *f = m_kernels[i];

      // Finish kernels now that we know the call graphs and their
      // barrier usage.
      f->finish_kernel ();

      dump_function (f);
      gimplify_function_tree (f->m_func_decl);
      cgraph_node::finalize_function (f->m_func_decl, true);

      // TODO: analyze the kernel's actual group and private segment usage
      // using a call graph.  Now this is overly pessimistic.
      tree launcher = f->build_launcher_and_metadata (group_segment_size (),
						      private_segment_size ());

      append_global (launcher);

      gimplify_function_tree (launcher);
      cgraph_node::finalize_function (launcher, true);
      pop_cfun ();
    }

  int no_globals = list_length (m_globals);
  tree *vec = new tree[no_globals];

  int i = 0;
  tree global = m_globals;
  while (global)
    {
      vec[i] = global;
      ++i;
      global = TREE_CHAIN (global);
    }

  wrapup_global_declarations (vec, no_globals);

  delete[] vec;

  for (size_t i = 0; i < m_brig_blobs.size (); ++i)
    delete m_brig_blobs[i];
}
