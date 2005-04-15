// -*- C++ -*-

// Testing character type and state type with char_traits and codecvt
// specializations for the C++ library testsuite.
//
// Copyright (C) 2003, 2005 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.
//
// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#ifndef _GLIBCXX_TESTSUITE_CHARACTER_H
#define _GLIBCXX_TESTSUITE_CHARACTER_H

#include <climits>
#include <string> // for char_traits
#include <locale> // for codecvt
#include <ext/pod_char_traits.h>

namespace __gnu_test
{  
  struct pod_int
  {
    int value;
  };
  
  inline bool
  operator==(const pod_int& lhs, const pod_int& rhs)
  { return lhs.value == rhs.value; }
  
  inline bool
  operator<(const pod_int& lhs, const pod_int& rhs)
  { return lhs.value < rhs.value; }

  struct pod_state
  {
    unsigned long value;
  };

  inline bool
  operator==(const pod_state& lhs, const pod_state& rhs)
  { return lhs.value == rhs.value; }

  inline bool
  operator<(const pod_state& lhs, const pod_state& rhs)
  { return lhs.value < rhs.value; }

  // Alternate character types.
  using __gnu_cxx::character;
  typedef character<unsigned char, pod_int, pod_state>  	pod_char;
  typedef character<unsigned char, unsigned int, pod_state>  	pod_uchar;
  typedef character<unsigned short, unsigned int>	   	pod_ushort;

  // Specializations.
  // pod_char
  template<>
    template<typename V2>
      inline pod_char::char_type
      pod_char::char_type::from(const V2& v)
      {
	char_type ret = { static_cast<value_type>(v.value) };
	return ret;
      }

  template<>
    template<typename V2>
      inline V2
      pod_char::char_type::to(const char_type& c)
      {
	V2 ret = { c.value };
	return ret;
      }
  
  // pod_uchar
  template<>
    template<typename V2>
      inline pod_uchar::char_type
      pod_uchar::char_type::from(const V2& v)
      {
	char_type ret;
	ret.value = (v >> 5);
	return ret;
      }

  template<>
    template<typename V2>
      inline V2
      pod_uchar::char_type::to(const char_type& c)
      { return static_cast<V2>(c.value << 5); }
}; // namespace __gnu_test

