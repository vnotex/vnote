#include "simplesegmenthighlighter.h"

#include <QTextDocument>

using namespace vnotex;

SimpleSegmentHighlighter::SimpleSegmentHighlighter(QTextDocument *p_parent)
    : QSyntaxHighlighter(p_parent) {}

void SimpleSegmentHighlighter::highlightBlock(const QString &p_text) {
  if (m_segments.isEmpty() || !m_highlightFormat.isValid()) {
    return;
  }

  const int len = p_text.size();
  for (const auto &seg : m_segments) {
    if (seg.m_offset >= 0 && seg.m_offset < len) {
      setFormat(seg.m_offset, qMin(seg.m_length, len - seg.m_offset), m_highlightFormat);
    }
  }
}

void SimpleSegmentHighlighter::setSegments(const QList<Segment> &p_segments) {
  m_segments = p_segments;
}

void SimpleSegmentHighlighter::setHighlightFormat(const QBrush &p_foreground,
                                                  const QBrush &p_background) {
  m_highlightFormat.setForeground(p_foreground);
  m_highlightFormat.setBackground(p_background);
}
