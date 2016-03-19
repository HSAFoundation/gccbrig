/* brig-label-handler.cc -- brig label directive handling
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

size_t
brig_directive_label_handler::operator() (const BrigBase *base)
{
  const BrigDirectiveLabel *brig_label = (const BrigDirectiveLabel*) base;

  const BrigData *label_name = parent_.get_brig_data_entry (brig_label->name);

  std::string label_str ((const char*) (label_name->bytes),
												 label_name->byteCount);

  parent_.m_cf->append_statement
    (build_stmt (LABEL_EXPR, parent_.m_cf->label (label_str)));
  return base->byteCount;
}
