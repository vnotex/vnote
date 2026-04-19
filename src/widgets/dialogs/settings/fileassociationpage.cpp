#include "fileassociationpage.h"

#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <core/sessionconfig.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>

#include "settingspagehelper.h"

using namespace vnotex;

const char *FileAssociationPage::c_nameProperty = "name";

const QChar FileAssociationPage::c_suffixSeparator = QLatin1Char(';');

FileAssociationPage::FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void FileAssociationPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  // Built-In File Types card.
  m_builtInCardLayout =
      SettingsPageHelper::addSection(mainLayout, tr("Built-In File Types"), QString(), this);
  m_builtInRowStartIndex = m_builtInCardLayout->count();

  // External Programs card.
  m_externalCardLayout =
      SettingsPageHelper::addSection(mainLayout, tr("External Programs"), QString(), this);
  m_externalRowStartIndex = m_externalCardLayout->count();

  mainLayout->addStretch();
}

void FileAssociationPage::loadInternal() {
  loadBuiltInTypesGroup();

  loadExternalProgramsGroup();
}

bool FileAssociationPage::saveInternal() {
  auto *ftService = m_services.get<FileTypeCoreService>();
  Q_ASSERT(ftService);
  auto types = ftService->getAllFileTypes();

  // Update suffixes from UI line edits.
  auto *builtInCard = m_builtInCardLayout->parentWidget();
  const auto lineEdits = builtInCard->findChildren<QLineEdit *>();
  for (auto *lineEdit : lineEdits) {
    QString name = lineEdit->property(c_nameProperty).toString();
    if (name.isEmpty()) {
      continue;
    }

    QStringList suffixes = lineEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
    for (auto &s : suffixes) {
      s = s.trimmed().toLower();
    }
    suffixes.removeAll(QString());

    for (auto &ft : types) {
      if (ft.m_typeName == name) {
        ft.m_suffixes = suffixes;
        break;
      }
    }
  }

  if (!ftService->setAllFileTypes(types)) {
    setError(tr("Failed to save file type associations."));
    return false;
  }

  return true;
}

QString FileAssociationPage::title() const { return tr("File Associations"); }

QString FileAssociationPage::slug() const { return QStringLiteral("fileassociation"); }

void FileAssociationPage::loadBuiltInTypesGroup() {
  // Clear previous dynamic rows.
  while (m_builtInCardLayout->count() > m_builtInRowStartIndex) {
    auto *item = m_builtInCardLayout->takeAt(m_builtInCardLayout->count() - 1);
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  auto *ftService = m_services.get<FileTypeCoreService>();
  Q_ASSERT(ftService);
  const auto types = ftService->getAllFileTypes();

  bool firstRow = true;
  for (const auto &ft : types) {
    if (ft.m_typeName == "Others") {
      continue;
    }

    auto *lineEdit = WidgetsFactory::createLineEdit(this);
    lineEdit->setProperty(c_nameProperty, ft.m_typeName);
    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(tr("List of suffixes for this file type"));
    lineEdit->setText(ft.m_suffixes.join(c_suffixSeparator));
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    if (!firstRow) {
      m_builtInCardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    }
    m_builtInCardLayout->addWidget(
        SettingsPageHelper::createSettingRow(ft.m_displayName, QString(), lineEdit, this));

    firstRow = false;
  }
}

void FileAssociationPage::loadExternalProgramsGroup() {
  // Clear previous dynamic rows.
  while (m_externalCardLayout->count() > m_externalRowStartIndex) {
    auto *item = m_externalCardLayout->takeAt(m_externalCardLayout->count() - 1);
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  const auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  QStringList names;
  for (const auto &pro : sessionConfig.getExternalPrograms()) {
    names.push_back(pro.m_name);
  }

  // "System Default Program" label.
  static const QString c_systemDefaultProgram = tr("System Default Program");
  names << c_systemDefaultProgram;

  bool firstRow = true;
  for (const auto &name : names) {
    auto *lineEdit = WidgetsFactory::createLineEdit(this);
    lineEdit->setProperty(c_nameProperty, name);
    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(
        tr("List of suffixes to open with external program (or system default program)"));
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    // TODO: External program suffix associations are not yet managed by vxcore.
    // Suffix editing is shown but not persisted until session config migration.

    if (!firstRow) {
      m_externalCardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    }
    m_externalCardLayout->addWidget(
        SettingsPageHelper::createSettingRow(name, QString(), lineEdit, this));

    firstRow = false;
  }
}
