/* PR 35503 - Warn about restricted pointers
   Test to exercise that -Wrestrict warnings are issued for memory and
   sring functions when they are declared in system headers (i.e., not
   just when they are explicitly declared in the source file.)
   Also verify that the warnings are issued even for calls where the
   source of the aliasing violation is in a different function than
   the restricted call.
   { dg-do compile }
   { dg-options "-O2 -Wrestrict" } */

#include <string.h>

void wrap_memcpy (void *d, const void *s, size_t n)
{
  memcpy (d, s, n);   /* { dg-warning "source argument is the same as destination" "memcpy" } */
}

void call_memcpy (void *d, size_t n)
{
  const void *s = d;
  wrap_memcpy (d, s, n);
}


void wrap_strcat (char *d, const char *s)
{
  strcat (d, s);   /* { dg-warning "source argument is the same as destination" "strcat" } */
}

void call_strcat (char *d)
{
  const char *s = d;
  wrap_strcat (d, s);
}


void wrap_strcpy (char *d, const char *s)
{
  strcpy (d, s);   /* { dg-warning "source argument is the same as destination" "strcpy" } */
}

void call_strcpy (char *d)
{
  const char *s = d;
  wrap_strcpy (d, s);
}


void wrap_strncat (char *d, const char *s, size_t n)
{
  strncat (d, s, n);   /* { dg-warning "source argument is the same as destination" "strncat" } */
}

void call_strncat (char *d, size_t n)
{
  const char *s = d;
  wrap_strncat (d, s, n);
}


void wrap_strncpy (char *d, const char *s, size_t n)
{
  strncpy (d, s, n);   /* { dg-warning "source argument is the same as destination" "strncpy" } */
}

void call_strncpy (char *d, size_t n)
{
  const char *s = d;
  wrap_strncpy (d, s, n);
}
