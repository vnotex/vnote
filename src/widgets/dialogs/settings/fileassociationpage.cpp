#include "fileassociationpage.h"

#include <QPushButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <QHBoxLayout>
#include <QLabel>

#include <controllers/windowscontextmenucontroller.h>
#include <utils/widgetutils.h>
#endif

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <core/sessionconfig.h>
#include <widgets/lineedit.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

#include "settingspagehelper.h"

using namespace vnotex;

#ifdef Q_OS_WIN
namespace {
constexpr char kContextMenuCardName[] = "systemContextMenuCard";
constexpr char kAddContextMenuButtonName[] = "addToContextMenuButton";
constexpr char kRemoveContextMenuButtonName[] = "removeFromContextMenuButton";
constexpr char kContextMenuStatusName[] = "systemContextMenuStatus";
} // namespace
#endif

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

#ifdef Q_OS_WIN
  setupSystemContextMenu(mainLayout);
#endif

  mainLayout->addStretch();
}

#ifdef Q_OS_WIN
void FileAssociationPage::setupSystemContextMenu(QVBoxLayout *p_layout) {
  m_contextMenuController = m_services.get<WindowsContextMenuController>();
  if (!m_contextMenuController) {
    // The controller registers itself and belongs to the application, not this page.
    m_contextMenuController = new WindowsContextMenuController(m_services);
  }

  const auto sectionTitle = tr("System Context Menu");
  auto *cardLayout = SettingsPageHelper::addSection(p_layout, sectionTitle, QString(), this);
  auto *card = cardLayout->parentWidget();
  card->setObjectName(QLatin1String(kContextMenuCardName));

  auto *actionRow = new QWidget(card);
  actionRow->setProperty(PropertyDefs::c_settingsContainer, true);
  auto *actionLayout = new QVBoxLayout(actionRow);
  actionLayout->setContentsMargins(16, 6, 16, 6);
  auto *buttonsLayout = new QHBoxLayout();
  m_addContextMenuButton = new QPushButton(tr("Add to Context Menu"), actionRow);
  m_addContextMenuButton->setObjectName(QLatin1String(kAddContextMenuButtonName));
  m_removeContextMenuButton = new QPushButton(tr("Remove from Context Menu"), actionRow);
  m_removeContextMenuButton->setObjectName(QLatin1String(kRemoveContextMenuButtonName));
  buttonsLayout->addWidget(m_addContextMenuButton);
  buttonsLayout->addWidget(m_removeContextMenuButton);
  buttonsLayout->addStretch();
  actionLayout->addLayout(buttonsLayout);

  m_contextMenuStatus = new QLabel(actionRow);
  m_contextMenuStatus->setObjectName(QLatin1String(kContextMenuStatusName));
  m_contextMenuStatus->setTextFormat(Qt::PlainText);
  m_contextMenuStatus->setWordWrap(true);
  m_contextMenuStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_contextMenuStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
  actionLayout->addWidget(m_contextMenuStatus);
  cardLayout->addWidget(actionRow);

  addSearchItem(sectionTitle + QLatin1Char(' ') + m_addContextMenuButton->text(),
                m_addContextMenuButton);
  addSearchItem(sectionTitle + QLatin1Char(' ') + m_removeContextMenuButton->text(),
                m_removeContextMenuButton);
  connect(m_addContextMenuButton, &QPushButton::clicked, this,
          [this]() { m_contextMenuController->start(WindowsContextMenuController::Action::Add); });
  connect(m_removeContextMenuButton, &QPushButton::clicked, this, [this]() {
    m_contextMenuController->start(WindowsContextMenuController::Action::Remove);
  });
  connect(m_contextMenuController, &WindowsContextMenuController::stateChanged, this,
          &FileAssociationPage::refreshSystemContextMenu);
  refreshSystemContextMenu();
}

