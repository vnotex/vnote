#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>

namespace vnotex {

// Returns true if @p_a < @p_b under natural (human-friendly) sort order.
// Comparison is case-insensitive for alphabetic characters.
// Embedded numeric substrings are compared as unsigned integers rather than
// lexicographically, so "Note 2" < "Note 10".
bool naturalCompare(const QString &p_a, const QString &p_b);

} // namespace vnotex

#endif // STRINGUTILS_H
