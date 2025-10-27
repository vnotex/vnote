#ifndef SEARCHTOKEN_H
#define SEARCHTOKEN_H

#include <QBitArray>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QString>
#include <QVector>

#include <core/global.h>

class QCommandLineParser;

namespace vnotex {
class SearchToken {
public:
  enum class Type { PlainText, RegularExpression };

  enum class Operator { And, Or };

  void clear();

  void append(const QString &p_text);

  void append(const QRegularExpression &p_regExp);

  // Whether @p_text is matched.
  bool matched(const QString &p_text, QList<Segment> *p_segments = nullptr) const;

  int constraintSize() const;

  bool isEmpty() const;

  bool shouldStartBatchMode() const;

  // Batch Mode: use a list of text string to match the same token.
  void startBatchMode();

  // Match one string in batch mode.
  // Return true if @p_text is matched.
  bool matchedInBatchMode(const QString &p_text, QList<Segment> *p_segments = nullptr);

  bool readyToEndBatchMode() const;

  void endBatchMode();

  QPair<QStringList, FindOptions> toPatterns() const;

  // Compile tokens from keyword.
  // Support some magic switchs in the keyword which will suppress the given options.
  static bool compile(const QString &p_keyword, FindOptions p_options, SearchToken &p_token);

  static QString getHelpText();

  static void addSearchOptionsToCommand(QCommandLineParser *p_parser);

private:
  static QCommandLineParser *getCommandLineParser();

  Type m_type = Type::PlainText;

  Operator m_operator = Operator::And;

  Qt::CaseSensitivity m_caseSensitivity = Qt::CaseInsensitive;

  QStringList m_keywords;

  QVector<QRegularExpression> m_regularExpressions;

  // [i] is true only if m_keywords[i] or m_regularExpressions[i] is matched.
  QBitArray m_matchedConstraintsInBatchMode;

  int m_matchedConstraintsCountInBatchMode = 0;
};
} // namespace vnotex

#endif // SEARCHTOKEN_H
