#include "legacyimagemigrationbar.h"

#include <QPushButton>

using namespace vnotex;

LegacyImageMigrationBar::LegacyImageMigrationBar(QWidget *p_parent)
    : InlineBanner(Severity::Warning, QString(), p_parent) {
  connect(addActionButton(tr("Migrate to Assets Folder")), &QPushButton::clicked, this,
          &LegacyImageMigrationBar::migrateRequested);
  connect(addActionButton(tr("Not Now")), &QPushButton::clicked, this,
          &LegacyImageMigrationBar::dismissRequested);
  connect(addActionButton(tr("Don't Ask Again")), &QPushButton::clicked, this,
          &LegacyImageMigrationBar::neverRequested);

  setImageCount(0);
}

void LegacyImageMigrationBar::setImageCount(int p_count) {
  setText(tr("This note uses %n image(s) from a deprecated image folder. Migrating them to the "
             "assets folder relocates the files on disk; other notes still linking to them will "
             "break.",
             "", p_count));
}