void FileAssociationPage::refreshSystemContextMenu() {
  const bool busy = m_contextMenuController->isBusy();
  m_addContextMenuButton->setEnabled(!busy);
  m_removeContextMenuButton->setEnabled(!busy);
  m_contextMenuStatus->setText(m_contextMenuController->statusText());
  WidgetUtils::setPropertyDynamically(m_contextMenuStatus, PropertyDefs::c_severityText,
                                      m_contextMenuController->hasError() ? QStringLiteral("error")
                                                                          : QStringLiteral("info"));
  m_contextMenuStatus->setVisible(!m_contextMenuStatus->text().isEmpty());
}
#endif

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

    QStringList suffixes = suffixesEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
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
    QStringList suffixes = suffixesEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
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

    const bool isImage = ft.m_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0;
    auto *lineEdit = WidgetsFactory::createLineEdit(this);
    lineEdit->setProperty(c_nameProperty, ft.m_typeName);
    if (isImage) {
      lineEdit->setObjectName(QStringLiteral("imageSuffixesEdit"));
    }
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
  // Clear previous dynamic rows. One owns the Add Program button, so the
  // member must be cleared BEFORE the row loop below: addExternalProgramRow()
  // dereferences it, and it is only re-created further down.
  m_addProgramButton = nullptr;
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

  // Fill the action row with the same themed background as the program rows.
  auto *buttonRow = new QWidget(m_externalCardLayout->parentWidget());
  buttonRow->setProperty(PropertyDefs::c_settingsContainer, true);
  auto *buttonLayout = new QVBoxLayout(buttonRow);
  buttonLayout->setContentsMargins(16, 6, 16, 6);
  m_addProgramButton = new QPushButton(tr("Add Program"), buttonRow);
  connect(m_addProgramButton, &QPushButton::clicked, this, [this]() {
    addExternalProgramRow(QString(), QString(), QString());
    pageIsChanged();
  });
  buttonLayout->addWidget(m_addProgramButton, 0, Qt::AlignLeading);
  m_externalCardLayout->addWidget(buttonRow);

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
  rowWidget->setProperty(PropertyDefs::c_settingsContainer, true);
  rowWidget->setProperty("programRow", true);

  auto *rowLayout = new QVBoxLayout(rowWidget);
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(0);

  auto *nameEdit = WidgetsFactory::createLineEdit(rowWidget);
  nameEdit->setObjectName(QStringLiteral("nameEdit"));
  nameEdit->setPlaceholderText(tr("Program name"));
  nameEdit->setToolTip(tr("Display name for the external program"));
  nameEdit->setText(p_name);
  connect(nameEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *commandEdit = WidgetsFactory::createLineEdit(rowWidget);
  commandEdit->setObjectName(QStringLiteral("commandEdit"));
  commandEdit->setPlaceholderText(tr("Command (%1 = file path)"));
  commandEdit->setToolTip(
      tr("Executable path or command. Use %1 as placeholder for the file path"));
  commandEdit->setText(p_command);
  connect(commandEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *suffixesEdit = WidgetsFactory::createLineEdit(rowWidget);
  suffixesEdit->setObjectName(QStringLiteral("suffixesEdit"));
  suffixesEdit->setPlaceholderText(tr("Suffixes separated by ;"));
  suffixesEdit->setToolTip(
      tr("File suffixes handled by this program, separated by ; (e.g. pdf;djvu)"));
  suffixesEdit->setText(p_suffixes);
  connect(suffixesEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *removeBtn = new QPushButton(tr("Remove"), rowWidget);
  connect(removeBtn, &QPushButton::clicked, this,
          [this, rowWidget]() { removeExternalProgramRow(rowWidget); });

  rowLayout->addWidget(
      SettingsPageHelper::createSettingRow(tr("Program name"), QString(), nameEdit, rowWidget));
  rowLayout->addWidget(
      SettingsPageHelper::createSettingRow(tr("Command (%1 = file path)"), QString(), commandEdit,
                                           rowWidget, SettingsPageHelper::RowLayout::FullWidth));
  rowLayout->addWidget(SettingsPageHelper::createSettingRow(tr("Suffixes separated by ;"),
                                                            QString(), suffixesEdit, rowWidget));

  auto *removeLayout = new QVBoxLayout();
  removeLayout->setContentsMargins(16, 6, 16, 6);
  removeLayout->addWidget(removeBtn, 0, Qt::AlignLeading);
  rowLayout->addLayout(removeLayout);

  // Insert before the Add Program action row if it exists.
  int insertIndex = m_externalCardLayout->count();
  if (m_addProgramButton) {
    insertIndex = m_externalCardLayout->indexOf(m_addProgramButton->parentWidget());
  }
  m_externalCardLayout->insertWidget(insertIndex, rowWidget);
}

void FileAssociationPage::removeExternalProgramRow(QWidget *p_row) {
  m_externalCardLayout->removeWidget(p_row);
  p_row->deleteLater();
  pageIsChanged();
}

void FileAssociationPage::addSystemProgramRow(const QString &p_suffixes) {
  auto *suffixesEdit = WidgetsFactory::createLineEdit(m_externalCardLayout->parentWidget());
  suffixesEdit->setObjectName(QStringLiteral("suffixesEdit"));
  suffixesEdit->setPlaceholderText(tr("Suffixes separated by ;"));
  suffixesEdit->setToolTip(tr("File suffixes to open with the system default application"));
  suffixesEdit->setText(p_suffixes);
  connect(suffixesEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

  auto *rowWidget = SettingsPageHelper::createSettingRow(
      tr("System default app"), QString(), suffixesEdit, m_externalCardLayout->parentWidget());
  rowWidget->setProperty("programRow", true);
  rowWidget->setProperty("systemRow", true);
  rowWidget->setToolTip(tr("Operating system will choose the application to open the given file"));

  // Insert before the Add Program action row if it exists.
  int insertIndex = m_externalCardLayout->count();
  if (m_addProgramButton) {
    insertIndex = m_externalCardLayout->indexOf(m_addProgramButton->parentWidget());
  }
  m_externalCardLayout->insertWidget(insertIndex, rowWidget);
}
