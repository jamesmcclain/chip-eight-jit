#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRYPOINT 0x200
#define MEMORY_SIZE 0x1000
#define MAX_SYMBOLS 2048	/* enough for a label on every word in a 4 KiB ROM */
#define MAX_TOKENS 260
#define MAX_LINE 1024

struct symbol
{
  char name[128];
  unsigned value;
};
static struct symbol symbols[MAX_SYMBOLS];
static size_t symbol_count;
static const char *filename;
static unsigned line_number;

static void
fail (const char *fmt, ...)
{
  va_list ap;
  fprintf (stderr, "%s:%u: error: ", filename, line_number);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
normalize (char *s)
{
  for (; *s; ++s)
    *s = (char) toupper ((unsigned char) *s);
}

static int
tokenize (char *line, char *tokens[])
{
  int count = 0;
  for (char *p = line; *p;)
    {
      if (*p == ';' || *p == '#')
	break;
      if (isspace ((unsigned char) *p) || *p == ',')
	{
	  ++p;
	  continue;
	}
      if (count == MAX_TOKENS)
	fail ("too many tokens");
      tokens[count++] = p;
      while (*p && !isspace ((unsigned char) *p) && *p != ',' && *p != ';' && *p != '#')
	++p;
      if (*p)
	*p++ = '\0';
    }
  for (int i = 0; i < count; ++i)
    normalize (tokens[i]);
  return count;
}

static bool
is_name (const char *s)
{
  if (!isalpha ((unsigned char) *s) && *s != '_')
    return false;
  for (++s; *s; ++s)
    if (!isalnum ((unsigned char) *s) && *s != '_')
      return false;
  return true;
}

static struct symbol *
lookup (const char *name)
{
  for (size_t i = 0; i < symbol_count; ++i)
    if (!strcmp (symbols[i].name, name))
      return &symbols[i];
  return NULL;
}

static void
define (const char *name, unsigned value)
{
  if (!is_name (name))
    fail ("invalid label '%s'", name);
  if (lookup (name))
    fail ("duplicate label '%s'", name);
  if (symbol_count == MAX_SYMBOLS)
    fail ("too many labels");
  strcpy (symbols[symbol_count].name, name);
  symbols[symbol_count++].value = value;
}

static unsigned
value (const char *s, bool resolve)
{
  char *end;
  unsigned long n;
  errno = 0;
  n = strtoul (s, &end, 0);
  if (*s && !*end && !errno)
    {
      if (n > 0xffff)
	fail ("number '%s' is too large", s);
      return (unsigned) n;
    }
  struct symbol *symbol = lookup (s);
  if (symbol)
    return symbol->value;
  if (resolve)
    fail ("undefined symbol '%s'", s);
  return 0;
}

static unsigned
reg (const char *s)
{
  if (s[0] != 'V' || !isxdigit ((unsigned char) s[1]) || s[2])
    fail ("expected V0 through VF, got '%s'", s);
  unsigned r = (unsigned) (isdigit ((unsigned char) s[1]) ? s[1] - '0' : s[1] - 'A' + 10);
  if (r > 15)
    fail ("expected V0 through VF, got '%s'", s);
  return r;
}

static void
range (unsigned n, unsigned max, const char *what)
{
  if (n > max)
    fail ("%s out of range: 0x%X", what, n);
}

static unsigned
instruction (char *t[], int n, bool resolve)
{
  unsigned x, y, z;
#define NEED(k) do { if (n != (k)) fail("wrong operand count for %s", t[0]); } while (0)
#define OP(a) (!strcmp(t[0], (a)))
  if (OP ("CLS"))
    {
      NEED (1);
      return 0x00e0;
    }
  if (OP ("RET"))
    {
      NEED (1);
      return 0x00ee;
    }
  if (OP ("JP"))
    {
      if (n == 2)
	{
	  z = value (t[1], resolve);
	  range (z, 0xfff, "address");
	  return 0x1000 | z;
	}
      NEED (3);
      if (strcmp (t[1], "V0"))
	fail ("JP offset form requires V0");
      z = value (t[2], resolve);
      range (z, 0xfff, "address");
      return 0xb000 | z;
    }
  if (OP ("CALL"))
    {
      NEED (2);
      z = value (t[1], resolve);
      range (z, 0xfff, "address");
      return 0x2000 | z;
    }
  if (OP ("SE") || OP ("SNE"))
    {
      NEED (3);
      x = reg (t[1]);
      if (t[2][0] == 'V')
	{
	  y = reg (t[2]);
	  return (OP ("SE") ? 0x5000 : 0x9000) | (x << 8) | (y << 4);
	}
      z = value (t[2], resolve);
      range (z, 0xff, "byte");
      return (OP ("SE") ? 0x3000 : 0x4000) | (x << 8) | z;
    }
  if (OP ("LD"))
    {
      NEED (3);
      if (!strcmp (t[1], "I"))
	{
	  z = value (t[2], resolve);
	  range (z, 0xfff, "address");
	  return 0xa000 | z;
	}
      if (!strcmp (t[1], "DT"))
	return 0xf015 | (reg (t[2]) << 8);
      if (!strcmp (t[1], "ST"))
	return 0xf018 | (reg (t[2]) << 8);
      if (!strcmp (t[1], "F"))
	return 0xf029 | (reg (t[2]) << 8);
      if (!strcmp (t[1], "B"))
	return 0xf033 | (reg (t[2]) << 8);
      if (!strcmp (t[1], "[I]"))
	return 0xf055 | (reg (t[2]) << 8);
      x = reg (t[1]);
      if (!strcmp (t[2], "DT"))
	return 0xf007 | (x << 8);
      if (!strcmp (t[2], "K"))
	return 0xf00a | (x << 8);
      if (!strcmp (t[2], "[I]"))
	return 0xf065 | (x << 8);
      if (t[2][0] == 'V')
	return 0x8000 | (x << 8) | (reg (t[2]) << 4);
      z = value (t[2], resolve);
      range (z, 0xff, "byte");
      return 0x6000 | (x << 8) | z;
    }
  if (OP ("ADD"))
    {
      NEED (3);
      if (!strcmp (t[1], "I"))
	return 0xf01e | (reg (t[2]) << 8);
      x = reg (t[1]);
      if (t[2][0] == 'V')
	return 0x8004 | (x << 8) | (reg (t[2]) << 4);
      z = value (t[2], resolve);
      range (z, 0xff, "byte");
      return 0x7000 | (x << 8) | z;
    }
  if (OP ("OR") || OP ("AND") || OP ("XOR") || OP ("SUB") || OP ("SUBN"))
    {
      NEED (3);
      x = reg (t[1]);
      y = reg (t[2]);
      unsigned low = OP ("OR") ? 1 : OP ("AND") ? 2 : OP ("XOR") ? 3 : OP ("SUB") ? 5 : 7;
      return 0x8000 | (x << 8) | (y << 4) | low;
    }
  if (OP ("SHR") || OP ("SHL"))
    {
      if (n != 2 && n != 3)
	fail ("wrong operand count for %s", t[0]);
      x = reg (t[1]);
      y = n == 3 ? reg (t[2]) : 0;
      return 0x8000 | (x << 8) | (y << 4) | (OP ("SHR") ? 6 : 0xe);
    }
  if (OP ("RND"))
    {
      NEED (3);
      x = reg (t[1]);
      z = value (t[2], resolve);
      range (z, 0xff, "byte");
      return 0xc000 | (x << 8) | z;
    }
  if (OP ("DRW"))
    {
      NEED (4);
      x = reg (t[1]);
      y = reg (t[2]);
      z = value (t[3], resolve);
      range (z, 0xf, "nibble");
      return 0xd000 | (x << 8) | (y << 4) | z;
    }
  if (OP ("SKP") || OP ("SKNP"))
    {
      NEED (2);
      return (OP ("SKP") ? 0xe09e : 0xe0a1) | (reg (t[1]) << 8);
    }
  fail ("unknown instruction '%s'", t[0]);
  return 0;			/* fail exits; keeps compilers without noreturn annotations happy. */
#undef NEED
#undef OP
}

static unsigned
line_size (char *t[], int n, unsigned pc, bool resolve, uint8_t *out)
{
  if (!n)
    return 0;
  if (!strcmp (t[0], ".ORG"))
    {
      if (n != 2)
	fail (".ORG needs one address");
      unsigned p = value (t[1], resolve);
      if (p < ENTRYPOINT || p >= MEMORY_SIZE)
	fail (".ORG must be from 0x200 through 0xFFF");
      if (p < pc)
	fail (".ORG cannot move backwards");
      return p - pc;
    }
  if (!strcmp (t[0], ".BYTE"))
    {
      if (n < 2)
	fail (".BYTE needs at least one value");
      for (int i = 1; i < n; ++i)
	{
	  unsigned v = value (t[i], resolve);
	  range (v, 0xff, "byte");
	  if (out)
	    out[i - 1] = (uint8_t) v;
	}
      return n - 1;
    }
  if (!strcmp (t[0], ".WORD"))
    {
      if (n < 2)
	fail (".WORD needs at least one value");
      for (int i = 1; i < n; ++i)
	{
	  unsigned v = value (t[i], resolve);
	  range (v, 0xffff, "word");
	  if (out)
	    {
	      out[2 * (i - 1)] = v >> 8;
	      out[2 * (i - 1) + 1] = v;
	    }
	}
      return 2 * (n - 1);
    }
  unsigned op = instruction (t, n, resolve);
  if (out)
    {
      out[0] = op >> 8;
      out[1] = op;
    }
  return 2;
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "Usage: %s <source.asm> > rom.ch8\n", argv[0]);
      return 2;
    }
  filename = argv[1];
  FILE *fp = fopen (filename, "r");
  if (!fp)
    {
      perror (filename);
      return 1;
    }
  char line[MAX_LINE], copy[MAX_LINE], *t[MAX_TOKENS];
  unsigned pc = ENTRYPOINT;
  while (fgets (line, sizeof line, fp))
    {
      ++line_number;
      strcpy (copy, line);
      int n = tokenize (copy, t);
      if (n && t[0][strlen (t[0]) - 1] == ':')
	{
	  t[0][strlen (t[0]) - 1] = 0;
	  define (t[0], pc);
	  memmove (t, t + 1, --n * sizeof *t);
	}
      pc += line_size (t, n, pc, false, NULL);
      if (pc > MEMORY_SIZE)
	fail ("program exceeds 4 KiB CHIP-8 memory");
    }
  if (ferror (fp))
    {
      perror (filename);
      return 1;
    }
  rewind (fp);
  line_number = 0;
  pc = ENTRYPOINT;
  while (fgets (line, sizeof line, fp))
    {
      ++line_number;
      int n = tokenize (line, t);
      if (n && t[0][strlen (t[0]) - 1] == ':')
	memmove (t, t + 1, --n * sizeof *t);
      uint8_t bytes[MAX_TOKENS * 2];
      unsigned size = line_size (t, n, pc, true, bytes);
      if (n && !strcmp (t[0], ".ORG"))
	for (unsigned i = 0; i < size; ++i)
	  putchar (0);
      else
	for (unsigned i = 0; i < size; ++i)
	  putchar (bytes[i]);
      pc += size;
    }
  fclose (fp);
  return ferror (stdout) ? 1 : 0;
}
