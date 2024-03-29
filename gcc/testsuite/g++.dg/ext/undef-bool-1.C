/* { dg-do compile { target { powerpc*-*-* } } } */
/* { dg-options "-O2 -DNO_WARN_X86_INTRINSICS -mvsx" } */

/* Test to ensure that "bool" gets undef'd in xmmintrin.h when
   we require strict ANSI.  */

#include <xmmintrin.h>

bool foo (int x)
{
  return x == 2;
}

