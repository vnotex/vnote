#include "fileassociationpage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QMap>

#include <widgets/widgetsfactory.h>
#include <widgets/lineedit.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/configmgr.h>
#include <core/buffermgr.h>
#include <utils/widgetutils.h>
#include <utils/utils.h>
#include <core/vnotex.h>
#include <buffer/filetypehelper.h>

using namespace vnotex;

const char *FileAssociationPage::c_nameProperty = "name";

const QChar FileAssociationPage::c_suffixSeparator = QLatin1Char(';');

FileAssociationPage::FileAssociationPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void FileAssociationPage::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    m_builtInFileTypesBox = new QGroupBox(tr("Built-In File Types"), this);
    WidgetsFactory::createFormLayout(m_builtInFileTypesBox);
    mainLayout->addWidget(m_builtInFileTypesBox);

    m_externalProgramsBox = new QGroupBox(tr("External Programs"), this);
    WidgetsFactory::createFormLayout(m_externalProgramsBox);
    mainLayout->addWidget(m_externalProgramsBox);

    mainLayout->addStretch();
}

void FileAssociationPage::loadInternal()
{
    loadBuiltInTypesGroup(m_builtInFileTypesBox);

    loadExternalProgramsGroup(m_externalProgramsBox);
}

bool FileAssociationPage::saveInternal()
{
    auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    QVector<CoreConfig::FileTypeSuffix> fileTypeSuffixes;

    auto lineEdits = m_builtInFileTypesBox->findChildren<QLineEdit *>(QString());
    lineEdits << m_externalProgramsBox->findChildren<QLineEdit *>(QString());
    fileTypeSuffixes.reserve(lineEdits.size());
    for (const auto lineEdit : lineEdits) {
        auto name = lineEdit->property(c_nameProperty).toString();
        if (name.isEmpty()) {
            continue;
        }
        auto suffixes = lineEdit->text().split(c_suffixSeparator, Qt::SkipEmptyParts);
        fileTypeSuffixes.push_back(CoreConfig::FileTypeSuffix(name, Utils::toLower(suffixes)));
    }

    coreConfig.setFileTypeSuffixes(fileTypeSuffixes);

    FileTypeHelper::getInst().reload();

    BufferMgr::updateSuffixToFileType(coreConfig.getFileTypeSuffixes());

    return true;
}

QString FileAssociationPage::title() const
{
    return tr("File Associations");
}

void FileAssociationPage::loadBuiltInTypesGroup(QGroupBox *p_box)
{
    auto layout = static_cast<QFormLayout *>(p_box->layout());
    WidgetUtils::clearLayout(layout);

    const auto &types = FileTypeHelper::getInst().getAllFileTypes();

    for (const auto &ft : types) {
        if (ft.m_type == FileType::Others) {
            continue;
        }

        auto lineEdit = WidgetsFactory::createLineEdit(p_box);
        layout->addRow(ft.m_displayName, lineEdit);
        connect(lineEdit, &QLineEdit::textChanged,
                this, &FileAssociationPage::pageIsChanged);

        lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
        lineEdit->setToolTip(tr("List of suffixes for this file type"));
        lineEdit->setProperty(c_nameProperty, ft.m_typeName);
        lineEdit->setText(ft.m_suffixes.join(c_suffixSeparator));
    }
}

void FileAssociationPage::loadExternalProgramsGroup(QGroupBox *p_box)
{
    auto layout = static_cast<QFormLayout *>(p_box->layout());
    WidgetUtils::clearLayout(layout);

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    QStringList names;
    for (const auto &pro : sessionConfig.getExternalPrograms()) {
        names.push_back(pro.m_name);
    }

    names << FileTypeHelper::s_systemDefaultProgram;

    for (const auto &name : names) {
        auto lineEdit = WidgetsFactory::createLineEdit(p_box);
        layout->addRow(name, lineEdit);
        connect(lineEdit, &QLineEdit::textChanged,
                this, &FileAssociationPage::pageIsChanged);

        lineEdit->setPlaceholderText(tr("Suffixes separated by ;"));
        lineEdit->setToolTip(tr("List of suffixes to open with external program (or system default program)"));
        lineEdit->setProperty(c_nameProperty, name);

        auto suffixes = coreConfig.findFileTypeSuffix(name);
        if (suffixes) {
            lineEdit->setText(suffixes->join(c_suffixSeparator));
        }
    }
}
