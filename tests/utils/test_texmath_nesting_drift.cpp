// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_texmath_nesting_drift.cpp
//
// Grep-gate regression test over two vendored JS assets:
//   1. src/data/extra/web/js/markdown-it/markdown-it-texmath.js
//   2. src/data/extra/web/js/markdownit.js
//
// === Why this test exists ===
// Upstream markdown-it-texmath pushes its BLOCK token with nesting `1`:
//     let token = state.push(rule.name, 'math', 1);
// and never pushes a matching close token. StateBlock.push() increments
// state.level for nesting > 0, so every `$$...$$` block permanently raises
// state.level by one. markdown-it's ParserBlock.tokenize() bails out silently
// once state.level reaches options.maxNesting (default 100):
//     if (state.level >= maxNesting) { state.line = endLine; break }
// The remainder of the document is discarded with no error. A note with more
// than 100 math blocks rendered only the first 100 formulas, and every trailing
// non-math paragraph vanished too.
//
// The fix is nesting `0` (math blocks are self-closing; every rule name has an
// explicit md.renderer.rules[...] entry, so renderToken()/nesting is never
// consulted for output). An explicit `maxNesting: 500` in markdownit.js is
// defense-in-depth against a future unbalanced plugin, not the fix.
//
// A markdown-it-texmath upgrade would reintroduce the upstream `1` with no
// visible symptom until a user writes their 101st formula. There is no JS test
// runner in this repo, so this grep gate is the regression guard.
//
// === Region scoping (load-bearing) ===
// The block assertion is scoped to the BLOCK factory only -- from
// `texmath.block =` up to `texmath.render =`. The INLINE factory above it has
// an identical `state.push(rule.name, 'math', 0)` call shape, so a whole-file
// check would be satisfied by the inline push even if the block factory were
// deleted or reverted.
//
// === Why a real lexer (blankJsNonCode) and not a regex ===
// A naive comment stripper that treats every quote as a string delimiter is
// desynchronised by the very first function in the texmath asset, which does
//     .replace(/"/g, "&quot;").replace(/'/g, "&#039;")
// -- quote characters inside REGEX literals. Once desynchronised it stops
// removing comments, and a commented-out `// let token = state.push(...)` then
// satisfies the gate: a silent FALSE PASS in exactly the file being guarded.
// blankJsNonCode() therefore lexes code / line comment / block comment /
// string / template / regex properly, and blanks the CONTENTS of every
// non-code run (comments entirely, literals down to their delimiters). Blanking
// literal contents also keeps the brace matcher below from counting a `{`
// that only ever appears inside a string.

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtTest>

namespace tests {

class TestTexmathNestingDrift : public QObject {
  Q_OBJECT

private slots:
  void blockRulePushesZeroNesting();
  void markdownItSetsExplicitMaxNesting();
  void lexerBlanksCommentsAfterRegexLiteralsHoldingQuotes();
  void gateRejectsTheRegressionsItGuards();

private:
  static QString readAsset(const QString &p_relPath);

  // Blanks every non-code run of a JavaScript source, preserving length and
  // line structure: comments become spaces, and string / template / regex
  // literals keep only their delimiters. See the header comment for why a
  // proper lexer is required here.
  static QString blankJsNonCode(const QString &p_source);

