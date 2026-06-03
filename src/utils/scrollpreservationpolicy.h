#ifndef SCROLLPRESERVATIONPOLICY_H
#define SCROLLPRESERVATIONPOLICY_H

namespace vnotex {

// Pure-helper that decides where to scroll a view after a file reload, given
// the user's previous scroll state.
//
// Rule set for computeRestoredScrollValue(oldValue, oldMax, newMax):
//   1. Sanitize: clamp each input to >= 0.
//   2. At-end (TAKES PRECEDENCE — resolves oldMax == 0 in favor of tail-follow):
//        if oldValue >= oldMax return newMax.
//   3. At-start: if oldValue == 0 return 0.
//   4. Middle (default): return min(oldValue, newMax) (clamp to new max).
//
// Rule set for computeRestoredReadModeLine(oldTopLine):
//   - If oldTopLine < 0 return -1 (invalid sentinel; caller skips restore).
//   - Otherwise return oldTopLine unchanged. Out-of-range line numbers are
//     gracefully handled by MarkdownViewerAdapter::scrollToPosition via the
//     JS bridge; this helper does not gate on the document's line count.
class ScrollPreservationPolicy {
public:
  static int computeRestoredScrollValue(int p_oldValue, int p_oldMax, int p_newMax);

  static int computeRestoredReadModeLine(int p_oldTopLine);
};

} // namespace vnotex

#endif // SCROLLPRESERVATIONPOLICY_H
