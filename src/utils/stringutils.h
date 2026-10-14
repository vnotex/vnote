#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QLocale>
#include <QString>

namespace vnotex {

// Returns true if @p_a < @p_b under natural (human-friendly) sort order.
// Comparison is case-insensitive for alphabetic characters.
// Embedded numeric substrings are compared as unsigned integers rather than
// lexicographically, so "Note 2" < "Note 10".
bool naturalCompare(const QString &p_a, const QString &p_b);

// Format bytes as "<number> <unit>" using 1024-based B, KB, ... EB units.
// B is integral; larger units have two decimals. Unit selection uses the
// original byte count, before display rounding. No filesystem access.
QString formatFileSize(quint64 p_bytes, const QLocale &p_locale = QLocale());

} // namespace vnotex

#endif // STRINGUTILS_H