namespace std
{
  // codecvt specialization
  //
  // The conversion performed by the specialization is not supposed to
  // be useful, rather it has been designed to demonstrate the
  // essential features of stateful conversions:
  // * Number and value of bytes for each internal character depends on the
  //   state in addition to the character itself.
  // * Unshift produces an unshift sequence and resets the state. On input
  //   the unshift sequence causes the state to be reset.
  //
  // The conversion for output is as follows:
  // 1. Calculate the value tmp by xor-ing the state and the internal
  //    character
  // 2. Split tmp into either two or three bytes depending on the value of
  //    state. Output those bytes.
  // 3. tmp becomes the new value of state.
  template<>
    class codecvt<__gnu_test::pod_uchar, char, __gnu_test::pod_state>
    : public __codecvt_abstract_base<__gnu_test::pod_uchar, char, 
				     __gnu_test::pod_state>
    {
    public:
      typedef codecvt_base::result	result;
      typedef __gnu_test::pod_uchar 	intern_type;
      typedef char 			extern_type;
      typedef __gnu_test::pod_state 	state_type;
      typedef __codecvt_abstract_base<intern_type, extern_type, state_type>
      base_type;

      explicit codecvt(size_t refs = 0) : base_type(refs)
      { }

      static locale::id id;

    protected:
      ~codecvt()
      { }

      virtual result
      do_out(state_type& state, const intern_type* from,
	     const intern_type* from_end, const intern_type*& from_next,
	     extern_type* to, extern_type* to_limit,
	     extern_type*& to_next) const
      {
	while (from < from_end && to < to_limit)
	  {
	    unsigned char tmp = (state.value ^ from->value);
	    if (state.value & 0x8)
	      {
		if (to >= to_limit - 2)
		  break;
		*to++ = (tmp & 0x7);
		*to++ = ((tmp >> 3) & 0x7);
		*to++ = ((tmp >> 6) & 0x3);
	      }
	    else
	      {
		if (to >= to_limit - 1)
		  break;
		*to++ = (tmp & 0xf);
		*to++ = ((tmp >> 4) & 0xf);
	      }
	    state.value = tmp;
	    ++from;
	  }

	from_next = from;
	to_next = to;
	return (from < from_end) ? partial : ok;
      }

      virtual result
      do_in(state_type& state, const extern_type* from,
	    const extern_type* from_end, const extern_type*& from_next,
	    intern_type* to, intern_type* to_limit,
	    intern_type*& to_next) const
      {
	while (from < from_end && to < to_limit)
	  {
	    unsigned char c = *from;
	    if (c & 0xc0)
	      {
		// Unshift sequence
		state.value &= c;
		++from;
		continue;
	      }

	    unsigned char tmp;
	    if (state.value & 0x8)
	      {
		if (from >= from_end - 2)
		  break;
		tmp = (*from++ & 0x7);
		tmp |= ((*from++ << 3) & 0x38);
		tmp |= ((*from++ << 6) & 0xc0);
	      }
	    else
	      {
		if (from >= from_end - 1)
		  break;
		tmp = (*from++ & 0xf);
		tmp |= ((*from++ << 4) & 0xf0);
	      }
	    to->value = (tmp ^ state.value);
	    state.value = tmp;
	    ++to;
	  }

	from_next = from;
	to_next = to;
	return (from < from_end) ? partial : ok;
      }

      virtual result
      do_unshift(state_type& state, extern_type* to, extern_type* to_limit,
		 extern_type*& to_next) const
      {
	for (unsigned int i = 0; i < CHAR_BIT; ++i)
	  {
	    unsigned int mask = (1 << i);
	    if (state.value & mask)
	      {
		if (to == to_limit)
		  {
		    to_next = to;
		    return partial;
		  }

		state.value &= ~mask;
		*to++ = static_cast<unsigned char>(~mask);
	      }
	  }

	to_next = to;
	return state.value == 0 ? ok : error;
      }

      virtual int
      do_encoding() const throw()
      { return -1; }

      virtual bool
      do_always_noconv() const throw()
      { return false; }

      virtual int
      do_length(state_type& state, const extern_type* from,
		const extern_type* end, size_t max) const
      {
	const extern_type* beg = from;
	while (from < end && max)
	  {
	    unsigned char c = *from;
	    if (c & 0xc0)
	      {
		// Unshift sequence
		state.value &= c;
		++from;
		continue;
	      }

	    unsigned char tmp;
	    if (state.value & 0x8)
	      {
		if (from >= end - 2)
		  break;
		tmp = (*from++ & 0x7);
		tmp |= ((*from++ << 3) & 0x38);
		tmp |= ((*from++ << 6) & 0xc0);
	      }
	    else
	      {
		if (from >= end - 1)
		  break;
		tmp = (*from++ & 0xf);
		tmp |= ((*from++ << 4) & 0xf0);
	      }
	    state.value = tmp;
	    --max;
	  }
	return from - beg;
      }

      // Maximum 8 bytes unshift sequence followed by max 3 bytes for
      // one character.
      virtual int
      do_max_length() const throw()
      { return 11; }
    };

  template<>
    class ctype<__gnu_test::pod_uchar>
    : public __ctype_abstract_base<__gnu_test::pod_uchar>
    {
    public:
      typedef __gnu_test::pod_uchar char_type;

      explicit ctype(size_t refs  = 0)
      : __ctype_abstract_base<__gnu_test::pod_uchar>(refs) { }

      static locale::id id;

    protected:
      ~ctype()
      { }

      virtual bool
      do_is(mask m, char_type c) const
      { return false; }

      virtual const char_type*
      do_is(const char_type* low, const char_type* high, mask* vec) const
      {
	fill_n(vec, high - low, mask());
	return high;
      }

      virtual const char_type*
      do_scan_is(mask m, const char_type* low, const char_type* high) const
      { return high; }

      virtual const char_type*
      do_scan_not(mask m, const char_type* low, const char_type* high) const
      { return low; }

      virtual char_type
      do_toupper(char_type c) const
      { return c; }

      virtual const char_type*
      do_toupper(char_type*  low, const char_type*  high) const
      { return high; }

      virtual char_type
      do_tolower(char_type c) const
      { return c; }

      virtual const char_type*
      do_tolower(char_type*  low, const char_type*  high) const
      { return high; }

      virtual char_type
      do_widen(char c) const
      { return __gnu_test::pod_uchar::from<char>(c); }

      virtual const char* 
      do_widen(const char* low, const char* high, char_type* dest) const
      {
	transform(low, high, dest, &__gnu_test::pod_uchar::from<char>);
	return high;
      }

      virtual char
      do_narrow(char_type, char dfault) const
      { return dfault; }

      virtual const char_type*
      do_narrow(const char_type* low, const char_type* high,
		char dfault, char*  dest) const
      {
	fill_n(dest, high - low, dfault);
	return high;
      }
    };
} // namespace std

#endif // _GLIBCXX_TESTSUITE_CHARACTER_H

