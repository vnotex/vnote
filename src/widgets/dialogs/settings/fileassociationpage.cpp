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
  {
    auto *cardLayout =
        SettingsPageHelper::addSection(mainLayout, tr("Built-In File Types"), QString(), this);

    m_builtInContainer = new QWidget(this);
    new QVBoxLayout(m_builtInContainer);
    cardLayout->addWidget(m_builtInContainer);
  }

  // External Programs card.
  {
    auto *cardLayout =
        SettingsPageHelper::addSection(mainLayout, tr("External Programs"), QString(), this);

    m_externalContainer = new QWidget(this);
    new QVBoxLayout(m_externalContainer);
    cardLayout->addWidget(m_externalContainer);
  }

  mainLayout->addStretch();
}

void FileAssociationPage::loadInternal() {
  loadBuiltInTypesGroup(m_builtInContainer);

  loadExternalProgramsGroup(m_externalContainer);
}

bool FileAssociationPage::saveInternal() {
  auto *ftService = m_services.get<FileTypeCoreService>();
  Q_ASSERT(ftService);
  auto types = ftService->getAllFileTypes();

  // Update suffixes from UI line edits.
  const auto lineEdits = m_builtInContainer->findChildren<QLineEdit *>();
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

void FileAssociationPage::loadBuiltInTypesGroup(QWidget *p_container) {
  auto *layout = qobject_cast<QVBoxLayout *>(p_container->layout());

  // Clear previous children.
  while (layout->count() > 0) {
    auto *item = layout->takeAt(0);
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

    auto *lineEdit = WidgetsFactory::createLineEdit(p_container);
    lineEdit->setProperty(c_nameProperty, ft.m_typeName);
    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(tr("List of suffixes for this file type"));
    lineEdit->setText(ft.m_suffixes.join(c_suffixSeparator));
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    if (!firstRow) {
      layout->addWidget(SettingsPageHelper::createSeparator(p_container));
    }
    layout->addWidget(
        SettingsPageHelper::createSettingRow(ft.m_displayName, QString(), lineEdit, p_container));

    firstRow = false;
  }
}

void FileAssociationPage::loadExternalProgramsGroup(QWidget *p_container) {
  auto *layout = qobject_cast<QVBoxLayout *>(p_container->layout());

  // Clear previous children.
  while (layout->count() > 0) {
    auto *item = layout->takeAt(0);
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
    auto *lineEdit = WidgetsFactory::createLineEdit(p_container);
    lineEdit->setProperty(c_nameProperty, name);
    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(
        tr("List of suffixes to open with external program (or system default program)"));
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    // TODO: External program suffix associations are not yet managed by vxcore.
    // Suffix editing is shown but not persisted until session config migration.

    if (!firstRow) {
      layout->addWidget(SettingsPageHelper::createSeparator(p_container));
    }
    layout->addWidget(SettingsPageHelper::createSettingRow(name, QString(), lineEdit, p_container));

    firstRow = false;
  }
}
