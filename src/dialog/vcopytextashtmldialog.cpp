#include "vcopytextashtmldialog.h"

#include <QtWidgets>
#include <QWebEngineView>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>

#include "utils/vutils.h"
#include "utils/vclipboardutils.h"
#include "utils/vwebutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

extern VWebUtils *g_webUtils;

VCopyTextAsHtmlDialog::VCopyTextAsHtmlDialog(const QString &p_text,
                                             const QString &p_copyTarget,
                                             QWidget *p_parent)
    : QDialog(p_parent), m_text(p_text), m_copyTarget(p_copyTarget)
{
    setupUI();
}

void VCopyTextAsHtmlDialog::setupUI()
{
    QLabel *textLabel = new QLabel(tr("Text:"));
    m_textEdit = new QPlainTextEdit(m_text);
    m_textEdit->setReadOnly(true);
    m_textEdit->setProperty("LineEdit", true);

    m_htmlLabel = new QLabel(tr("HTML:"));
    m_htmlViewer = VUtils::getWebEngineView(g_config->getBaseBackground());
    m_htmlViewer->setContextMenuPolicy(Qt::NoContextMenu);
    m_htmlViewer->setMinimumSize(600, 400);

    m_infoLabel = new QLabel(tr("Converting text to HTML ..."));

    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    m_btnBox->button(QDialogButtonBox::Ok)->setProperty("SpecialBtn", true);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(textLabel);
    mainLayout->addWidget(m_textEdit);
    mainLayout->addWidget(m_htmlLabel);
    mainLayout->addWidget(m_htmlViewer);
    mainLayout->addWidget(m_infoLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    setWindowTitle(tr("Copy Text As HTML (%1)").arg(m_copyTarget));

    setHtmlVisible(false);
}

void VCopyTextAsHtmlDialog::setHtmlVisible(bool p_visible)
{
    m_htmlLabel->setVisible(p_visible);
    m_htmlViewer->setVisible(p_visible);
}

void VCopyTextAsHtmlDialog::setConvertedHtml(const QUrl &p_baseUrl,
                                             const QString &p_html)
{
    QString html = p_html;
    m_htmlViewer->setHtml("<html><body>" + html + "</body></html>", p_baseUrl);
    setHtmlVisible(true);

    g_webUtils->alterHtmlAsTarget(p_baseUrl, html, m_copyTarget);

    QClipboard *clipboard = QApplication::clipboard();
    QMimeData *data = new QMimeData();
    data->setText(m_text);
    data->setHtml(html);
    VClipboardUtils::setMimeDataToClipboard(clipboard, data, QClipboard::Clipboard);

    QTimer::singleShot(3000, this, &VCopyTextAsHtmlDialog::accept);
    m_infoLabel->setText(tr("HTML has been copied. Will be closed in 3 seconds."));
}
