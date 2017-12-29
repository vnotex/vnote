#include "vtipsdialog.h"

#include <QtWidgets>
#include <QWebEngineView>

#include "vconfigmanager.h"
#include "vmarkdownconverter.h"
#include "utils/vutils.h"
#include "vnote.h"

extern VConfigManager *g_config;

VTipsDialog::VTipsDialog(const QString &p_tipFile,
                         const QString &p_actionText,
                         TipsDialogFunc p_action,
                         QWidget *p_parent)
    : QDialog(p_parent), m_actionBtn(NULL), m_action(p_action)
{
    setupUI(p_actionText);

    readFile(p_tipFile);
}

void VTipsDialog::setupUI(const QString &p_actionText)
{
    m_viewer = VUtils::getWebEngineView();
    m_viewer->setContextMenuPolicy(Qt::NoContextMenu);

    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    m_okBtn = m_btnBox->button(QDialogButtonBox::Ok);

    if (!p_actionText.isEmpty()) {
        Q_ASSERT(m_action != nullptr);
        m_actionBtn = m_btnBox->addButton(p_actionText, QDialogButtonBox::ActionRole);
        m_actionBtn->setProperty("SpecialBtn", true);
        m_actionBtn->setDefault(true);

        connect(m_actionBtn, &QPushButton::clicked,
                this, [this]() {
                    m_action();
                });
    }

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_viewer);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    setWindowTitle(tr("VNote Tips"));
}

void VTipsDialog::readFile(const QString &p_tipFile)
{
    QString content = VUtils::readFileFromDisk(p_tipFile);
    VMarkdownConverter mdConverter;
    QString toc;
    QString html = mdConverter.generateHtml(content,
                                            g_config->getMarkdownExtensions(),
                                            toc);
    html = VUtils::generateSimpleHtmlTemplate(html);
    m_viewer->setHtml(html);
}

void VTipsDialog::showEvent(QShowEvent *p_event)
{
    QDialog::showEvent(p_event);

    if (m_actionBtn) {
        m_actionBtn->setFocus();
    } else {
        m_okBtn->setFocus();
    }
}
