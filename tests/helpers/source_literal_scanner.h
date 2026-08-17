// SPDX-License-Identifier: LGPL-3.0-or-later
//
// source_literal_scanner.h
//
// Shared C++ string-literal extractor for the grep-gate regression tests.
//
// === Why this is shared ===
// VNote runs several gates that fail the build when a source file contains a
// string literal of a forbidden shape (a hardcoded stylesheet color, a
// hand-written Markdown image regex, ...). Each needs the same front half:
// walk a .cpp/.h, skip comments and character literals, honor raw strings and
// encoding prefixes, and coalesce adjacent literals the way the compiler does.
//
// That front half was copy-pasted once and immediately began to drift. A gate
// whose scanner under-matches FAILS OPEN -- it reports success while seeing
// nothing -- which is the exact failure mode these gates exist to prevent, so a
// parsing bug fixed in one copy silently leaving the other blind is not an
// acceptable outcome. One implementation, two predicates.
//
// === Known limits (deliberate) ===
// This is a lexer for the shapes these gates care about, not a C++ front end.
// Hex, octal and universal-character escapes are not decoded to their values,
// line splicing is not honored, and digit separators inside numeric literals
// are irrelevant here. Only string literals are produced; anything assembled at
// runtime from non-literal pieces is invisible to every gate built on this.

#ifndef TESTS_HELPERS_SOURCE_LITERAL_SCANNER_H
#define TESTS_HELPERS_SOURCE_LITERAL_SCANNER_H

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tests {
namespace literalscan {

// One coalesced C++ string literal and the source lines it spans.
struct SourceLiteral {
  QString text;
  int firstLine = 0;
  int lastLine = 0;
};

// How escapes are recorded: a backslash escape contributes only the escaped
// character, which is what the compiler does for the cases these gates care
// about. `"\\!"` therefore yields the two characters `\!` -- the value a
// regular-expression engine would see -- and `"\""` yields `"`.

// Extract every string literal in p_source, with adjacent literals coalesced.
inline QVector<SourceLiteral> extractLiterals(const QString &p_source) {
  QVector<SourceLiteral> out;
  const int n = p_source.size();
  int i = 0;
  int line = 1;

  const auto advance = [&](int p_count) {
    for (int k = 0; k < p_count && i < n; ++k, ++i) {
      if (p_source.at(i) == QLatin1Char('\n')) {
        ++line;
      }
    }
  };

  // Skips whitespace and comments; returns false at end of input.
  const auto skipTrivia = [&]() {
    while (i < n) {
      const QChar c = p_source.at(i);
      if (c.isSpace()) {
        advance(1);
      } else if (c == QLatin1Char('/') && i + 1 < n && p_source.at(i + 1) == QLatin1Char('/')) {
        while (i < n && p_source.at(i) != QLatin1Char('\n')) {
          advance(1);
        }
      } else if (c == QLatin1Char('/') && i + 1 < n && p_source.at(i + 1) == QLatin1Char('*')) {
        advance(2);
        while (i + 1 < n &&
               !(p_source.at(i) == QLatin1Char('*') && p_source.at(i + 1) == QLatin1Char('/'))) {
          advance(1);
        }
        advance(2);
      } else {
        return true;
      }
    }
    return false;
  };

  // Reads ONE literal starting at i and appends its body to p_body.
  const auto readOneLiteral = [&](QString *p_body) {
    if (p_source.at(i) == QLatin1Char('R') && i + 1 < n && p_source.at(i + 1) == QLatin1Char('"')) {
      advance(2);
      QString delim;
      while (i < n && p_source.at(i) != QLatin1Char('(')) {
        delim.append(p_source.at(i));
        advance(1);
      }
      advance(1); // '('
      const QString terminator = QLatin1Char(')') + delim + QLatin1Char('"');
      const int end = p_source.indexOf(terminator, i);
      if (end < 0) {
        i = n;
        return;
      }
      // A raw string has no escapes; its body is already the runtime value.
      p_body->append(p_source.mid(i, end - i));
      advance(end - i + terminator.size());
      return;
    }

    advance(1); // opening quote
    while (i < n) {
      const QChar c = p_source.at(i);
      if (c == QLatin1Char('\\')) {
        if (i + 1 < n) {
          p_body->append(p_source.at(i + 1));
        }
        advance(2);
        continue;
      }
      if (c == QLatin1Char('"')) {
        advance(1);
        return;
      }
      p_body->append(c);
      advance(1);
    }
  };

  // True when i sits on a literal start, skipping an encoding prefix.
  const auto atLiteralStart = [&]() {
    int j = i;
    if (j < n && (p_source.at(j) == QLatin1Char('L') || p_source.at(j) == QLatin1Char('u') ||
                  p_source.at(j) == QLatin1Char('U'))) {
      // Must not be the tail of a longer identifier.
      if (j > 0 &&
          (p_source.at(j - 1).isLetterOrNumber() || p_source.at(j - 1) == QLatin1Char('_'))) {
        return false;
      }
      ++j;
      if (j < n && p_source.at(j) == QLatin1Char('8')) {
        ++j;
      }
    }
    if (j < n &&
        (p_source.at(j) == QLatin1Char('"') || (p_source.at(j) == QLatin1Char('R') && j + 1 < n &&
                                                p_source.at(j + 1) == QLatin1Char('"')))) {
      advance(j - i);
      return true;
    }
    return false;
  };

  while (i < n) {
    const QChar c = p_source.at(i);

    if (c == QLatin1Char('/') && i + 1 < n &&
        (p_source.at(i + 1) == QLatin1Char('/') || p_source.at(i + 1) == QLatin1Char('*'))) {
      skipTrivia();
      continue;
    }

    // Character literal: skip it, so a '"' does not open a phantom string
    // literal and swallow the real one that follows. QLatin1Char('"') occurs in
    // the scanned trees, and without this a forbidden literal added later in
    // such a file slips past the gate.
    if (c == QLatin1Char('\'')) {
      advance(1);
      while (i < n && p_source.at(i) != QLatin1Char('\'')) {
        advance(p_source.at(i) == QLatin1Char('\\') ? 2 : 1);
      }
      advance(1);
      continue;
    }

    if (!atLiteralStart()) {
      advance(1);
      continue;
    }

    SourceLiteral lit;
    lit.firstLine = line;
    QString body;
    readOneLiteral(&body);
    lit.lastLine = line;

    // C++ concatenates adjacent literals separated only by whitespace and
    // comments. Splitting a forbidden string across lines must NOT defeat a
    // gate.
    for (;;) {
      const int savedI = i;
      const int savedLine = line;
      if (!skipTrivia() || !atLiteralStart()) {
        i = savedI;
        line = savedLine;
        break;
      }
      readOneLiteral(&body);
      lit.lastLine = line;
    }

    lit.text = body;
    out.append(lit);
  }

  return out;
}

// True when any line the literal spans carries a per-line escape hatch.
// p_markers are the comment markers the calling gate honors.
inline bool hasEscapeHatch(const QStringList &p_lines, const SourceLiteral &p_literal,
                           const QStringList &p_markers) {
  for (int ln = p_literal.firstLine; ln <= p_literal.lastLine && ln <= p_lines.size(); ++ln) {
    for (const QString &marker : p_markers) {
      if (p_lines.at(ln - 1).contains(marker)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace literalscan
} // namespace tests

#endif // TESTS_HELPERS_SOURCE_LITERAL_SCANNER_H
