/* -*- mode: c; c-file-style: "k&r" -*-

  strnatcmp.c -- Perform 'natural order' comparisons of strings in C.
  Copyright (C) 2000, 2004 by Martin Pool <mbp sourcefrog net>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* partial change history:
 *
 * 2004-10-10 mbp: Lift out character type dependencies into macros.
 *
 * Eric Sosman pointed out that ctype functions take a parameter whose
 * value must be that of an unsigned int, even on platforms that have
 * negative chars in their default char type.
 */

#include <stddef.h> /* size_t */

#include "strnatcmp.h"

/* Character classification is deliberately ASCII-only and locale-independent.
 *
 * Callers pass UTF-8 (vnotex::naturalCompare feeds QString::toUtf8()), so any
 * byte >= 0x80 is part of a multi-byte sequence and must be passed through
 * untouched. The <ctype.h> functions are locale-dependent for those bytes: on a
 * CP936/GBK or Latin-1 locale, toupper() would fold UTF-8 continuation bytes and
 * isspace() would classify e.g. 0xA0 as whitespace, so two distinct CJK names
 * could compare equal or in an arbitrary order. Leaving bytes >= 0x80 alone
 * makes the byte-wise ordering equal to Unicode code point ordering, which is
 * what the previous QString::compare() gave. */
static inline int nat_isdigit(nat_char a) {
  return (unsigned char)a >= '0' && (unsigned char)a <= '9';
}

static inline int nat_isspace(nat_char a) {
  const unsigned char c = (unsigned char)a;
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static inline nat_char nat_toupper(nat_char a) {
  const unsigned char c = (unsigned char)a;
  return (c >= 'a' && c <= 'z') ? (nat_char)(c - ('a' - 'A')) : a;
}

static int compare_right(nat_char const *a, nat_char const *b) {
  int bias = 0;

  /* The longest run of digits wins.  That aside, the greatest
     value wins, but we can't know that it will until we've scanned
     both numbers to know that they have the same magnitude, so we
     remember it in BIAS. */
  for (;; a++, b++) {
    if (!nat_isdigit(*a) && !nat_isdigit(*b))
      return bias;
    if (!nat_isdigit(*a))
      return -1;
    if (!nat_isdigit(*b))
      return +1;
    if (*a < *b) {
      if (!bias)
        bias = -1;
    } else if (*a > *b) {
      if (!bias)
        bias = +1;
    } else if (!*a && !*b)
      return bias;
  }

  return 0;
}

/* Number of digits in a run whose length makes it a fixed-width field (a year,
 * a date, a zero-padded id) rather than a counter. Runs at or above this width
 * are compared left-aligned instead of by magnitude, so that e.g.
 * "report-2026" sorts after "report-20220301" the way a plain string compare
 * would. Comparing them by magnitude (2026 < 20220301) scatters same-year notes
 * across the list -- see issue #2741. Counters such as "Note 2" / "Note 10" stay
 * below the threshold and keep true numeric ordering.
 *
 * Accepted trade-off: run LENGTH alone cannot tell a date from a counter, so a
 * counter that reaches four digits also loses magnitude ordering
 * ("Note 10000" sorts before "Note 9999"). Note names in the four-digit-counter
 * range are far rarer than date-stamped ones, and unlike the issue #2741
 * scattering the result is still a stable, predictable string order. */
#define NAT_WIDE_DIGIT_RUN 4

/* Length of the run of digits starting at S. */
static size_t digit_run_len(nat_char const *s) {
  size_t n = 0;
  while (nat_isdigit(s[n]))
    ++n;
  return n;
}

static int compare_left(nat_char const *a, nat_char const *b) {
  /* Compare two left-aligned numbers: the first to have a
     different value wins. */
  for (;; a++, b++) {
    if (!nat_isdigit(*a) && !nat_isdigit(*b))
      return 0;
    if (!nat_isdigit(*a))
      return -1;
    if (!nat_isdigit(*b))
      return +1;
    if (*a < *b)
      return -1;
    if (*a > *b)
      return +1;
  }

  return 0;
}

/* Plain byte-wise compare, optionally ASCII case-folding, with NO whitespace
 * skipping and no numeric handling. Used only to break a tie left by
 * strnatcmp0(), whose whitespace skipping otherwise reports distinct names such
 * as "Report 2026" and "Report2026" as equal -- which makes the sort order
 * between them arbitrary. It applies the same case fold, so case variants
 * ("alpha" / "ALPHA") stay equivalent: this gives whitespace-distinct names a
 * deterministic order without making the relation a total order over distinct
 * byte strings. */
static int compare_bytes(nat_char const *a, nat_char const *b, int fold_case) {
  size_t i;
  for (i = 0;; ++i) {
    nat_char ca = a[i];
    nat_char cb = b[i];

    if (fold_case) {
      ca = nat_toupper(ca);
      cb = nat_toupper(cb);
    }

    if ((unsigned char)ca < (unsigned char)cb)
      return -1;
    if ((unsigned char)ca > (unsigned char)cb)
      return +1;
    if (!ca)
      return 0;
  }
}

static int strnatcmp0(nat_char const *a, nat_char const *b, int fold_case) {
  int ai, bi;
  nat_char ca, cb;
  int fractional, result;

  ai = bi = 0;
  while (1) {
    ca = a[ai];
    cb = b[bi];

    /* skip over leading spaces or zeros */
    while (nat_isspace(ca))
      ca = a[++ai];

    while (nat_isspace(cb))
      cb = b[++bi];

    /* process run of digits */
    if (nat_isdigit(ca) && nat_isdigit(cb)) {
      fractional = (ca == '0' || cb == '0');

      /* Left-aligned compare for zero-padded runs and for wide fixed-width
         fields (dates/years/ids); magnitude compare for plain counters. */
      if (fractional || (digit_run_len(a + ai) >= NAT_WIDE_DIGIT_RUN &&
                         digit_run_len(b + bi) >= NAT_WIDE_DIGIT_RUN)) {
        if ((result = compare_left(a + ai, b + bi)) != 0)
          return result;
      } else {
        if ((result = compare_right(a + ai, b + bi)) != 0)
          return result;
      }
    }

    if (!ca && !cb) {
      /* Equal apart from whitespace and case: break the tie on the raw bytes so
         that names differing only in whitespace get a deterministic order.
         Reached only after every digit-run and character comparison above
         returned equal, so this can never override a numeric result. */
      return compare_bytes(a, b, fold_case);
    }

    if (fold_case) {
      ca = nat_toupper(ca);
      cb = nat_toupper(cb);
    }

    /* Compare as unsigned: nat_char may be signed, which would sort every
       non-ASCII UTF-8 byte before ASCII. */
    if ((unsigned char)ca < (unsigned char)cb)
      return -1;

    if ((unsigned char)ca > (unsigned char)cb)
      return +1;

    ++ai;
    ++bi;
  }
}

int strnatcmp(nat_char const *a, nat_char const *b) { return strnatcmp0(a, b, 0); }

/* Compare, recognizing numeric string and ignoring case. */
int strnatcasecmp(nat_char const *a, nat_char const *b) { return strnatcmp0(a, b, 1); }
