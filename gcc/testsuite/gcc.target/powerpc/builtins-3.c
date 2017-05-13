/* { dg-do compile } */
/* { dg-require-effective-target powerpc_vsx_ok } */
/* { dg-options "-maltivec -mvsx" } */

#include <altivec.h>

vector bool char
test_eq_char (vector bool char x, vector bool char y)
{
	return vec_cmpeq (x, y);
}

vector bool short
test_eq_short (vector bool short x, vector bool short y)
{
	return vec_cmpeq (x, y);
}

vector bool int
test_eq_int (vector bool int x, vector bool int y)
{
	return vec_cmpeq (x, y);
}

vector double
test_shift_left_double (vector double x, vector double y)
{
	return vec_sld (x, y, /* shift_by */ 10);
}

vector signed char
test_nabs_char (vector signed char x)
{
	return vec_nabs (x);
}

vector short
test_nabs_short (vector short x)
{
  return vec_nabs (x);
}

vector int
test_nabs_int (vector int x)
{
  return vec_nabs (x);
}

vector float
test_nabs_float (vector float x)
{
  return vec_nabs (x);
}

vector double
test_nabs_double (vector double x)
{
	return vec_nabs (x);
}

vector signed char
test_neg_char (vector signed char x)
{
	return vec_neg (x);
}

vector short
test_neg_short (vector short x)
{
	return vec_neg (x);
}

vector int
test_neg_int (vector int x)
{
	return vec_neg (x);
}

vector float
test_neg_float (vector float x)
{
	return vec_neg (x);
}

vector double
test_neg_double (vector double x)
{
	return vec_neg (x);
}

vector signed long long
test_vsll_slo_vsll_vsc (vector signed long long x, vector signed char y)
{
	return vec_slo (x, y);
}

vector signed long long
test_vsll_slo_vsll_vuc (vector signed long long x, vector unsigned char y)
{
	return vec_slo (x, y);
}

vector unsigned long long
test_vull_slo_vull_vsc (vector unsigned long long x, vector signed char y)
{
	return vec_slo (x, y);
}

vector unsigned long long
test_vull_slo_vull_vuc (vector unsigned long long x, vector unsigned char y)
{
	return vec_slo (x, y);
}

/* Expected test results:

     test_eq_char              1 vcmpequb inst
     test_eq_short             1 vcmpequh inst
     test_eq_int               1 vcmpequw inst
     test_shift_left_double    1 vsldoi inst
     test_nabs_char            1 vspltisw, 1 vsububm, 1 vminsb
     test_nabs_short           1 vspltisw, 1 vsubuhm, 1 vminsh
     test_nabs_int             1 vspltisw, 1 vsubuwm, 1 vminsw
     test_nabs_float           1 xvnabssp
     test_nabs_double          1 xvnabsdp
     test_neg_char             1 vspltisw, 1 vsububm
     test_neg_short            1 vspltisw, 1 vsubuhm
     test_neg_int              1 vspltisw, 1 vsubuwm
     test_neg_float            1 xvnegsp
     test_neg_float            1 xvnegdp
     test_vsll_slo_vsll_vsc    1 vslo
     test_vsll_slo_vsll_vuc    1 vslo
     test_vull_slo_vsll_vsc    1 vslo
     test_vull_slo_vsll_vuc    1 vslo */

/* { dg-final { scan-assembler-times "vcmpequb" 1 } } */
/* { dg-final { scan-assembler-times "vcmpequh" 1 } } */
/* { dg-final { scan-assembler-times "vcmpequw" 1 } } */
/* { dg-final { scan-assembler-times "vsldoi"   1 } } */
/* { dg-final { scan-assembler-times "vsububm"  2 } } */
/* { dg-final { scan-assembler-times "vsubuhm"  2 } } */
/* { dg-final { scan-assembler-times "vsubuwm"  2 } } */
/* { dg-final { scan-assembler-times "vminsb"   1 } } */
/* { dg-final { scan-assembler-times "vminsh"   1 } } */
/* { dg-final { scan-assembler-times "vminsw"   1 } } */
/* { dg-final { scan-assembler-times "vspltisw" 6 } } */
/* { dg-final { scan-assembler-times "xvnabssp" 1 } } */
/* { dg-final { scan-assembler-times "xvnabsdp" 1 } } */
/* { dg-final { scan-assembler-times "xvnegsp"  1 } } */
/* { dg-final { scan-assembler-times "xvnegdp"  1 } } */
/* { dg-final { scan-assembler-times "vslo"     4 } } */

