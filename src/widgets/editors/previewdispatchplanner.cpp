#include "previewdispatchplanner.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

using namespace vnotex;

QVector<PreviewDispatchRequest>
PreviewDispatchPlanner::visibleFirst(const QVector<PreviewDispatchRequest> &p_requests,
                                     QTextDocument *p_document,
                                     const QPair<int, int> &p_visibleRange) {
  if (!p_document || p_visibleRange.first < 0 || p_visibleRange.first > p_visibleRange.second ||
      p_visibleRange.second >= p_document->blockCount()) {
    return p_requests;
  }

  QVector<PreviewDispatchRequest> visible;
  QVector<PreviewDispatchRequest> offscreen;
  visible.reserve(p_requests.size());
  offscreen.reserve(p_requests.size());

  for (const auto &request : p_requests) {
    bool isVisible = false;
    if (request.m_startBlock >= 0 && request.m_startBlock <= request.m_endBlock &&
        request.m_endBlock < p_document->blockCount()) {
      const int overlapStart = std::max(request.m_startBlock, p_visibleRange.first);
      const int overlapEnd = std::min(request.m_endBlock, p_visibleRange.second);
      if (overlapStart <= overlapEnd) {
        auto block = p_document->findBlockByNumber(overlapStart);
        for (int blockNumber = overlapStart; blockNumber <= overlapEnd;
             ++blockNumber, block = block.next()) {
          if (block.isVisible()) {
            isVisible = true;
            break;
          }
        }
      }
    }

    (isVisible ? visible : offscreen).append(request);
  }

  visible += offscreen;
  return visible;
}
