#include "stringutils.h"
#include "strnatcmp.h"

namespace vnotex {

bool naturalCompare(const QString &p_a, const QString &p_b) {
  return strnatcasecmp(p_a.toUtf8().constData(), p_b.toUtf8().constData()) < 0;
}

} // namespace vnotex
