#include "levellabelwithupbutton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <core/vnotex.h>
#include <utils/iconutils.h>

using namespace vnotex;

LevelLabelWithUpButton::LevelLabelWithUpButton(QWidget *p_parent) : QWidget(p_parent) { setupUI(); }

void LevelLabelWithUpButton::setupUI() {
  auto mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  m_label = new QLabel(this);
  mainLayout->addWidget(m_label, 1);

  const auto iconFile = VNoteX::getInst().getThemeMgr().getIconFile("up_level.svg");
  m_upButton = new QPushButton(IconUtils::fetchIconWithDisabledState(iconFile), tr("Up"), this);
  m_upButton->setToolTip(tr("Go one level up"));
  connect(m_upButton, &QPushButton::clicked, this, [this]() {
    if (m_levelIdx < m_levels.size() - 1) {
      ++m_levelIdx;
      updateLabelAndButton();
      emit levelChanged();
    }
  });
  mainLayout->addWidget(m_upButton, 0);

  updateLabelAndButton();
}

void LevelLabelWithUpButton::updateLabelAndButton() {
  if (m_levels.isEmpty()) {
    m_label->clear();
  } else {
    Q_ASSERT(m_levelIdx < m_levels.size());
    m_label->setText(m_levels[m_levelIdx].m_name);
  }

  m_upButton->setVisible(!m_readOnly && (m_levelIdx < m_levels.size() - 1));
}

const LevelLabelWithUpButton::Level &LevelLabelWithUpButton::getLevel() const {
  Q_ASSERT(m_levelIdx < m_levels.size());
  return m_levels[m_levelIdx];
}

void LevelLabelWithUpButton::setLevels(const QVector<Level> &p_levels) {
  m_levels = p_levels;
  Q_ASSERT(!m_levels.isEmpty());
  m_levelIdx = 0;

  updateLabelAndButton();
  emit levelChanged();
}

void LevelLabelWithUpButton::setReadOnly(bool p_readonly) {
  if (m_readOnly == p_readonly) {
    return;
  }

  m_readOnly = p_readonly;
  updateLabelAndButton();
}