  // The two assertions, factored out so they can be exercised against
  // synthetic decoys as well as against the real assets. Return an empty
  // string on success, or a human-readable reason on failure.
  static QString checkBlockRegion(const QString &p_texmathSource);
  static QString checkMaxNesting(const QString &p_markdownitSource);
};

QString TestTexmathNestingDrift::blankJsNonCode(const QString &p_source) {
  QString out = p_source;
  const int n = out.size();
  // The last significant code character, used to decide whether a `/` opens a
  // regex literal or is a division operator.
  QChar prev;
  bool prevWasWordChar = false;
  QString prevWord;
  // Set when `prev` is the `)` that closed an `if`/`while`/`for`/... header. A
  // regex literal may legally begin there, whereas after a `)` that completed a
  // value expression a `/` is division.
  bool prevIsControlCloseParen = false;
  // Set when `prev` is the second character of a postfix `++`/`--`, after which
  // a `/` is division rather than the start of a regex.
  bool prevIsPostfixOp = false;
  // Per open paren: was it a control-statement header?
  QVector<bool> parenIsControl;

  auto blankRange = [&out](int p_from, int p_to) {
    for (int k = p_from; k < p_to; ++k) {
      if (out.at(k) != QLatin1Char('\n')) {
        out[k] = QLatin1Char(' ');
      }
    }
  };

  int i = 0;
  while (i < n) {
    const QChar c = out.at(i);
    const QChar next = (i + 1 < n) ? out.at(i + 1) : QChar();

    // Line comment.
    if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
      int j = i;
      while (j < n && out.at(j) != QLatin1Char('\n')) {
        ++j;
      }
      blankRange(i, j);
      i = j;
      continue;
    }

    // Block comment.
    if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
      const int close = out.indexOf(QStringLiteral("*/"), i + 2);
      const int j = (close < 0) ? n : close + 2;
      blankRange(i, j);
      i = j;
      continue;
    }

    // String or template literal: keep the delimiters, blank the contents.
    if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
      const QChar quote = c;
      int j = i + 1;
      while (j < n) {
        const QChar d = out.at(j);
        if (d == QLatin1Char('\\')) {
          j += 2;
          continue;
        }
        if (d == quote) {
          break;
        }
        // An unterminated single-quoted string cannot span a newline; stop
        // there rather than swallowing the rest of the file.
        if (quote != QLatin1Char('`') && d == QLatin1Char('\n')) {
          break;
        }
        ++j;
      }
      blankRange(i + 1, qMin(j, n));
      i = qMin(j + 1, n);
      prev = quote;
      prevWasWordChar = false;
      prevIsControlCloseParen = false;
      prevIsPostfixOp = false;
      prevWord.clear();
      continue;
    }

    // Regex literal. `/` starts one only where a value may begin.
    if (c == QLatin1Char('/')) {
      static const QString regexPreceders = QStringLiteral("(,=:[!&|?{};+-*%^~<>");
      static const QStringList regexKeywords = {
          QStringLiteral("return"), QStringLiteral("typeof"), QStringLiteral("case"),
          QStringLiteral("in"),     QStringLiteral("of"),     QStringLiteral("new"),
          QStringLiteral("delete"), QStringLiteral("void"),   QStringLiteral("do"),
          QStringLiteral("else"),   QStringLiteral("yield"),  QStringLiteral("await")};
      const bool allowed = prev.isNull() || prevIsControlCloseParen ||
                           (!prevIsPostfixOp && regexPreceders.contains(prev)) ||
                           (prevWasWordChar && regexKeywords.contains(prevWord));
      if (allowed) {
        int j = i + 1;
        bool inClass = false;
        bool closed = false;
        while (j < n) {
          const QChar d = out.at(j);
          if (d == QLatin1Char('\\')) {
            j += 2;
            continue;
          }
          if (d == QLatin1Char('\n')) {
            break; // Unterminated: not a regex after all.
          }
          if (d == QLatin1Char('[')) {
            inClass = true;
          } else if (d == QLatin1Char(']')) {
            inClass = false;
          } else if (d == QLatin1Char('/') && !inClass) {
            closed = true;
            break;
          }
          ++j;
        }
        if (closed) {
          blankRange(i + 1, j);
          i = j + 1;
          // Skip the flags so they are not mistaken for an identifier.
          while (i < n && out.at(i).isLetter()) {
            ++i;
          }
          prev = QLatin1Char('/');
          prevWasWordChar = false;
          prevIsControlCloseParen = false;
          prevIsPostfixOp = false;
          prevWord.clear();
          continue;
        }
      }
    }

    if (!c.isSpace()) {
      static const QStringList controlKeywords = {
          QStringLiteral("if"),   QStringLiteral("while"),  QStringLiteral("for"),
          QStringLiteral("with"), QStringLiteral("switch"), QStringLiteral("catch")};
      if (c == QLatin1Char('(')) {
        parenIsControl.append(prevWasWordChar && controlKeywords.contains(prevWord));
      }
      const bool closingControlParen =
          (c == QLatin1Char(')')) && !parenIsControl.isEmpty() && parenIsControl.takeLast();
      prevIsPostfixOp = (c == QLatin1Char('+') || c == QLatin1Char('-')) && prev == c;
      prevIsControlCloseParen = closingControlParen;
      prev = c;
      if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$')) {
        if (!prevWasWordChar) {
          prevWord.clear();
        }
        prevWord.append(c);
        prevWasWordChar = true;
      } else {
        prevWasWordChar = false;
        prevWord.clear();
      }
    }
    ++i;
  }
  return out;
}

