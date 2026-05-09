#ifndef HEADINGSLUGGER_H
#define HEADINGSLUGGER_H

#include <QHash>
#include <QString>

namespace vnotex {

class HeadingSlugger {
public:
  HeadingSlugger();

  // Generate a slug for @p_heading with dedup tracking.
  QString slug(const QString &p_heading);

  // Reset dedup state.
  void reset();

  // Stateless slug generation (no dedup).
  static QString slugify(const QString &p_text);

private:
  QHash<QString, int> m_occurrences;
};

} // namespace vnotex

#endif // HEADINGSLUGGER_H
