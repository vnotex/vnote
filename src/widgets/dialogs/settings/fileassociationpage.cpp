#include "fileassociationpage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QMap>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <core/sessionconfig.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

const char *FileAssociationPage::c_nameProperty = "name";

const QChar FileAssociationPage::c_suffixSeparator = QLatin1Char(';');

FileAssociationPage::FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void FileAssociationPage::setupUI() {
  auto mainLayout = new QVBoxLayout(this);

  m_builtInFileTypesBox = new QGroupBox(tr("Built-In File Types"), this);
  WidgetsFactory::createFormLayout(m_builtInFileTypesBox);
  mainLayout->addWidget(m_builtInFileTypesBox);

  m_externalProgramsBox = new QGroupBox(tr("External Programs"), this);
  WidgetsFactory::createFormLayout(m_externalProgramsBox);
  mainLayout->addWidget(m_externalProgramsBox);

  mainLayout->addStretch();
}

void FileAssociationPage::loadInternal() {
  loadBuiltInTypesGroup(m_builtInFileTypesBox);

  loadExternalProgramsGroup(m_externalProgramsBox);
}

bool FileAssociationPage::saveInternal() {
  auto *ftService = m_services.get<FileTypeCoreService>();
  Q_ASSERT(ftService);
  auto types = ftService->getAllFileTypes();

  // Update suffixes from UI line edits.
  auto layout = static_cast<QFormLayout *>(m_builtInFileTypesBox->layout());
  for (int i = 0; i < layout->rowCount(); ++i) {
    auto *item = layout->itemAt(i, QFormLayout::FieldRole);
    if (!item) {
      continue;
    }
    auto *lineEdit = qobject_cast<QLineEdit *>(item->widget());
    if (!lineEdit) {
      continue;
    }

    QString name = lineEdit->property(c_nameProperty).toString();
    QStringList suffixes =
        lineEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
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

void FileAssociationPage::loadBuiltInTypesGroup(QGroupBox *p_box) {
  auto layout = static_cast<QFormLayout *>(p_box->layout());
  WidgetUtils::clearLayout(layout);

  auto *ftService = m_services.get<FileTypeCoreService>();
  Q_ASSERT(ftService);
  const auto types = ftService->getAllFileTypes();

  for (const auto &ft : types) {
    if (ft.m_typeName == "Others") {
      continue;
    }

    auto lineEdit = WidgetsFactory::createLineEdit(p_box);
    layout->addRow(ft.m_displayName, lineEdit);
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(tr("List of suffixes for this file type"));
    lineEdit->setProperty(c_nameProperty, ft.m_typeName);
    lineEdit->setText(ft.m_suffixes.join(c_suffixSeparator));
  }
}

void FileAssociationPage::loadExternalProgramsGroup(QGroupBox *p_box) {
  auto layout = static_cast<QFormLayout *>(p_box->layout());
  WidgetUtils::clearLayout(layout);

  const auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  QStringList names;
  for (const auto &pro : sessionConfig.getExternalPrograms()) {
    names.push_back(pro.m_name);
  }

  // "System Default Program" label.
  static const QString c_systemDefaultProgram = tr("System Default Program");
  names << c_systemDefaultProgram;

  for (const auto &name : names) {
    auto lineEdit = WidgetsFactory::createLineEdit(p_box);
    layout->addRow(name, lineEdit);
    connect(lineEdit, &QLineEdit::textChanged, this, &FileAssociationPage::pageIsChanged);

    lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
    lineEdit->setToolTip(
        tr("List of suffixes to open with external program (or system default program)"));
    lineEdit->setProperty(c_nameProperty, name);

    // TODO: External program suffix associations are not yet managed by vxcore.
    // Suffix editing is shown but not persisted until session config migration.
  }
}
