#include <QtWidgets>
#include <QWebChannel>
#include <QWebEngineView>
#include <QFileInfo>
#include <QXmlStreamReader>
#include "vedittab.h"
#include "vedit.h"
#include "vdocument.h"
#include "vnote.h"
#include "utils/vutils.h"
#include "vpreviewpage.h"
#include "hgmarkdownhighlighter.h"
#include "vconfigmanager.h"
#include "vmarkdownconverter.h"
#include "vnotebook.h"
#include "vtoc.h"
#include "vmdedit.h"

extern VConfigManager vconfig;

VEditTab::VEditTab(VFile *p_file, OpenFileMode p_mode, QWidget *p_parent)
    : QStackedWidget(p_parent), m_file(p_file), isEditMode(false),
      mdConverterType(vconfig.getMdConverterType()), m_fileModified(false)
{
    tableOfContent.filePath = p_file->retrivePath();
    curHeader.filePath = p_file->retrivePath();
    Q_ASSERT(!m_file->isOpened());
    m_file->open();
    setupUI();
    if (p_mode == OpenFileMode::Edit) {
        showFileEditMode();
    } else {
        showFileReadMode();
    }
    connect(qApp, &QApplication::focusChanged,
            this, &VEditTab::handleFocusChanged);
}

VEditTab::~VEditTab()
{
    if (m_file) {
        m_file->close();
    }
}

void VEditTab::setupUI()
{
    switch (m_file->getDocType()) {
    case DocType::Markdown:
        m_textEditor = new VMdEdit(m_file, this);
        connect(dynamic_cast<VMdEdit *>(m_textEditor), &VMdEdit::headersChanged,
                this, &VEditTab::updateTocFromHeaders);
        connect(dynamic_cast<VMdEdit *>(m_textEditor), &VMdEdit::statusChanged,
                this, &VEditTab::noticeStatusChanged);
        connect(m_textEditor, SIGNAL(curHeaderChanged(int, int)),
                this, SLOT(updateCurHeader(int, int)));
        connect(m_textEditor, &VEdit::textChanged,
                this, &VEditTab::handleTextChanged);
        m_textEditor->reloadFile();
        addWidget(m_textEditor);

        setupMarkdownPreview();
        break;

    case DocType::Html:
        m_textEditor = new VEdit(m_file, this);
        connect(m_textEditor, &VEdit::textChanged,
                this, &VEditTab::handleTextChanged);
        m_textEditor->reloadFile();
        addWidget(m_textEditor);
        webPreviewer = NULL;
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
        Q_ASSERT(false);
    }
}

void VEditTab::handleTextChanged()
{
    if (m_fileModified) {
        return;
    }
    noticeStatusChanged();
}

void VEditTab::noticeStatusChanged()
{
    m_fileModified = m_file->isModified();
    emit statusChanged();
}

void VEditTab::showFileReadMode()
{
    qDebug() << "read" << m_file->getName();
    isEditMode = false;
    int outlineIndex = curHeader.m_outlineIndex;
    switch (m_file->getDocType()) {
    case DocType::Html:
        m_textEditor->setReadOnly(true);
        break;
    case DocType::Markdown:
        if (mdConverterType == MarkdownConverterType::Marked) {
            document.setText(m_file->getContent());
            updateTocFromHtml(document.getToc());
        } else {
            previewByConverter();
        }
        setCurrentWidget(webPreviewer);
        scrollPreviewToHeader(outlineIndex);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
        Q_ASSERT(false);
    }
    noticeStatusChanged();
}

void VEditTab::scrollPreviewToHeader(int p_outlineIndex)
{
    Q_ASSERT(p_outlineIndex >= 0);
    if (p_outlineIndex < tableOfContent.headers.size()) {
        QString anchor = tableOfContent.headers[p_outlineIndex].anchor;
        qDebug() << "scroll preview to" << p_outlineIndex << anchor;
        if (!anchor.isEmpty()) {
            document.scrollToAnchor(anchor.mid(1));
        }
    }
}

void VEditTab::previewByConverter()
{
    VMarkdownConverter mdConverter;
    QString &content = m_file->getContent();
    QString html = mdConverter.generateHtml(content, vconfig.getMarkdownExtensions());
    QRegularExpression tocExp("<p>\\[TOC\\]<\\/p>", QRegularExpression::CaseInsensitiveOption);
    QString toc = mdConverter.generateToc(content, vconfig.getMarkdownExtensions());
    processHoedownToc(toc);
    html.replace(tocExp, toc);
    document.setHtml(html);
    // Hoedown will add '\n' while Marked does not
    updateTocFromHtml(toc);
}

