#include "notebookinfowidget.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUrl>

#include <controllers/managenotebookscontroller.h>
#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <core/sessionconfig.h>
#include <utils/pathutils.h>
#include <widgets/lineeditwithsnippet.h>
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NotebookInfoWidget::NotebookInfoWidget(ServiceLocator &p_services, Mode p_mode, QWidget *p_parent)
    : QWidget(p_parent), m_mode(p_mode), m_hasNotebook(p_mode == Mode::Create) {
  setupUI(p_services);
  updateEditability();
}

void NotebookInfoWidget::setupUI(ServiceLocator &p_services) {
  auto *layout = WidgetsFactory::createFormLayout(this);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

  if (m_mode == Mode::Create) {
    m_nameEdit =
        WidgetsFactory::createLineEditWithSnippet(p_services.get<SnippetCoreService>(), this);
  } else {
    m_nameEdit = WidgetsFactory::createLineEdit(this);
  }
  m_nameEdit->setObjectName(QStringLiteral("notebookNameEdit"));
  m_nameEdit->setPlaceholderText(tr("Notebook name"));
  layout->addRow(tr("Name"), m_nameEdit);
  connect(m_nameEdit, &QLineEdit::textChanged, this, &NotebookInfoWidget::inputEdited);
  setFocusProxy(m_nameEdit);

  m_descriptionEdit = new QPlainTextEdit(this);
  m_descriptionEdit->setObjectName(QStringLiteral("notebookDescriptionEdit"));
  m_descriptionEdit->setPlaceholderText(tr("Optional description for the notebook"));
  m_descriptionEdit->setMaximumHeight(100);
  layout->addRow(tr("Description"), m_descriptionEdit);
  connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this, &NotebookInfoWidget::inputEdited);

  auto *rootWidget = new QWidget(this);
  auto *rootLayout = new QHBoxLayout(rootWidget);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  QString defaultRootPath;
  if (m_mode == Mode::Create) {
    defaultRootPath =
        p_services.get<ConfigMgr2>()->getSessionConfig().getNewNotebookDefaultRootFolderPath();
  }
  m_rootFolderInput = new LocationInputWithBrowseButton(rootWidget, defaultRootPath);
  m_rootFolderInput->setObjectName(QStringLiteral("rootFolderInput"));
  m_rootFolderInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                   tr("Select Notebook Root Folder"));
  m_rootFolderInput->setPlaceholderText(tr("Select a folder as notebook root"));
  rootLayout->addWidget(m_rootFolderInput, 1);
  m_openRootFolderButton = new QPushButton(tr("Open"), rootWidget);
  m_openRootFolderButton->setToolTip(tr("Open root folder in file explorer"));
  m_openRootFolderButton->setVisible(m_mode == Mode::Edit);
  rootLayout->addWidget(m_openRootFolderButton);
  connect(m_openRootFolderButton, &QPushButton::clicked, this, [this]() {
    if (!getRootFolder().isEmpty()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(getRootFolder()));
    }
  });
  connect(m_rootFolderInput, &LocationInputWithBrowseButton::textChanged, this, [this]() {
    if (m_mode == Mode::Create && m_nameEdit->text().isEmpty()) {
      const auto path = getRootFolder().trimmed();
      if (!path.isEmpty()) {
        m_nameEdit->setText(PathUtils::dirName(path));
      }
    }
    emit inputEdited();
  });
  layout->addRow(tr("Root folder"), rootWidget);

  m_typeComboBox = WidgetsFactory::createComboBox(this);
  m_typeComboBox->setObjectName(QStringLiteral("notebookTypeComboBox"));
  m_typeComboBox->addItem(tr("Bundled notebook"), static_cast<int>(NotebookType::Bundled));
  m_typeComboBox->addItem(tr("Raw notebook"), static_cast<int>(NotebookType::Raw));
  m_typeComboBox->setToolTip(tr("Bundled: notebook with metadata stored in config files.\n"
                                "Raw: plain folder structure with minimal VNote metadata"));
  layout->addRow(tr("Type"), m_typeComboBox);

  auto *advancedToggle = new QToolButton(this);
  advancedToggle->setText(tr("Advanced"));
  advancedToggle->setCheckable(true);
  advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  const bool expanded = m_mode == Mode::Edit;
  advancedToggle->setChecked(expanded);
  advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  layout->addRow(advancedToggle);
  auto *advanced = new QWidget(this);
  auto *advancedLayout = WidgetsFactory::createFormLayout(advanced);

  m_assetsFolderEdit = WidgetsFactory::createLineEdit(advanced);
  m_assetsFolderEdit->setObjectName(QStringLiteral("assetsFolderEdit"));
  m_assetsFolderEdit->setPlaceholderText(QStringLiteral("vx_assets"));
  m_assetsFolderEdit->setToolTip(
      tr("Name or path for the assets folder.\n"
         "Can be a folder name (vx_assets), relative path, or absolute path.\n"
         "Relative paths resolve against each note file's parent directory.\n"
         "Empty uses vx_assets.\n"
         "Changing this setting does not move existing assets, attachments, or comments"));
  advancedLayout->addRow(tr("Assets folder"), m_assetsFolderEdit);
  connect(m_assetsFolderEdit, &QLineEdit::textChanged, this, &NotebookInfoWidget::inputEdited);

  m_recycleBinFolderInput = new LocationInputWithBrowseButton(advanced);
  m_recycleBinFolderInput->setObjectName(QStringLiteral("recycleBinFolderInput"));
  m_recycleBinFolderInput->setBrowseType(LocationInputWithBrowseButton::Folder,
                                         tr("Select Recycle Bin Folder"));
  m_recycleBinFolderInput->setPlaceholderText(QStringLiteral("vx_notebook/recycle_bin"));
  m_recycleBinFolderInput->setToolTip(
      tr("Absolute path, or a path relative to the notebook root. Empty uses "
         "vx_notebook/recycle_bin"));
  advancedLayout->addRow(tr("Recycle bin folder"), m_recycleBinFolderInput);
  connect(m_recycleBinFolderInput, &LocationInputWithBrowseButton::textChanged, this,
          &NotebookInfoWidget::inputEdited);

  m_lineEndingComboBox = WidgetsFactory::createComboBox(advanced);
  m_lineEndingComboBox->setObjectName(QStringLiteral("lineEndingComboBox"));
  m_lineEndingComboBox->addItem(tr("Use global editor setting"), QString());
  m_lineEndingComboBox->addItem(tr("LF (Linux/macOS)"), QStringLiteral("lf"));
  m_lineEndingComboBox->addItem(tr("CR LF (Windows)"), QStringLiteral("crlf"));
  m_lineEndingComboBox->addItem(tr("CR"), QStringLiteral("cr"));
  m_lineEndingComboBox->setToolTip(
      tr("Used when saving note content with built-in editors; existing notes are not converted "
         "until edited and saved"));
  advancedLayout->addRow(tr("Line ending"), m_lineEndingComboBox);
  connect(m_lineEndingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &NotebookInfoWidget::inputEdited);
  if (m_mode == Mode::Create) {
    m_assetsFolderEdit->setText(QStringLiteral("vx_assets"));
    m_recycleBinFolderInput->setText(QStringLiteral("vx_notebook/recycle_bin"));
  } else {
    m_typeComboBox->setCurrentIndex(-1);
  }

  layout->addRow(advanced);
  advanced->setVisible(expanded);
  connect(advancedToggle, &QToolButton::toggled, this, [advanced, advancedToggle](bool p_expanded) {
    advanced->setVisible(p_expanded);
    advancedToggle->setArrowType(p_expanded ? Qt::DownArrow : Qt::RightArrow);
  });
  connect(m_typeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
    if (m_mode == Mode::Create && getType() == NotebookType::Raw) {
      m_lineEndingComboBox->setCurrentIndex(0);
    }
    updateEditability();
    emit typeChanged(getType());
    emit inputEdited();
  });
}

