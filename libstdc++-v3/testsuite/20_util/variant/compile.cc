// { dg-options "-std=gnu++17" }
// { dg-do compile }

// Copyright (C) 2016 Free Software Foundation, Inc.
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

#include <variant>
#include <string>
#include <vector>

using namespace std;

struct AllDeleted
{
  AllDeleted() = delete;
  AllDeleted(const AllDeleted&) = delete;
  AllDeleted(AllDeleted&&) = delete;
  AllDeleted& operator=(const AllDeleted&) = delete;
  AllDeleted& operator=(AllDeleted&&) = delete;
};

struct Empty
{
  Empty() { };
  Empty(const Empty&) { };
  Empty(Empty&&) { };
  Empty& operator=(const Empty&) { return *this; };
  Empty& operator=(Empty&&) { return *this; };
};

struct DefaultNoexcept
{
  DefaultNoexcept() noexcept = default;
  DefaultNoexcept(const DefaultNoexcept&) noexcept = default;
  DefaultNoexcept(DefaultNoexcept&&) noexcept = default;
  DefaultNoexcept& operator=(const DefaultNoexcept&) noexcept = default;
  DefaultNoexcept& operator=(DefaultNoexcept&&) noexcept = default;
};

void default_ctor()
{
  static_assert(is_default_constructible_v<variant<int, string>>, "");
  static_assert(is_default_constructible_v<variant<string, string>>, "");
  static_assert(!is_default_constructible_v<variant<AllDeleted, string>>, "");
  static_assert(is_default_constructible_v<variant<string, AllDeleted>>, "");

  static_assert(noexcept(variant<int>()), "");
  static_assert(!noexcept(variant<Empty>()), "");
  static_assert(noexcept(variant<DefaultNoexcept>()), "");
}

void copy_ctor()
{
  static_assert(is_copy_constructible_v<variant<int, string>>, "");
  static_assert(!is_copy_constructible_v<variant<AllDeleted, string>>, "");

  {
    variant<int> a;
    static_assert(!noexcept(variant<int>(a)), "");
  }
  {
    variant<string> a;
    static_assert(!noexcept(variant<string>(a)), "");
  }
  {
    variant<int, string> a;
    static_assert(!noexcept(variant<int, string>(a)), "");
  }
  {
    variant<int, char> a;
    static_assert(!noexcept(variant<int, char>(a)), "");
  }
}

void move_ctor()
{
  static_assert(is_move_constructible_v<variant<int, string>>, "");
  static_assert(!is_move_constructible_v<variant<AllDeleted, string>>, "");
  static_assert(!noexcept(variant<int, Empty>(declval<variant<int, Empty>>())), "");
  static_assert(noexcept(variant<int, DefaultNoexcept>(declval<variant<int, DefaultNoexcept>>())), "");
}

void arbitrary_ctor()
{
  static_assert(!is_constructible_v<variant<string, string>, const char*>, "");
  static_assert(is_constructible_v<variant<int, string>, const char*>, "");
  static_assert(noexcept(variant<int, Empty>(int{})), "");
  static_assert(noexcept(variant<int, DefaultNoexcept>(int{})), "");
  static_assert(!noexcept(variant<int, Empty>(Empty{})), "");
  static_assert(noexcept(variant<int, DefaultNoexcept>(DefaultNoexcept{})), "");
}

void in_place_index_ctor()
{
  variant<string, string> a(in_place_index<0>, "a");
  variant<string, string> b(in_place_index<1>, {'a'});
}

void in_place_type_ctor()
{
  variant<int, string, int> a(in_place_type<string>, "a");
  variant<int, string, int> b(in_place_type<string>, {'a'});
  static_assert(!is_constructible_v<variant<string, string>, in_place_type_t<string>, const char*>, "");
}

void uses_alloc_ctors()
{
  std::allocator<char> alloc;
  variant<int> a(allocator_arg, alloc);
  static_assert(!is_constructible_v<variant<AllDeleted>, allocator_arg_t, std::allocator<char>>, "");
  {
    variant<string, int> b(allocator_arg, alloc, "a");
    static_assert(!is_constructible_v<variant<string, string>, allocator_arg_t, std::allocator<char>, const char*>, "");
  }
  {
    variant<string, int> b(allocator_arg, alloc, in_place_index<0>, "a");
    variant<string, string> c(allocator_arg, alloc, in_place_index<1>, "a");
  }
  {
    variant<string, int> b(allocator_arg, alloc, in_place_index<0>, {'a'});
    variant<string, string> c(allocator_arg, alloc, in_place_index<1>, {'a'});
  }
  {
    variant<int, string, int> b(allocator_arg, alloc, in_place_type<string>, "a");
  }
  {
    variant<int, string, int> b(allocator_arg, alloc, in_place_type<string>, {'a'});
  }
}

void dtor()
{
  static_assert(is_destructible_v<variant<int, string>>, "");
  static_assert(is_destructible_v<variant<AllDeleted, string>>, "");
}

void copy_assign()
{
  static_assert(is_copy_assignable_v<variant<int, string>>, "");
  static_assert(!is_copy_assignable_v<variant<AllDeleted, string>>, "");
  {
    variant<Empty> a;
    static_assert(!noexcept(a = a), "");
  }
  {
    variant<DefaultNoexcept> a;
    static_assert(!noexcept(a = a), "");
  }
}