void VEditTab::processHoedownToc(QString &p_toc)
{
    // Hoedown will add '\n'.
    p_toc.replace("\n", "");
    // Hoedown will translate `_` in title to `<em>`.
    p_toc.replace("<em>", "_");
    p_toc.replace("</em>", "_");
}

void VEditTab::showFileEditMode()
{
    isEditMode = true;

    // beginEdit() may change curHeader.
    int outlineIndex = curHeader.m_outlineIndex;
    m_textEditor->beginEdit();
    setCurrentWidget(m_textEditor);
    if (m_file->getDocType() == DocType::Markdown) {
        dynamic_cast<VMdEdit *>(m_textEditor)->scrollToHeader(outlineIndex);
    }
    m_textEditor->setFocus();
    noticeStatusChanged();
}

bool VEditTab::closeFile(bool p_forced)
{
    if (p_forced && isEditMode) {
        // Discard buffer content
        m_textEditor->reloadFile();
        m_textEditor->endEdit();
        showFileReadMode();
    } else {
        readFile();
    }
    return !isEditMode;
}

void VEditTab::editFile()
{
    if (isEditMode) {
        return;
    }

    showFileEditMode();
}

void VEditTab::readFile()
{
    if (!isEditMode) {
        return;
    }

    if (m_textEditor->isModified()) {
        // Prompt to save the changes
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      QString("Note %1 has been modified.").arg(m_file->getName()),
                                      tr("Do you want to save your changes?"),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                      QMessageBox::Save, this);
        switch (ret) {
        case QMessageBox::Save:
            saveFile();
            // Fall through
        case QMessageBox::Discard:
            m_textEditor->reloadFile();
            break;
        case QMessageBox::Cancel:
            // Nothing to do if user cancel this action
            return;
        default:
            qWarning() << "error: wrong return value from QMessageBox:" << ret;
            return;
        }
    }
    m_textEditor->endEdit();
    showFileReadMode();
}

bool VEditTab::saveFile()
{
    bool ret;
    if (!isEditMode || !m_textEditor->isModified()) {
        return true;
    }
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    QString filePath = m_file->retrivePath();
    if (!QFile(filePath).exists()) {
        qWarning() << filePath << "being written has been removed";
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note"),
                            QString("%1 being written has been removed.").arg(filePath),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return false;
    }
    m_textEditor->saveFile();
    ret = m_file->save();
    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note"),
                            QString("Fail to write to disk when saving a note. Please try it again."),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        m_textEditor->setModified(true);
    }
    noticeStatusChanged();
    return ret;
}

void VEditTab::setupMarkdownPreview()
{
    webPreviewer = new QWebEngineView(this);
    VPreviewPage *page = new VPreviewPage(this);
    webPreviewer->setPage(page);

    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("content"), &document);
    connect(&document, &VDocument::tocChanged,
            this, &VEditTab::updateTocFromHtml);
    connect(&document, SIGNAL(headerChanged(const QString&)),
            this, SLOT(updateCurHeader(const QString &)));
    page->setWebChannel(channel);

    if (mdConverterType == MarkdownConverterType::Marked) {
        webPreviewer->setHtml(VNote::templateHtml,
                              QUrl::fromLocalFile(m_file->retriveBasePath() + QDir::separator()));
    } else {
        webPreviewer->setHtml(VNote::preTemplateHtml + VNote::postTemplateHtml,
                              QUrl::fromLocalFile(m_file->retriveBasePath() + QDir::separator()));
    }

    addWidget(webPreviewer);
}

void VEditTab::focusTab()
{
    currentWidget()->setFocus();
}

void VEditTab::handleFocusChanged(QWidget *old, QWidget *now)
{
    if (isChild(now)) {
        emit getFocused();
    }
}

void VEditTab::updateTocFromHtml(const QString &tocHtml)
{
    if (isEditMode) {
        return;
    }
    tableOfContent.type = VHeaderType::Anchor;
    QVector<VHeader> &headers = tableOfContent.headers;
    headers.clear();

    if (!tocHtml.isEmpty()) {
        QXmlStreamReader xml(tocHtml);
        if (xml.readNextStartElement()) {
            if (xml.name() == "ul") {
                parseTocUl(xml, headers, 1);
            } else {
                qWarning() << "error: TOC HTML does not start with <ul>";
            }
        }
        if (xml.hasError()) {
            qWarning() << "error: fail to parse TOC in HTML";
            return;
        }
    }

    tableOfContent.filePath = m_file->retrivePath();
    tableOfContent.valid = true;

    emit outlineChanged(tableOfContent);
}

