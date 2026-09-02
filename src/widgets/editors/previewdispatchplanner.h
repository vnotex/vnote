#ifndef PREVIEWDISPATCHPLANNER_H
#define PREVIEWDISPATCHPLANNER_H

#include <QPair>
#include <QVector>

class QTextDocument;

namespace vnotex {

struct PreviewDispatchRequest {
  bool operator==(const PreviewDispatchRequest &p_other) const {
    return m_previewId == p_other.m_previewId && m_startBlock == p_other.m_startBlock &&
           m_endBlock == p_other.m_endBlock;
  }

  int m_previewId = -1;
  int m_startBlock = -1;
  int m_endBlock = -1;
};

class PreviewDispatchPlanner {
public:
  PreviewDispatchPlanner() = delete;

  static QVector<PreviewDispatchRequest>
  visibleFirst(const QVector<PreviewDispatchRequest> &p_requests, QTextDocument *p_document,
               const QPair<int, int> &p_visibleRange);
};

} // namespace vnotex

#endif // PREVIEWDISPATCHPLANNER_H
