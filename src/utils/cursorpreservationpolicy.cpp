#include "cursorpreservationpolicy.h"

#include <algorithm>

using namespace vnotex;

int CursorPreservationPolicy::computeRestoredCursorLine(int p_oldLine, int p_newBlockCount) {
  if (p_oldLine < 0 || p_newBlockCount <= 0) {
    return -1;
  }
  return std::min(p_oldLine, p_newBlockCount - 1);
}

int CursorPreservationPolicy::computeRestoredCursorOffset(int p_oldOffset,
                                                          int p_newBlockTextLength) {
  if (p_oldOffset < 0 || p_newBlockTextLength < 0) {
    return 0;
  }
  return std::min(p_oldOffset, p_newBlockTextLength);
}
