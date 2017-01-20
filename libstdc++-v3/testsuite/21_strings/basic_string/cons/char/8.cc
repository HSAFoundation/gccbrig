// Copyright (C) 2016-2017 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// { dg-do run { target c++11 } }

#include <string>
#include <testsuite_hooks.h>

template<typename... Args>
std::size_t
construct(Args&&... args)
{
  return std::string( std::forward<Args>(args)... ).length();
}

void
test01()
{
  using string = std::string;
  using list = std::initializer_list<string::value_type>;

  const std::string lvalue = "lvalue";
  std::allocator<char> alloc;

  // test all valid combinations of arguments:
  VERIFY( construct( ) == 0 );
  VERIFY( construct( alloc ) == 0 );
  VERIFY( construct( lvalue ) == 6 );
  VERIFY( construct( string{"rvalue"} ) == 6 );
  VERIFY( construct( lvalue, 2 ) == 4 );
  VERIFY( construct( lvalue, 2, alloc ) == 4 );
  VERIFY( construct( lvalue, 2, 3 ) == 3 );
  VERIFY( construct( lvalue, 2, 3, alloc ) == 3 );
  VERIFY( construct( "C string", 4 ) == 4 );
  VERIFY( construct( "C string", 4, alloc ) == 4 );
  VERIFY( construct( "C string" ) == 8 );
  VERIFY( construct( "C string and alloc", alloc ) == 18 );
  VERIFY( construct( 5, ' ' ) == 5 );
  VERIFY( construct( 5, ' ', alloc ) == 5 );
  VERIFY( construct( lvalue.begin(), lvalue.end() ) == 6 );
  VERIFY( construct( lvalue.begin(), lvalue.end(), alloc ) == 6 );
  VERIFY( construct( list{ 'l' , 'i' , 's', 't' } ) == 4 );
  VERIFY( construct( list{ 'l', 'i', 's', 't' }, alloc ) == 4 );
#if _GLIBCXX_USE_CXX11_ABI
  VERIFY( construct( lvalue, alloc ) == 6 );
  VERIFY( construct( string{"rvalue"}, alloc ) == 6 );
#endif
}

int
main()
{
  test01();
}