void NotebookInfoWidget::updateEditability() {
  const bool editable = m_hasNotebook && !m_readOnly;
  const bool bundled = m_typeComboBox->currentIndex() >= 0 && getType() == NotebookType::Bundled;
  m_nameEdit->setReadOnly(!editable);
  m_descriptionEdit->setReadOnly(!editable);
  m_assetsFolderEdit->setReadOnly(!editable);
  m_rootFolderInput->setReadOnly(m_mode != Mode::Create || !editable);
  m_openRootFolderButton->setEnabled(m_hasNotebook && !getRootFolder().isEmpty());
  m_typeComboBox->setEnabled(m_mode == Mode::Create && editable);
  m_recycleBinFolderInput->setReadOnly(m_mode != Mode::Edit || !editable || !bundled);
  m_lineEndingComboBox->setEnabled(editable && bundled);
  if (m_mode == Mode::Create) {
    m_rootFolderInput->setToolTip(
        bundled
            ? tr("Root folder of the notebook.\n"
                 "A new notebook requires an empty folder or a non-existent path (will be created)")
            : tr("Root folder of the notebook.\n"
                 "For raw notebooks, you can select an existing folder with files.\n"
                 "The folder's contents will be indexed as notebook nodes"));
  }
}

void NotebookInfoWidget::setNotebookInfo(const NotebookInfo &p_info) {
  const QSignalBlocker blocker(this);
  m_hasNotebook = !p_info.id.isEmpty();
  m_readOnly = p_info.readOnly;
  m_nameEdit->setText(p_info.name);
  m_descriptionEdit->setPlainText(p_info.description);
  m_rootFolderInput->setText(p_info.rootFolder);
  m_assetsFolderEdit->setText(p_info.assetsFolder);
  m_recycleBinFolderInput->setText(p_info.recycleBinFolder);
  m_typeComboBox->setCurrentIndex(p_info.type == QStringLiteral("bundled") ? 0
                                  : p_info.type == QStringLiteral("raw")   ? 1
                                                                           : -1);
  const int index = m_lineEndingComboBox->findData(p_info.lineEnding);
  m_lineEndingComboBox->setCurrentIndex(index < 0 ? 0 : index);
  updateEditability();
}

QString NotebookInfoWidget::getName() const {
  return m_mode == Mode::Create ? static_cast<LineEditWithSnippet *>(m_nameEdit)->evaluatedText()
                                : m_nameEdit->text();
}

QString NotebookInfoWidget::getDescription() const { return m_descriptionEdit->toPlainText(); }
QString NotebookInfoWidget::getRootFolder() const { return m_rootFolderInput->text(); }
NotebookType NotebookInfoWidget::getType() const {
  return static_cast<NotebookType>(m_typeComboBox->currentData().toInt());
}
QString NotebookInfoWidget::getAssetsFolder() const { return m_assetsFolderEdit->text(); }
QString NotebookInfoWidget::getRecycleBinFolder() const { return m_recycleBinFolderInput->text(); }
QString NotebookInfoWidget::getLineEnding() const {
  return m_lineEndingComboBox->currentData().toString();
}
