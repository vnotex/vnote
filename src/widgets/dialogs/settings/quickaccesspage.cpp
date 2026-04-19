#include "quickaccesspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <core/sessionconfig.h>
#include <utils/widgetutils.h>
#include <widgets/lineeditwithsnippet.h>
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/messageboxhelper.h>
#include <widgets/widgetsfactory.h>

#include "newquickaccessitemdialog.h"
#include "settingspagehelper.h"

// LEGACY: NoteTemplateSelector now requires ServiceLocator - disabled until migration
// LEGACY: NotebookMgr not yet in ServiceLocator - getCurrentNotebook disabled

using namespace vnotex;

QuickAccessPage::QuickAccessPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void QuickAccessPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  // Quick Access section.
  {
    auto *cardLayout =
        SettingsPageHelper::addSection(mainLayout, tr("Quick Access"), QString(), this);

    // New button row.
    {
      auto *headerWidget = new QWidget(this);
      auto *headerLayout = new QHBoxLayout(headerWidget);
      headerLayout->setContentsMargins(16, 6, 16, 6);
      headerLayout->addStretch();

      auto *newBtn = new QPushButton(tr("New"), this);
      connect(newBtn, &QPushButton::clicked, this, &QuickAccessPage::newQuickAccessItem);
      headerLayout->addWidget(newBtn);

      cardLayout->addWidget(headerWidget);
    }

    // Text edit.
    {
      m_quickAccessTextEdit = WidgetsFactory::createPlainTextEdit(this);
      m_quickAccessTextEdit->setPlaceholderText(tr("One file per line: file_path, open_mode"));
      m_quickAccessTextEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

      auto *editWrapper = new QWidget(this);
      auto *editLayout = new QVBoxLayout(editWrapper);
      editLayout->setContentsMargins(16, 0, 16, 6);
      editLayout->addWidget(m_quickAccessTextEdit);
      cardLayout->addWidget(editWrapper);

      const QString searchLabel(tr("Quick Access:"));
      addSearchItem(searchLabel, m_quickAccessTextEdit->placeholderText(), m_quickAccessTextEdit);
      connect(m_quickAccessTextEdit, &QPlainTextEdit::textChanged, this,
              &QuickAccessPage::pageIsChanged);
    }
  }

  // Quick Note section.
  {
    auto *cardLayout =
        SettingsPageHelper::addSection(mainLayout, tr("Quick Note"), QString(), this);

    // Scheme selector.
    {
      auto *selectorWidget = new QWidget(this);
      auto *selectorLayout = new QHBoxLayout(selectorWidget);
      selectorLayout->setContentsMargins(0, 0, 0, 0);

      m_quickNoteSchemeComboBox = WidgetsFactory::createComboBox(this);
      selectorLayout->addWidget(m_quickNoteSchemeComboBox, 1);
      m_quickNoteSchemeComboBox->setPlaceholderText(tr("No scheme to show"));

      auto *newBtn = new QPushButton(tr("New"), this);
      connect(newBtn, &QPushButton::clicked, this, &QuickAccessPage::newQuickNoteScheme);
      selectorLayout->addWidget(newBtn);

      auto *deleteBtn = new QPushButton(tr("Delete"), this);
      deleteBtn->setEnabled(false);
      connect(deleteBtn, &QPushButton::clicked, this, &QuickAccessPage::removeQuickNoteScheme);
      selectorLayout->addWidget(deleteBtn);

      const QString label(tr("Scheme:"));
      auto *row = SettingsPageHelper::createSettingRow(label, QString(), selectorWidget, this);
      cardLayout->addWidget(row);
      addSearchItem(label, m_quickNoteSchemeComboBox);

      connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              &QuickAccessPage::pageIsChanged);
      connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [deleteBtn](int idx) { deleteBtn->setEnabled(idx > -1); });
    }

    // Quick note info section (shown/hidden based on scheme selection).
    {
      cardLayout->addWidget(SettingsPageHelper::createSeparator(this));

      m_quickNoteInfoGroupBox = new QGroupBox(this);
      auto *infoLayout = WidgetsFactory::createFormLayout(m_quickNoteInfoGroupBox);

      {
        const QString label(tr("Folder:"));
        m_quickNoteFolderPathInput = new LocationInputWithBrowseButton(m_quickNoteInfoGroupBox);
        m_quickNoteFolderPathInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                                  tr("Select Quick Note Folder"));
        m_quickNoteFolderPathInput->setPlaceholderText(
            tr("Empty to use current explored folder dynamically"));
        infoLayout->addRow(label, m_quickNoteFolderPathInput);
        addSearchItem(label, m_quickNoteFolderPathInput);
        connect(m_quickNoteFolderPathInput, &LocationInputWithBrowseButton::textChanged, this,
                &QuickAccessPage::pageIsChanged);
      }

      {
        const QString label(tr("Note name:"));
        auto *snippetService = m_services.get<SnippetCoreService>();
        m_quickNoteNoteNameLineEdit =
            WidgetsFactory::createLineEditWithSnippet(snippetService, m_quickNoteInfoGroupBox);
        infoLayout->addRow(label, m_quickNoteNoteNameLineEdit);
        connect(m_quickNoteNoteNameLineEdit, &QLineEdit::textChanged, this,
                &QuickAccessPage::pageIsChanged);
      }

      // LEGACY: NoteTemplateSelector now requires ServiceLocator

      m_quickNoteInfoGroupBox->setVisible(false);
      cardLayout->addWidget(m_quickNoteInfoGroupBox);

      connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [this](int idx) {
                if (isLoading()) {
                  return;
                }
                setCurrentQuickNote(idx);
              });
    }
  }

  mainLayout->addStretch();
}