void move_assign()
{
  static_assert(is_move_assignable_v<variant<int, string>>, "");
  static_assert(!is_move_assignable_v<variant<AllDeleted, string>>, "");
  {
    variant<Empty> a;
    static_assert(!noexcept(a = std::move(a)), "");
  }
  {
    variant<DefaultNoexcept> a;
    static_assert(noexcept(a = std::move(a)), "");
  }
}

void arbitrary_assign()
{
  static_assert(!is_assignable_v<variant<string, string>, const char*>, "");
  static_assert(is_assignable_v<variant<int, string>, const char*>, "");
  static_assert(noexcept(variant<int, Empty>() = int{}), "");
  static_assert(noexcept(variant<int, DefaultNoexcept>() = int{}), "");
  static_assert(!noexcept(variant<int, Empty>() = Empty{}), "");
  static_assert(noexcept(variant<int, DefaultNoexcept>() = DefaultNoexcept{}), "");
}

void test_get()
{
  static_assert(is_same<decltype(get<0>(variant<int, string>())), int&&>::value, "");
  static_assert(is_same<decltype(get<1>(variant<int, string>())), string&&>::value, "");
  static_assert(is_same<decltype(get<1>(variant<int, const string>())), const string&&>::value, "");

  static_assert(is_same<decltype(get<int>(variant<int, string>())), int&&>::value, "");
  static_assert(is_same<decltype(get<string>(variant<int, string>())), string&&>::value, "");
  static_assert(is_same<decltype(get<const string>(variant<int, const string>())), const string&&>::value, "");
}

void test_relational()
{
  {
    const variant<int, string> a, b;
    (void)(a < b);
    (void)(a > b);
    (void)(a <= b);
    (void)(a == b);
    (void)(a != b);
    (void)(a >= b);
  }
  {
    const monostate a, b;
    (void)(a < b);
    (void)(a > b);
    (void)(a <= b);
    (void)(a == b);
    (void)(a != b);
    (void)(a >= b);
  }
}

void test_swap()
{
  variant<int, string> a, b;
  a.swap(b);
  swap(a, b);
}

void test_visit()
{
  {
    struct Visitor
    {
      void operator()(monostate) {}
      void operator()(const int&) {}
    };
    struct CVisitor
    {
      void operator()(monostate) const {}
      void operator()(const int&) const {}
    };
  }
  {
    struct Visitor
    {
      bool operator()(int, float) { return false; }
      bool operator()(int, double) { return false; }
      bool operator()(char, float) { return false; }
      bool operator()(char, double) { return false; }
    };
    visit(Visitor(), variant<int, char>(), variant<float, double>());
  }
}

void test_constexpr()
{
  constexpr variant<int> a;
  static_assert(holds_alternative<int>(a), "");
  constexpr variant<int, char> b(in_place_index<0>, int{});
  static_assert(holds_alternative<int>(b), "");
  constexpr variant<int, char> c(in_place_type<int>, int{});
  static_assert(holds_alternative<int>(c), "");
  constexpr variant<int, char> d(in_place_index<1>, char{});
  static_assert(holds_alternative<char>(d), "");
  constexpr variant<int, char> e(in_place_type<char>, char{});
  static_assert(holds_alternative<char>(e), "");
  constexpr variant<int, char> f(char{});
  static_assert(holds_alternative<char>(f), "");

  {
    struct literal {
	constexpr literal() = default;
    };

    struct nonliteral {
	nonliteral() { }
    };

    constexpr variant<literal, nonliteral> v{};
    constexpr variant<literal, nonliteral> v1{in_place_type<literal>};
    constexpr variant<literal, nonliteral> v2{in_place_index<0>};
  }
}

void test_pr77641()
{
  struct X {
    constexpr X() { }
  };

  constexpr std::variant<X> v1 = X{};
}

namespace adl_trap
{
  struct X {
    X() = default;
    X(int) { }
    X(std::initializer_list<int>, const X&) { }
  };
  template<typename T> void move(T&) { }
  template<typename T> void forward(T&) { }

  struct Visitor {
    template<typename T> void operator()(T&&) { }
  };
}

void test_adl()
{
   using adl_trap::X;
   using std::allocator_arg;
   X x;
   std::allocator<int> a;
   std::initializer_list<int> il;
   adl_trap::Visitor vis;

   std::variant<X> v0(x);
   v0 = x;
   v0.emplace<0>(x);
   v0.emplace<0>(il, x);
   visit(vis, v0);
   variant<X> v1{in_place_index<0>, x};
   variant<X> v2{in_place_type<X>, x};
   variant<X> v3{in_place_index<0>, il, x};
   variant<X> v4{in_place_type<X>, il, x};
   variant<X> v5{allocator_arg, a, in_place_index<0>, x};
   variant<X> v6{allocator_arg, a, in_place_type<X>, x};
   variant<X> v7{allocator_arg, a, in_place_index<0>, il, x};
   variant<X> v8{allocator_arg, a, in_place_type<X>, il, x};
   variant<X> v9{allocator_arg, a, in_place_type<X>, 1};
}
