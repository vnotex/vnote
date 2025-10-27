#ifndef SIMPLESEGMENTHIGHLIGHTER_H
#define SIMPLESEGMENTHIGHLIGHTER_H

#include <QList>
#include <QSyntaxHighlighter>

#include <core/global.h>

namespace vnotex {
class SimpleSegmentHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
public:
  explicit SimpleSegmentHighlighter(QTextDocument *p_parent);

  void setSegments(const QList<Segment> &p_segments);

  void setHighlightFormat(const QBrush &p_foreground, const QBrush &p_background);

protected:
  void highlightBlock(const QString &p_text) Q_DECL_OVERRIDE;

private:
  QTextCharFormat m_highlightFormat;

  QList<Segment> m_segments;
};
} // namespace vnotex

#endif // SIMPLESEGMENTHIGHLIGHTER_H
