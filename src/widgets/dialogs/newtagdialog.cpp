#include "newtagdialog.h"

#include <QFormLayout>

#include <notebook/tagi.h>

#include "../lineeditwithsnippet.h"
#include "../widgetsfactory.h"
#include "levellabelwithupbutton.h"

using namespace vnotex;

NewTagDialog::NewTagDialog(TagI *p_tagI, Tag *p_tag, QWidget *p_parent)
    : ScrollDialog(p_parent), m_tagI(p_tagI), m_parentTag(p_tag) {
  setupUI();

  m_nameLineEdit->setFocus();
}

static QVector<LevelLabelWithUpButton::Level> tagToLevels(const Tag *p_tag) {
  QVector<LevelLabelWithUpButton::Level> levels;
  while (p_tag) {
    LevelLabelWithUpButton::Level level;
    level.m_name = p_tag->fetchPath();
    level.m_data = static_cast<const void *>(p_tag);
    levels.push_back(level);
    p_tag = p_tag->getParent();
  }

  // Append an empty level.
  levels.push_back(LevelLabelWithUpButton::Level());

  return levels;
}

void NewTagDialog::setupUI() {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = WidgetsFactory::createFormLayout(mainWidget);

  {
    m_parentTagLabel = new LevelLabelWithUpButton(this);
    m_parentTagLabel->setLevels(tagToLevels(m_parentTag));
    mainLayout->addRow(tr("Location:"), m_parentTagLabel);
  }

  m_nameLineEdit = WidgetsFactory::createLineEditWithSnippet(mainWidget);
  mainLayout->addRow(tr("Name:"), m_nameLineEdit);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Tag"));
}

bool NewTagDialog::validateInputs() {
  bool valid = true;
  QString msg;

  auto name = getTagName();
  if (!Tag::isValidName(name)) {
    valid = false;
    msg = tr("Please specify a valid name for the tag.");
  } else if (m_tagI->findTag(name)) {
    valid = false;
    msg = tr("Name conflicts with existing tag.");
  }

  setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                : ScrollDialog::InformationLevel::Error);
  return valid;
}

bool NewTagDialog::newTag() {
  const Tag *parentTag = static_cast<const Tag *>(m_parentTagLabel->getLevel().m_data);
  const auto parentName = parentTag ? parentTag->name() : QString();
  const auto name = getTagName();
  if (!m_tagI->newTag(name, parentName)) {
    setInformationText(tr("Failed to create tag (%1).").arg(name),
                       ScrollDialog::InformationLevel::Error);
    // Tags maybe updated. Don't allow operation for now.
    setButtonEnabled(QDialogButtonBox::Ok, false);
    return false;
  }
  return true;
}

void NewTagDialog::acceptedButtonClicked() {
  if (validateInputs() && newTag()) {
    accept();
  }
}

QString NewTagDialog::getTagName() const { return m_nameLineEdit->evaluatedText(); }
