#include "legacyimagemigrationbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

using namespace vnotex;

LegacyImageMigrationBar::LegacyImageMigrationBar(QWidget *p_parent) : QWidget(p_parent) {
  setupUI();
}

void LegacyImageMigrationBar::setupUI() {
  // Hand-rolled banner, following the precedent in LocationList2::setupUI().
  setStyleSheet(QStringLiteral("QWidget { background-color: #FFF3CD; color: #856404; }"));

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 4, 8, 4);
  layout->setSpacing(8);

  m_label = new QLabel(this);
  m_label->setWordWrap(true);
  layout->addWidget(m_label, 1);

  auto *migrateBtn = new QPushButton(tr("Move to Assets Folder"), this);
  layout->addWidget(migrateBtn);
  connect(migrateBtn, &QPushButton::clicked, this,
          &LegacyImageMigrationBar::migrateRequested);

  auto *dismissBtn = new QPushButton(tr("Not Now"), this);
  layout->addWidget(dismissBtn);
  connect(dismissBtn, &QPushButton::clicked, this,
          &LegacyImageMigrationBar::dismissRequested);

  auto *neverBtn = new QPushButton(tr("Don't Ask Again"), this);
  layout->addWidget(neverBtn);
  connect(neverBtn, &QPushButton::clicked, this, &LegacyImageMigrationBar::neverRequested);

  setImageCount(0);
}

void LegacyImageMigrationBar::setImageCount(int p_count) {
  m_label->setText(tr("This note uses %n image(s) from a deprecated image folder. Moving them to "
                      "the assets folder relocates the files on disk; other notes still linking to "
                      "them will break.",
                      "", p_count));
}
