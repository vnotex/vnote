#include "wordcountpanel.h"

#include <QFormLayout>
#include <QLabel>

using namespace vnotex;

WordCountPanel::WordCountPanel(QWidget *p_parent) : QWidget(p_parent) {
  auto mainLayout = new QFormLayout(this);

  m_selectionLabel = new QLabel(tr("Selection Area"), this);
  mainLayout->addRow(m_selectionLabel);
  m_selectionLabel->hide();

  const auto alignment = Qt::AlignRight | Qt::AlignVCenter;
  m_wordLabel = new QLabel("0", this);
  m_wordLabel->setAlignment(alignment);
  mainLayout->addRow(tr("Words"), m_wordLabel);

  m_charWithoutSpaceLabel = new QLabel("0", this);
  m_charWithoutSpaceLabel->setAlignment(alignment);
  mainLayout->addRow(tr("Characters (no spaces)"), m_charWithoutSpaceLabel);

  m_charWithSpaceLabel = new QLabel("0", this);
  m_charWithSpaceLabel->setAlignment(alignment);
  mainLayout->addRow(tr("Characters (with spaces)"), m_charWithSpaceLabel);
}

void WordCountPanel::updateCount(bool p_isSelection, int p_words, int p_charsWithoutSpace,
                                 int p_charsWithSpace) {
  m_selectionLabel->setVisible(p_isSelection);
  m_wordLabel->setText(QString::number(p_words));
  m_charWithoutSpaceLabel->setText(QString::number(p_charsWithoutSpace));
  m_charWithSpaceLabel->setText(QString::number(p_charsWithSpace));
}

WordCountInfo WordCountPanel::calculateWordCount(const QString &p_text) {
  WordCountInfo info;

  // Char without spaces.
  int cns = 0;
  int wc = 0;
  // Remove th ending new line.
  int cc = p_text.size();
  // 0 - not in word;
  // 1 - in English word;
  // 2 - in non-English word;
  int state = 0;

  for (int i = 0; i < cc; ++i) {
    QChar ch = p_text[i];
    if (ch.isSpace()) {
      if (state) {
        state = 0;
      }

      continue;
    } else if (ch.unicode() < 128) {
      if (state != 1) {
        state = 1;
        ++wc;
      }
    } else {
      state = 2;
      ++wc;
    }

    ++cns;
  }

  info.m_wordCount = wc;
  info.m_charWithoutSpaceCount = cns;
  info.m_charWithSpaceCount = cc;
  return info;
}