void QuickAccessPage::loadInternal() {
  const auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  {
    const auto &quickAccess = sessionConfig.getQuickAccessItems();
    // Build path->UUID map so UUIDs survive the text-edit round-trip.
    m_quickAccessUuidMap.clear();
    for (const auto &item : quickAccess) {
      if (!item.m_uuid.isEmpty()) {
        m_quickAccessUuidMap.insert(item.m_path, item.m_uuid);
      }
    }
    if (!quickAccess.isEmpty()) {
      m_quickAccessTextEdit->setPlainText(formatQuickAccessItems(quickAccess));
    }
  }

  loadQuickNoteSchemes();
}

void QuickAccessPage::loadQuickNoteSchemes() {
  const auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  m_quickNoteSchemes = sessionConfig.getQuickNoteSchemes();
  m_quickNoteCurrentIndex = -1;

  m_quickNoteSchemeComboBox->clear();
  for (const auto &scheme : m_quickNoteSchemes) {
    m_quickNoteSchemeComboBox->addItem(scheme.m_name);
  }
  if (m_quickNoteSchemeComboBox->count() > 0) {
    m_quickNoteSchemeComboBox->setCurrentIndex(0);
    // Manually call the handler.
    setCurrentQuickNote(0);
  }
}

bool QuickAccessPage::saveInternal() {
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  {
    auto text = m_quickAccessTextEdit->toPlainText();
    if (!text.isEmpty()) {
      auto items = parseQuickAccessText(text);
      // Restore UUIDs from the hidden map.
      for (auto &item : items) {
        auto it = m_quickAccessUuidMap.constFind(item.m_path);
        if (it != m_quickAccessUuidMap.constEnd()) {
          item.m_uuid = it.value();
        }
      }
      sessionConfig.setQuickAccessItems(items);
    }
  }

  saveQuickNoteSchemes();

  return true;
}

void QuickAccessPage::saveQuickNoteSchemes() {
  // Save current quick note scheme from inputs.
  saveCurrentQuickNote();

  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
  sessionConfig.setQuickNoteSchemes(m_quickNoteSchemes);
}

QString QuickAccessPage::title() const { return tr("Quick Access"); }

QString QuickAccessPage::slug() const { return QStringLiteral("quickaccess"); }

QString QuickAccessPage::getDefaultQuickNoteFolderPath() {
  // LEGACY: NotebookMgr not yet in ServiceLocator - cannot get current notebook path
  return QDir::homePath();
}

void QuickAccessPage::newQuickAccessItem() {
  NewQuickAccessItemDialog dialog(m_services, this);
  if (dialog.exec() == QDialog::Accepted) {
    auto item = dialog.getItem();
    // Store UUID in hidden map so it survives the text round-trip.
    if (!item.m_uuid.isEmpty()) {
      m_quickAccessUuidMap.insert(item.m_path, item.m_uuid);
    }
    QString line;
    if (item.m_openMode == QuickAccessOpenMode::Default) {
      line = item.m_path;
    } else {
      line = item.m_path + QStringLiteral(",") + SessionConfig::openModeToString(item.m_openMode);
    }

    auto text = m_quickAccessTextEdit->toPlainText();
    if (!text.isEmpty() && !text.endsWith(QChar('\n'))) {
      text += QChar('\n');
    }
    text += line;
    m_quickAccessTextEdit->setPlainText(text);
    emit pageIsChanged();
  }
}

