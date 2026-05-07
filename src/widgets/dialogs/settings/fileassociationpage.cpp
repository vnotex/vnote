#include "fileassociationpage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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

  // Save external programs.
  QVector<SessionConfig::ExternalProgram> programs;
  auto *externalCard = m_externalCardLayout->parentWidget();
  const auto rows = externalCard->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
  for (auto *row : rows) {
    if (!row->property("programRow").toBool()) {
      continue;
    }
    if (row->property("systemRow").toBool()) {
      continue;
    }

    auto *nameEdit = row->findChild<QLineEdit *>(QStringLiteral("nameEdit"));
    auto *commandEdit = row->findChild<QLineEdit *>(QStringLiteral("commandEdit"));
    auto *suffixesEdit = row->findChild<QLineEdit *>(QStringLiteral("suffixesEdit"));
    if (!nameEdit || !commandEdit || !suffixesEdit) {
      continue;
    }

    QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
      continue;
    }

    SessionConfig::ExternalProgram prog;
    prog.m_name = name;
    prog.m_command = commandEdit->text().trimmed();

    QStringList suffixes =
        suffixesEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
    for (auto &s : suffixes) {
      s = s.trimmed().toLower();
      while (s.startsWith(QLatin1Char('.'))) {
        s.remove(0, 1);
      }
    }
    suffixes.removeAll(QString());
    prog.m_suffixes = suffixes;

    programs.append(prog);
  }

  // Collect system program row (always appended last).
  for (auto *row : rows) {
    if (!row->property("systemRow").toBool()) {
      continue;
    }
    auto *suffixesEdit = row->findChild<QLineEdit *>(QStringLiteral("suffixesEdit"));
    if (!suffixesEdit) {
      continue;
    }
    SessionConfig::ExternalProgram sysProg;
    sysProg.m_name = SessionConfig::ExternalProgram::c_systemProgramName;
    QStringList suffixes =
        suffixesEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
    for (auto &s : suffixes) {
      s = s.trimmed().toLower();
      while (s.startsWith(QLatin1Char('.'))) {
        s.remove(0, 1);
      }
    }
    suffixes.removeAll(QString());
    sysProg.m_suffixes = suffixes;
    programs.append(sysProg);
    break;
  }

  m_services.get<ConfigMgr2>()->getSessionConfig().setExternalPrograms(programs);

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
  const auto &programs = sessionConfig.getExternalPrograms();

  for (const auto &prog : programs) {
    if (prog.isSystemProgram()) {
      continue;
    }
    addExternalProgramRow(prog.m_name, prog.m_command, prog.m_suffixes.join(c_suffixSeparator));
  }

  // Add Program button.
  m_addProgramButton = new QPushButton(tr("Add Program"), this);
  connect(m_addProgramButton, &QPushButton::clicked, this, [this]() {
    addExternalProgramRow(QString(), QString(), QString());
    pageIsChanged();
  });
  m_externalCardLayout->addWidget(m_addProgramButton);

  // System program row — always last, before the Add button.
  for (const auto &prog : programs) {
    if (prog.isSystemProgram()) {
      addSystemProgramRow(prog.m_suffixes.join(c_suffixSeparator));
      break;
    }
  }
}

void FileAssociationPage::addExternalProgramRow(const QString &p_name, const QString &p_command,
                                                const QString &p_suffixes) {
  auto *rowWidget = new QWidget(m_externalCardLayout->parentWidget());
  rowWidget->setProperty("programRow", true);

  auto *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(0, 0, 0, 0);

  auto *nameEdit = WidgetsFactory::createLineEdit(rowWidget);
  nameEdit->setObjectName(QStringLiteral("nameEdit"));
  nameEdit->setPlaceholderText(tr("Program name"));
  nameEdit->setToolTip(tr("Display name for the external program"));
  nameEdit->setText(p_name);
  connect(nameEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *commandEdit = WidgetsFactory::createLineEdit(rowWidget);
  commandEdit->setObjectName(QStringLiteral("commandEdit"));
  commandEdit->setPlaceholderText(tr("Command (%1 = file path)"));
  commandEdit->setToolTip(tr("Executable path or command. Use %1 as placeholder for the file path"));
  commandEdit->setText(p_command);
  connect(commandEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *suffixesEdit = WidgetsFactory::createLineEdit(rowWidget);
  suffixesEdit->setObjectName(QStringLiteral("suffixesEdit"));
  suffixesEdit->setPlaceholderText(tr("Suffixes separated by ;"));
  suffixesEdit->setToolTip(tr("File suffixes handled by this program, separated by ; (e.g. pdf;djvu)"));
  suffixesEdit->setText(p_suffixes);
  connect(suffixesEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *removeBtn = new QPushButton(tr("Remove"), rowWidget);
  connect(removeBtn, &QPushButton::clicked, this, [this, rowWidget]() {
    removeExternalProgramRow(rowWidget);
  });

  rowLayout->addWidget(nameEdit, 2);
  rowLayout->addWidget(commandEdit, 3);
  rowLayout->addWidget(suffixesEdit, 2);
  rowLayout->addWidget(removeBtn, 0);

  // Insert before the Add Program button if it exists.
  int insertIndex = m_externalCardLayout->count();
  if (m_addProgramButton) {
    insertIndex = m_externalCardLayout->indexOf(m_addProgramButton);
  }
  m_externalCardLayout->insertWidget(insertIndex, rowWidget);
}

void FileAssociationPage::removeExternalProgramRow(QWidget *p_row) {
  m_externalCardLayout->removeWidget(p_row);
  p_row->deleteLater();
  pageIsChanged();
}

void FileAssociationPage::addSystemProgramRow(const QString &p_suffixes) {
  auto *rowWidget = new QWidget(m_externalCardLayout->parentWidget());
  rowWidget->setProperty("programRow", true);
  rowWidget->setProperty("systemRow", true);

  auto *rowLayout = new QHBoxLayout(rowWidget);
  rowLayout->setContentsMargins(0, 0, 0, 0);

  auto *nameLabel = new QLabel(tr("System Default App"), rowWidget);
  nameLabel->setToolTip(tr("Built-in program that opens files with the OS default application"));

  auto *suffixesEdit = WidgetsFactory::createLineEdit(rowWidget);
  suffixesEdit->setObjectName(QStringLiteral("suffixesEdit"));
  suffixesEdit->setPlaceholderText(tr("Suffixes separated by ;"));
  suffixesEdit->setToolTip(tr("File suffixes to open with the system default application"));
  suffixesEdit->setText(p_suffixes);
  connect(suffixesEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  rowLayout->addWidget(nameLabel, 2);
  rowLayout->addWidget(suffixesEdit, 5);

  // Insert before the Add Program button if it exists.
  int insertIndex = m_externalCardLayout->count();
  if (m_addProgramButton) {
    insertIndex = m_externalCardLayout->indexOf(m_addProgramButton);
  }
  m_externalCardLayout->insertWidget(insertIndex, rowWidget);
}
