#include "stringutils.h"

namespace vnotex {

bool naturalCompare(const QString &p_a, const QString &p_b) {
  int ia = 0;
  int ib = 0;
  const int na = p_a.length();
  const int nb = p_b.length();

  while (ia < na && ib < nb) {
    const QChar ca = p_a[ia];
    const QChar cb = p_b[ib];

    if (ca.isDigit() && cb.isDigit()) {
      // Extract both numeric runs.
      int startA = ia;
      while (ia < na && p_a[ia].isDigit()) {
        ++ia;
      }
      int startB = ib;
      while (ib < nb && p_b[ib].isDigit()) {
        ++ib;
      }

      const qulonglong numA = p_a.mid(startA, ia - startA).toULongLong();
      const qulonglong numB = p_b.mid(startB, ib - startB).toULongLong();

      if (numA != numB) {
        return numA < numB;
      }
    } else {
      // Extract both non-digit runs.
      int startA = ia;
      while (ia < na && !p_a[ia].isDigit()) {
        ++ia;
      }
      int startB = ib;
      while (ib < nb && !p_b[ib].isDigit()) {
        ++ib;
      }

      const QString runA = p_a.mid(startA, ia - startA);
      const QString runB = p_b.mid(startB, ib - startB);

      // When one run is a prefix of the other, the longer run (no digit after)
      // sorts before the shorter (digit follows). So "note.md" < "note1.md".
      const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
      if (runB.size() < runA.size() && runA.startsWith(runB, cs)) {
        return true; // p_a has more non-digits only, p_b has digits next
      }
      if (runA.size() < runB.size() && runB.startsWith(runA, cs)) {
        return false; // p_a has digits next, p_b has more non-digits; p_a > p_b
      }

      const int cmp = QString::compare(runA, runB, Qt::CaseInsensitive);
      if (cmp != 0) {
        return cmp < 0;
      }
    }
  }

  // If all common segments are equal, the shorter string sorts first.
  return (na - ia) < (nb - ib);
}

} // namespace vnotex