QVector<SessionConfig::QuickAccessItem>
QuickAccessPage::parseQuickAccessText(const QString &p_text) {
  QVector<SessionConfig::QuickAccessItem> items;
  const auto lines = p_text.split(QChar('\n'));
  for (const auto &rawLine : lines) {
    auto line = rawLine.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    SessionConfig::QuickAccessItem item;
    int lastComma = line.lastIndexOf(QChar(','));
    if (lastComma >= 0) {
      auto suffix = line.mid(lastComma + 1).trimmed().toLower();
      if (suffix == QStringLiteral("default") || suffix == QStringLiteral("read") ||
          suffix == QStringLiteral("edit")) {
        item.m_path = line.left(lastComma).trimmed();
        item.m_openMode = SessionConfig::stringToOpenMode(suffix);
      } else {
        // Unrecognized suffix — entire line is the path.
        item.m_path = line;
        item.m_openMode = QuickAccessOpenMode::Default;
      }
    } else {
      // No comma — entire line is the path.
      item.m_path = line;
      item.m_openMode = QuickAccessOpenMode::Default;
    }

    if (!item.m_path.isEmpty()) {
      items.append(item);
    }
  }
  return items;
}

QString
QuickAccessPage::formatQuickAccessItems(const QVector<SessionConfig::QuickAccessItem> &p_items) {
  QStringList lines;
  for (const auto &item : p_items) {
    if (item.m_openMode == QuickAccessOpenMode::Default) {
      lines << item.m_path;
    } else {
      lines << item.m_path + QStringLiteral(",") + SessionConfig::openModeToString(item.m_openMode);
    }
  }
  return lines.join(QChar('\n'));
}

void QuickAccessPage::newQuickNoteScheme() {
  bool isDuplicated = false;
  QString schemeName;
  do {
    schemeName = QInputDialog::getText(this, tr("Quick Note Scheme"),
                                       isDuplicated ? tr("Scheme name already exists! Try again:")
                                                    : tr("Scheme name:"));
    if (schemeName.isEmpty()) {
      return;
    }
    isDuplicated = m_quickNoteSchemeComboBox->findText(schemeName) != -1;
  } while (isDuplicated);

  SessionConfig::QuickNoteScheme scheme;
  scheme.m_name = schemeName;
  scheme.m_folderPath = getDefaultQuickNoteFolderPath();
  scheme.m_noteName = tr("quick_note_%da%.md");
  m_quickNoteSchemes.push_back(scheme);

  m_quickNoteSchemeComboBox->addItem(schemeName);
  m_quickNoteSchemeComboBox->setCurrentText(schemeName);

  emit pageIsChanged();
}

void QuickAccessPage::removeQuickNoteScheme() {
  int idx = m_quickNoteSchemeComboBox->currentIndex();
  Q_ASSERT(idx > -1);

  auto &scheme = m_quickNoteSchemes[idx];
  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Type::Question, tr("Delete quick note scheme (%1)?").arg(scheme.m_name));
  if (ret != QMessageBox::Ok) {
    return;
  }

  m_quickNoteCurrentIndex = -1;
  m_quickNoteSchemes.removeAt(idx);
  m_quickNoteSchemeComboBox->removeItem(idx);
  emit pageIsChanged();
}

void QuickAccessPage::saveCurrentQuickNote() {
  if (m_quickNoteCurrentIndex < 0) {
    return;
  }
  Q_ASSERT(m_quickNoteCurrentIndex < m_quickNoteSchemes.size());
  auto &scheme = m_quickNoteSchemes[m_quickNoteCurrentIndex];
  scheme.m_folderPath = m_quickNoteFolderPathInput->text();
  // No need to apply the snippet for now.
  scheme.m_noteName = m_quickNoteNoteNameLineEdit->text();
  // LEGACY: m_quickNoteTemplateSelector disabled - requires ServiceLocator
}

void QuickAccessPage::loadCurrentQuickNote() {
  if (m_quickNoteCurrentIndex < 0) {
    m_quickNoteFolderPathInput->setText(QString());
    m_quickNoteNoteNameLineEdit->setText(QString());
    // LEGACY: m_quickNoteTemplateSelector disabled - requires ServiceLocator
    return;
  }

  Q_ASSERT(m_quickNoteCurrentIndex < m_quickNoteSchemes.size());
  const auto &scheme = m_quickNoteSchemes[m_quickNoteCurrentIndex];
  m_quickNoteFolderPathInput->setText(scheme.m_folderPath);
  m_quickNoteNoteNameLineEdit->setText(scheme.m_noteName);
  // LEGACY: m_quickNoteTemplateSelector disabled - requires ServiceLocator
}

void QuickAccessPage::setCurrentQuickNote(int idx) {
  saveCurrentQuickNote();
  m_quickNoteCurrentIndex = idx;
  loadCurrentQuickNote();

  m_quickNoteInfoGroupBox->setVisible(idx > -1);
}
