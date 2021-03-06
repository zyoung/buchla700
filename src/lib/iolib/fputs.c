/*
   =============================================================================
	fputs.c -- output a string to a stream
	Version 3 -- 1987-07-09 -- D.N. Lynx Crowe
   =============================================================================
*/

#include "stdio.h"
#include "stddefs.h"

int
puts (str)
     register char *str;
{
  while (*str)
    if (putchar (*str++) == EOF)
      return (EOF);

  return (putchar ('\n'));
}

int
aputc (c, ptr)
     register int c;
     register FILE *ptr;
{
  c &= 127;

  if (c == '\n')
    if (putc ('\r', ptr) == EOF)
      return (EOF);

  return (putc (c, ptr));
}

int
fputs (s, fp)
     register char *s;
     FILE *fp;
{
  while (*s)
    if (aputc (*s++, fp) == EOF)
      return (EOF);
  return (0);
}
