#ifndef LEGACYIMAGEMIGRATIONBAR_H
#define LEGACYIMAGEMIGRATIONBAR_H

#include "inlinebanner.h"

namespace vnotex {

// Offers to migrate images out of a pre-v4 image folder (vx_images /
// _v_images) into the note's v4 assets folder.
//
// Hosted via ViewWindow2::addTopWidget(). A thin, feature-specific View over
// InlineBanner: it owns the copy and the three action buttons, and nothing
// else. All policy lives in MarkdownViewWindow2 +
// LegacyImageMigrationController.
class LegacyImageMigrationBar : public InlineBanner {
  Q_OBJECT

public:
  explicit LegacyImageMigrationBar(QWidget *p_parent = nullptr);

  // Number of legacy image OCCURRENCES detected in the note.
  void setImageCount(int p_count);

signals:
  void migrateRequested();

  void dismissRequested();

  void neverRequested();
};

} // namespace vnotex

#endif // LEGACYIMAGEMIGRATIONBAR_H