QString TestTexmathNestingDrift::readAsset(const QString &p_relPath) {
  const QString path = QStringLiteral(VNOTE_SOURCE_DIR) + p_relPath;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // Reported by the caller's QVERIFY2 on the empty result.
    qWarning() << "cannot open asset:" << path;
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

QString TestTexmathNestingDrift::checkBlockRegion(const QString &p_texmathSource) {
  const QString source = blankJsNonCode(p_texmathSource);

  const int begin = source.indexOf(QStringLiteral("texmath.block ="));
  if (begin < 0) {
    return QStringLiteral("region delimiter `texmath.block =` not found; the vendored file was "
                          "restructured -- update this gate");
  }
  const int end = source.indexOf(QStringLiteral("texmath.render ="), begin);
  if (end <= begin) {
    return QStringLiteral("region delimiter `texmath.render =` not found after `texmath.block =`; "
                          "the vendored file was restructured -- update this gate");
  }

  const QString region = source.mid(begin, end - begin);

  // Exactly one state.push() ASSIGNMENT in the block factory, and its third
  // argument is the literal 0.
  static const QRegularExpression pushRe(QStringLiteral("=\\s*state\\.push\\s*\\(([^)]*)\\)"));
  QStringList pushes;
  auto it = pushRe.globalMatch(region);
  while (it.hasNext()) {
    pushes.append(it.next().captured(1).trimmed());
  }
  if (pushes.size() != 1) {
    return QStringLiteral("expected exactly 1 `= state.push(...)` in the texmath block factory, "
                          "found %1")
        .arg(pushes.size());
  }

  const QStringList args = pushes.first().split(QLatin1Char(','));
  if (args.size() != 3) {
    return QStringLiteral("`state.push()` in the block factory takes %1 arguments, expected 3")
        .arg(args.size());
  }
  if (args.at(2).trimmed() != QStringLiteral("0")) {
    return QStringLiteral(
               "the texmath BLOCK token is pushed with nesting `%1`, not `0`. It has no "
               "closing token, so a non-zero nesting permanently inflates state.level and "
               "markdown-it silently truncates the document at options.maxNesting.")
        .arg(args.at(2).trimmed());
  }

  // The token fields the renderer and the line mapping depend on must survive.
  const QStringList fields = {QStringLiteral("token.block"), QStringLiteral("token.content"),
                              QStringLiteral("token.map")};
  for (const QString &field : fields) {
    if (!region.contains(field + QStringLiteral(" ="))) {
      return QStringLiteral("`%1` assignment disappeared from the block factory").arg(field);
    }
  }
  return QString();
}

QString TestTexmathNestingDrift::checkMaxNesting(const QString &p_markdownitSource) {
  const QString source = blankJsNonCode(p_markdownitSource);

  const QString anchor = QStringLiteral("window.markdownit({");
  const int begin = source.indexOf(anchor);
  if (begin < 0) {
    return QStringLiteral("`window.markdownit({` not found");
  }

  // Bound the search to the options object itself, and to its TOP level: a
  // `maxNesting: 500` buried inside a nested object (e.g. in the `highlight`
  // callback) is not an option markdown-it ever sees. Literal contents were
  // blanked above, so only code-level braces are counted.
  int depth = 0;
  int end = -1;
  // Same length as `source`, holding only the characters that sit at options
  // depth 1; everything else is a space.
  QString topLevel(source.size(), QLatin1Char(' '));
  for (int i = begin + anchor.size() - 1; i < source.size(); ++i) {
    const QChar c = source.at(i);
    if (c == QLatin1Char('{')) {
      ++depth;
    } else if (c == QLatin1Char('}')) {
      if (--depth == 0) {
        end = i;
        break;
      }
    } else if (depth == 1) {
      topLevel[i] = c;
    }
  }
  if (end <= begin) {
    return QStringLiteral("unterminated window.markdownit({...}) options object");
  }

  const QString options = topLevel.mid(begin, end - begin);
  // A value terminator is required, so `maxNesting: 500 - 400` does not pass.
  static const QRegularExpression maxNestingRe(
      QStringLiteral("maxNesting\\s*:\\s*500\\s*(?=[,}])"));
  if (!maxNestingRe.match(options).hasMatch()) {
    return QStringLiteral(
        "`maxNesting: 500` is missing from the top level of the window.markdownit({...}) "
        "options. It is defense-in-depth against an unbalanced third-party token pusher "
        "silently truncating the document; do not remove or lower it.");
  }
  return QString();
}

void TestTexmathNestingDrift::blockRulePushesZeroNesting() {
  const QString raw =
      readAsset(QStringLiteral("/src/data/extra/web/js/markdown-it/markdown-it-texmath.js"));
  QVERIFY2(!raw.isEmpty(), "markdown-it-texmath.js is empty or unreadable");

  const QString err = checkBlockRegion(raw);
  QVERIFY2(err.isEmpty(), qPrintable(err));
}

void TestTexmathNestingDrift::markdownItSetsExplicitMaxNesting() {
  const QString raw = readAsset(QStringLiteral("/src/data/extra/web/js/markdownit.js"));
  QVERIFY2(!raw.isEmpty(), "markdownit.js is empty or unreadable");

  const QString err = checkMaxNesting(raw);
  QVERIFY2(err.isEmpty(), qPrintable(err));
}

// The exact desynchronisation that made an earlier version of this gate a false
// pass: quote characters inside regex literals, as they appear at the top of
// markdown-it-texmath.js.
void TestTexmathNestingDrift::lexerBlanksCommentsAfterRegexLiteralsHoldingQuotes() {
  const QString js = QStringLiteral("function escapeHTML(text) {\n"
                                    "    return text\n"
                                    "        .replace(/\"/g, \"&quot;\")\n"
                                    "        .replace(/'/g, \"&#039;\");\n"
                                    "}\n"
                                    "// let token = state.push(rule.name, 'math', 0);\n"
                                    "const real = 1;\n");
  const QString blanked = blankJsNonCode(js);
  QVERIFY2(!blanked.contains(QStringLiteral("state.push")),
           "the lexer was desynchronised by a regex literal holding a quote character, so a "
           "commented-out push survived");
  QVERIFY(blanked.contains(QStringLiteral("const real = 1;")));
  // Delimiters survive; contents do not.
  QVERIFY(!blanked.contains(QStringLiteral("&quot;")));
  QCOMPARE(blanked.size(), js.size());

  // Division must not be mistaken for a regex literal.
  const QString division =
      QStringLiteral("const a = b / c; const d = e / f;\n// gone\nvar keep;\n");
  const QString blankedDivision = blankJsNonCode(division);
  QVERIFY(blankedDivision.contains(QStringLiteral("const d = e / f;")));
  QVERIFY(!blankedDivision.contains(QStringLiteral("gone")));
  QVERIFY(blankedDivision.contains(QStringLiteral("var keep;")));

  // A regex literal is legal directly after a control-statement `)`, and must be
  // lexed as one -- otherwise a quote inside it opens a phantom string that
  // swallows the following `//` and exposes the comment as code.
  const QString afterControlParen =
      QStringLiteral("if (true) /\"/.test(text); // \" let token = state.push(a, b, 0);\n"
                     "var keep;\n");
  const QString blankedControl = blankJsNonCode(afterControlParen);
  QVERIFY2(!blankedControl.contains(QStringLiteral("state.push")),
           "a regex literal after a control-statement `)` desynchronised the lexer");
  QVERIFY(blankedControl.contains(QStringLiteral("var keep;")));

  // But a `)` that completed a value expression is followed by division.
  const QString valueParen = QStringLiteral("var x = (a + b) / c;\nvar keep;\n");
  QVERIFY(blankJsNonCode(valueParen).contains(QStringLiteral("(a + b) / c;")));

  // Postfix `++` is followed by division, not by a regex literal.
  const QString postfix = QStringLiteral("var y = i++ / 2;\n// gone\nvar keep;\n");
  const QString blankedPostfix = blankJsNonCode(postfix);
  QVERIFY(blankedPostfix.contains(QStringLiteral("i++ / 2;")));
  QVERIFY(!blankedPostfix.contains(QStringLiteral("gone")));
}

void TestTexmathNestingDrift::gateRejectsTheRegressionsItGuards() {
  const QString texmath =
      readAsset(QStringLiteral("/src/data/extra/web/js/markdown-it/markdown-it-texmath.js"));
  QVERIFY(!texmath.isEmpty());
  const QString markdownit = readAsset(QStringLiteral("/src/data/extra/web/js/markdownit.js"));
  QVERIFY(!markdownit.isEmpty());

  const QString good = QStringLiteral("state.push(rule.name, 'math', 0)");

  // 1. The upstream reversion to nesting 1.
  QVERIFY(!checkBlockRegion(
               QString(texmath).replace(good, QStringLiteral("state.push(rule.name, 'math', 1)")))
               .isEmpty());

  // 2. The push commented out (the false pass an earlier version of this gate
  //    allowed).
  QVERIFY(!checkBlockRegion(QString(texmath).replace(QStringLiteral("let token = ") + good,
                                                     QStringLiteral("// let token = ") + good))
               .isEmpty());

  // 3. A token field the renderer / line mapping needs, removed.
  QVERIFY(!checkBlockRegion(
               QString(texmath).replace(QStringLiteral("token.map ="), QStringLiteral("token.x =")))
               .isEmpty());

  // 4. The block factory deleted outright: the inline factory's identical push
  //    must not stand in for it.
  QVERIFY(!checkBlockRegion(QString(texmath).replace(QStringLiteral("texmath.block ="),
                                                     QStringLiteral("texmath.blockRemoved =")))
               .isEmpty());

  // 5. The push commented out behind a regex-after-control-paren construct,
  //    which desynchronises a naive lexer.
  QVERIFY(!checkBlockRegion(
               QString(texmath).replace(QStringLiteral("let token = ") + good,
                                        QStringLiteral("if (true) /\"/.test(str); // \" let "
                                                       "token = ") +
                                            good))
               .isEmpty());

  // 6. maxNesting removed, commented out, or reduced.
  QVERIFY(
      !checkMaxNesting(QString(markdownit).replace(QStringLiteral("maxNesting: 500,"), QString()))
           .isEmpty());
  QVERIFY(!checkMaxNesting(QString(markdownit)
                               .replace(QStringLiteral("maxNesting: 500,"),
                                        QStringLiteral("// maxNesting: 500,")))
               .isEmpty());
  QVERIFY(!checkMaxNesting(
               QString(markdownit)
                   .replace(QStringLiteral("maxNesting: 500,"), QStringLiteral("maxNesting: 100,")))
               .isEmpty());
  QVERIFY(!checkMaxNesting(QString(markdownit)
                               .replace(QStringLiteral("maxNesting: 500,"),
                                        QStringLiteral("maxNesting: 500 - 400,")))
               .isEmpty());

  // 7. maxNesting present, but nested inside the `highlight` callback rather
  //    than at the top level of the options object, where markdown-it never
  //    sees it.
  QVERIFY(!checkMaxNesting(QString(markdownit)
                               .replace(QStringLiteral("maxNesting: 500,"), QString())
                               .replace(QStringLiteral("// Use external default escaping."),
                                        QStringLiteral("const ignored = { maxNesting: 500 };")))
               .isEmpty());

  // And the unmodified assets still pass, so the decoys above are meaningful.
  QVERIFY(checkBlockRegion(texmath).isEmpty());
  QVERIFY(checkMaxNesting(markdownit).isEmpty());
}

} // namespace tests

QTEST_APPLESS_MAIN(tests::TestTexmathNestingDrift)

#include "test_texmath_nesting_drift.moc"
