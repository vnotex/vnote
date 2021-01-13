#include "filepropertiesdialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QFileInfo>
#include <QRegularExpressionValidator>

#include "../lineedit.h"
#include "../widgetsfactory.h"
#include <utils/pathutils.h>
#include <utils/widgetutils.h>


using namespace vnotex;

FilePropertiesDialog::FilePropertiesDialog(const QString &p_path, QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_path(p_path)
{
    Q_ASSERT(!p_path.isEmpty());
    setupUI();

    LineEdit::selectBaseName(m_nameLineEdit);

    m_nameLineEdit->setFocus();
}

void FilePropertiesDialog::setupUI()
{
    auto widget = new QWidget(this);
    setCentralWidget(widget);

    auto mainLayout = WidgetsFactory::createFormLayout(widget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    const QFileInfo info(m_path);

    mainLayout->addRow(tr("Location:"), new QLabel(info.absolutePath(), widget));

    setupNameLineEdit(widget);
    m_nameLineEdit->setText(info.fileName());
    mainLayout->addRow(tr("Name:"), m_nameLineEdit);

    mainLayout->addRow(tr("Size:"), new QLabel(tr("%1 Bytes").arg(info.size()), widget));

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(tr("Properties"));
}

void FilePropertiesDialog::setupNameLineEdit(QWidget *p_parent)
{
    m_nameLineEdit = WidgetsFactory::createLineEdit(p_parent);
    auto validator = new QRegularExpressionValidator(QRegularExpression(PathUtils::c_fileNameRegularExpression),
                                                     m_nameLineEdit);
    m_nameLineEdit->setValidator(validator);
}

QString FilePropertiesDialog::getFileName() const
{
    return m_nameLineEdit->text();
}
