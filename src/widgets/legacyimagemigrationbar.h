#ifndef LEGACYIMAGEMIGRATIONBAR_H
#define LEGACYIMAGEMIGRATIONBAR_H

#include <QWidget>

class QLabel;

namespace vnotex {

// Inline, dismissible bar offering to move images out of a pre-v4 image folder
// (vx_images / _v_images) into the note's v4 assets folder.
//
// Hosted via ViewWindow2::addTopWidget(). Pure view: it renders a count and
// emits three intents; all policy lives in MarkdownViewWindow2 +
// LegacyImageMigrationController.
class LegacyImageMigrationBar : public QWidget {
  Q_OBJECT

public:
  explicit LegacyImageMigrationBar(QWidget *p_parent = nullptr);

  // Number of legacy image OCCURRENCES detected in the note.
  void setImageCount(int p_count);

signals:
  void migrateRequested();

  void dismissRequested();

  void neverRequested();

private:
  void setupUI();

  QLabel *m_label = nullptr;
};

} // namespace vnotex

#endif // LEGACYIMAGEMIGRATIONBAR_H
