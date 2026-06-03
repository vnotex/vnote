#include "scrollpreservationpolicy.h"

#include <algorithm>

using namespace vnotex;

int ScrollPreservationPolicy::computeRestoredScrollValue(int p_oldValue, int p_oldMax,
                                                         int p_newMax) {
  const int oldValue = std::max(p_oldValue, 0);
  const int oldMax = std::max(p_oldMax, 0);
  const int newMax = std::max(p_newMax, 0);

  // At-end check FIRST: resolves oldMax == 0 ambiguity as tail-follow.
  if (oldValue >= oldMax) {
    return newMax;
  }

  // At-start (only reachable when oldValue < oldMax, so oldMax > 0).
  if (oldValue == 0) {
    return 0;
  }

  // Middle: clamp to new max.
  return std::min(oldValue, newMax);
}

int ScrollPreservationPolicy::computeRestoredReadModeLine(int p_oldTopLine) {
  if (p_oldTopLine < 0) {
    return -1;
  }
  return p_oldTopLine;
}