void VEditTab::updateTocFromHeaders(const QVector<VHeader> &headers)
{
    if (!isEditMode) {
        return;
    }
    tableOfContent.type = VHeaderType::LineNumber;
    tableOfContent.headers = headers;
    tableOfContent.filePath = m_file->retrivePath();
    tableOfContent.valid = true;

    emit outlineChanged(tableOfContent);
}

void VEditTab::parseTocUl(QXmlStreamReader &xml, QVector<VHeader> &headers, int level)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == "ul");

    while (xml.readNextStartElement()) {
        if (xml.name() == "li") {
            parseTocLi(xml, headers, level);
        } else {
            qWarning() << "error: TOC HTML <ul> should contain <li>" << xml.name();
            break;
        }
    }
}

void VEditTab::parseTocLi(QXmlStreamReader &xml, QVector<VHeader> &headers, int level)
{
    Q_ASSERT(xml.isStartElement() && xml.name() == "li");

    if (xml.readNextStartElement()) {
        if (xml.name() == "a") {
            QString anchor = xml.attributes().value("href").toString();
            QString name;
            if (xml.readNext()) {
                if (xml.tokenString() == "Characters") {
                    name = xml.text().toString();
                } else if (!xml.isEndElement()) {
                    qWarning() << "error: TOC HTML <a> should be ended by </a>" << xml.name();
                    return;
                }
                VHeader header(level, name, anchor, -1);
                headers.append(header);
            } else {
                // Error
                return;
            }
        } else if (xml.name() == "ul") {
            // Such as header 3 under header 1 directly
            VHeader header(level, "[Empty]", "#", -1);
            headers.append(header);
            parseTocUl(xml, headers, level + 1);
        } else {
            qWarning() << "error: TOC HTML <li> should contain <a> or <ul>" << xml.name();
            return;
        }
    }

    while (xml.readNext()) {
        if (xml.isEndElement()) {
            if (xml.name() == "li") {
                return;
            }
            continue;
        }
        if (xml.name() == "ul") {
            // Nested unordered list
            parseTocUl(xml, headers, level + 1);
        } else {
            return;
        }
    }
}

void VEditTab::requestUpdateCurHeader()
{
    emit curHeaderChanged(curHeader);
}

void VEditTab::requestUpdateOutline()
{
    emit outlineChanged(tableOfContent);
}

void VEditTab::scrollToAnchor(const VAnchor &anchor)
{
    if (anchor == curHeader) {
        return;
    }
    curHeader = anchor;
    if (isEditMode) {
        if (anchor.lineNumber > -1) {
            m_textEditor->scrollToLine(anchor.lineNumber);
        }
    } else {
        if (!anchor.anchor.isEmpty()) {
            document.scrollToAnchor(anchor.anchor.mid(1));
        }
    }
}

void VEditTab::updateCurHeader(const QString &anchor)
{
    if (isEditMode || curHeader.anchor.mid(1) == anchor) {
        return;
    }
    curHeader = VAnchor(m_file->retrivePath(), "#" + anchor, -1);
    if (!anchor.isEmpty()) {
        const QVector<VHeader> &headers = tableOfContent.headers;
        for (int i = 0; i < headers.size(); ++i) {
            if (headers[i].anchor == curHeader.anchor) {
                curHeader.m_outlineIndex = i;
                break;
            }
        }
        emit curHeaderChanged(curHeader);
    }
}

void VEditTab::updateCurHeader(int p_lineNumber, int p_outlineIndex)
{
    if (!isEditMode || curHeader.lineNumber == p_lineNumber) {
        return;
    }
    curHeader = VAnchor(m_file->retrivePath(), "", p_lineNumber);
    curHeader.m_outlineIndex = p_outlineIndex;
    if (p_lineNumber > -1) {
        emit curHeaderChanged(curHeader);
    }
}

void VEditTab::insertImage()
{
    qDebug() << "insert image";
    if (!isEditMode) {
        return;
    }
    m_textEditor->insertImage();
}
