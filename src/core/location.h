#ifndef LOCATION_H
#define LOCATION_H

#include <QDebug>

#include "global.h"

namespace vnotex {
struct Location {
  friend QDebug operator<<(QDebug p_dbg, const Location &p_loc) {
    QDebugStateSaver saver(p_dbg);
    p_dbg.nospace() << p_loc.m_path << ":" << p_loc.m_lineNumber;
    return p_dbg;
  }

  // TODO: support encoding like buffer/notebook.
  QString m_path;

  QString m_displayPath;

  // 0-based.
  int m_lineNumber = -1;
};

enum class LocationType { Buffer, File, Folder, Notebook };

struct ComplexLocation {
  struct Line {
    Line() = default;

    Line(int p_lineNumber, const QString &p_text, const QList<Segment> &p_segments)
        : m_lineNumber(p_lineNumber), m_text(p_text), m_segments(p_segments) {}

    // 0-based.
    int m_lineNumber = -1;

    QString m_text;

    QList<Segment> m_segments;
  };

  void addLine(int p_lineNumber, const QString &p_text, const QList<Segment> &p_segments) {
    m_lines.push_back(Line(p_lineNumber, p_text, p_segments));
  }

  friend QDebug operator<<(QDebug p_dbg, const ComplexLocation &p_loc) {
    QDebugStateSaver saver(p_dbg);
    p_dbg.nospace() << static_cast<int>(p_loc.m_type) << p_loc.m_path << p_loc.m_displayPath;
    return p_dbg;
  }

  LocationType m_type = LocationType::File;

  QString m_path;

  QString m_displayPath;

  QVector<Line> m_lines;
};
} // namespace vnotex

#endif // LOCATION_H
