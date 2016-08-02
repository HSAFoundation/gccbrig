/* { dg-do compile } */
/* { dg-options "-O2 -ffast-math -fdump-tree-forwprop-details" } */

#include <math.h>
double f(float f)
{
  double t1 = fabs(f); 
  double t2 = f / t1;
  return t2;
}

/* { dg-final { scan-tree-dump "__builtin_copysign" "forwprop1" } } */
