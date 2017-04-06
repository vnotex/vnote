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
#include "dialog/vfindreplacedialog.h"
#include "veditarea.h"
#include "vconstants.h"

extern VConfigManager vconfig;

VEditTab::VEditTab(VFile *p_file, OpenFileMode p_mode, QWidget *p_parent)
    : QStackedWidget(p_parent), m_file(p_file), isEditMode(false), document(p_file, this),
      mdConverterType(vconfig.getMdConverterType()), m_fileModified(false),
      m_editArea(NULL)
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

void VEditTab::init(VEditArea *p_editArea)
{
    m_editArea = p_editArea;
}

void VEditTab::setupUI()
{
    switch (m_file->getDocType()) {
    case DocType::Markdown:
        if (m_file->isModifiable()) {
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
        } else {
            m_textEditor = NULL;
        }
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
        qWarning() << "unknown doc type" << int(m_file->getDocType());
        Q_ASSERT(false);
    }
}

void VEditTab::handleTextChanged()
{
    Q_ASSERT(m_file->isModifiable());
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
        if (mdConverterType == MarkdownConverterType::Hoedown) {
            previewByConverter();
        } else {
            document.updateText();
            updateTocFromHtml(document.getToc());
        }
        setCurrentWidget(webPreviewer);
        clearSearchedWordHighlight();
        scrollPreviewToHeader(outlineIndex);
        break;
    default:
        qWarning() << "unknown doc type" << int(m_file->getDocType());
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
    const QString &content = m_file->getContent();
    QString html = mdConverter.generateHtml(content, vconfig.getMarkdownExtensions());
    QRegularExpression tocExp("<p>\\[TOC\\]<\\/p>", QRegularExpression::CaseInsensitiveOption);
    QString toc = mdConverter.generateToc(content, vconfig.getMarkdownExtensions());
    processHoedownToc(toc);
    html.replace(tocExp, toc);
    document.setHtml(html);
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
    if (!m_file->isModifiable()) {
        return;
    }
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
    if (isEditMode || !m_file->isModifiable()) {
        return;
    }

    showFileEditMode();
}

void VEditTab::readFile()
{
    if (!isEditMode) {
        return;
    }

    if (m_textEditor && m_textEditor->isModified()) {
        // Prompt to save the changes
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      tr("Note %1 has been modified.").arg(m_file->getName()),
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
            qWarning() << "wrong return value from QMessageBox:" << ret;
            return;
        }
    }

    if (m_textEditor) {
        m_textEditor->endEdit();
    }

    showFileReadMode();
}

bool VEditTab::saveFile()
{
    if (!isEditMode || !m_textEditor->isModified()) {
        return true;
    }

    bool ret;
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    QString filePath = m_file->retrivePath();
    if (!QFile(filePath).exists()) {
        qWarning() << filePath << "being written has been removed";
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("%1 being written has been removed.").arg(filePath),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return false;
    }
    m_textEditor->saveFile();
    ret = m_file->save();
    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("Fail to write to disk when saving a note. Please try it again."),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        m_textEditor->setModified(true);
    }
    noticeStatusChanged();
    return ret;
}

void VEditTab::setupMarkdownPreview()
{
    const QString jsHolder("JS_PLACE_HOLDER");
    const QString extraHolder("<!-- EXTRA_PLACE_HOLDER -->");

    webPreviewer = new QWebEngineView(this);
    VPreviewPage *page = new VPreviewPage(this);
    webPreviewer->setPage(page);
    webPreviewer->setZoomFactor(vconfig.getWebZoomFactor());

    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("content"), &document);
    connect(&document, &VDocument::tocChanged,
            this, &VEditTab::updateTocFromHtml);
    connect(&document, SIGNAL(headerChanged(const QString&)),
            this, SLOT(updateCurHeader(const QString &)));
    connect(&document, &VDocument::keyPressed,
            this, &VEditTab::handleWebKeyPressed);
    page->setWebChannel(channel);

    QString jsFile, extraFile;
    switch (mdConverterType) {
    case MarkdownConverterType::Marked:
        jsFile = "qrc" + VNote::c_markedJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Hoedown:
        jsFile = "qrc" + VNote::c_hoedownJsFile;
        break;

    case MarkdownConverterType::MarkdownIt:
        jsFile = "qrc" + VNote::c_markdownitJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markdownitExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitAnchorExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitTaskListExtraFile + "\"></script>\n";
        break;

    default:
        Q_ASSERT(false);
    }

    if (vconfig.getEnableMermaid()) {
        extraFile += "<link rel=\"stylesheet\" type=\"text/css\" href=\"qrc" + VNote::c_mermaidCssFile +
                     "\"/>\n" + "<script src=\"qrc" + VNote::c_mermaidApiJsFile + "\"></script>\n" +
                     "<script>var VEnableMermaid = true;</script>\n";
    }

    if (vconfig.getEnableMathjax()) {
        extraFile += "<script type=\"text/x-mathjax-config\">"
                     "MathJax.Hub.Config({\n"
                     "                    tex2jax: {inlineMath: [['$','$'], ['\\\\(','\\\\)']]},\n"
                     "                    showProcessingMessages: false,\n"
                     "                    messageStyle: \"none\"});\n"
                     "</script>\n"
                     "<script type=\"text/javascript\" async src=\"" + VNote::c_mathjaxJsFile + "\"></script>\n" +
                     "<script>var VEnableMathjax = true;</script>\n";
    }

    QString htmlTemplate = VNote::s_markdownTemplate;
    htmlTemplate.replace(jsHolder, jsFile);
    if (!extraFile.isEmpty()) {
        htmlTemplate.replace(extraHolder, extraFile);
    }
    webPreviewer->setHtml(htmlTemplate, QUrl::fromLocalFile(m_file->retriveBasePath() + QDir::separator()));
    addWidget(webPreviewer);
}

void VEditTab::focusTab()
{
    currentWidget()->setFocus();
    emit getFocused();
}

void VEditTab::handleFocusChanged(QWidget * /* old */, QWidget *now)
{
    if (isChild(now)) {
        if (now == this) {
            // When VEditTab get focus, it should focus to current widget.
            currentWidget()->setFocus();
        }
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
                qWarning() << "TOC HTML does not start with <ul>";
            }
        }
        if (xml.hasError()) {
            qWarning() << "fail to parse TOC in HTML";
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
            qWarning() << "TOC HTML <ul> should contain <li>" << xml.name();
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
                    qWarning() << "TOC HTML <a> should be ended by </a>" << xml.name();
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
            VHeader header(level, "[EMPTY]", "#", -1);
            headers.append(header);
            parseTocUl(xml, headers, level + 1);
        } else {
            qWarning() << "TOC HTML <li> should contain <a> or <ul>" << xml.name();
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
    checkToc();
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
        if (checkToc()) {
            emit outlineChanged(tableOfContent);
        }
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
    if (checkToc()) {
        emit outlineChanged(tableOfContent);
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

void VEditTab::findText(const QString &p_text, uint p_options, bool p_peek,
                        bool p_forward)
{
    if (isEditMode || !webPreviewer) {
        if (p_peek) {
            m_textEditor->peekText(p_text, p_options);
        } else {
            m_textEditor->findText(p_text, p_options, p_forward);
        }
    } else {
        findTextInWebView(p_text, p_options, p_peek, p_forward);
    }
}

void VEditTab::replaceText(const QString &p_text, uint p_options,
                           const QString &p_replaceText, bool p_findNext)
{
    if (isEditMode) {
        m_textEditor->replaceText(p_text, p_options, p_replaceText, p_findNext);
    }
}

void VEditTab::replaceTextAll(const QString &p_text, uint p_options,
                              const QString &p_replaceText)
{
    if (isEditMode) {
        m_textEditor->replaceTextAll(p_text, p_options, p_replaceText);
    }
}

void VEditTab::findTextInWebView(const QString &p_text, uint p_options,
                                 bool /* p_peek */, bool p_forward)
{
    Q_ASSERT(webPreviewer);
    QWebEnginePage::FindFlags flags;
    if (p_options & FindOption::CaseSensitive) {
        flags |= QWebEnginePage::FindCaseSensitively;
    }
    if (!p_forward) {
        flags |= QWebEnginePage::FindBackward;
    }
    webPreviewer->findText(p_text, flags);
}

QString VEditTab::getSelectedText() const
{
    if (isEditMode || !webPreviewer) {
        QTextCursor cursor = m_textEditor->textCursor();
        return cursor.selectedText();
    } else {
        return webPreviewer->selectedText();
    }
}

void VEditTab::clearSearchedWordHighlight()
{
    if (webPreviewer) {
        webPreviewer->findText("");
    }
    if (m_textEditor) {
        m_textEditor->clearSearchedWordHighlight();
    }
}

bool VEditTab::checkToc()
{
    bool ret = false;
    if (tableOfContent.filePath != m_file->retrivePath()) {
        tableOfContent.filePath = m_file->retrivePath();
        ret = true;
    }
    return ret;
}

void VEditTab::handleWebKeyPressed(int p_key, bool p_ctrl, bool /* p_shift */)
{
    Q_ASSERT(webPreviewer);
    switch (p_key) {
    // Esc
    case 27:
        m_editArea->getFindReplaceDialog()->closeDialog();
        break;

    // Dash
    case 189:
        if (p_ctrl) {
            // Zoom out.
            zoomWebPage(false);
        }
        break;

    // Equal
    case 187:
        if (p_ctrl) {
            // Zoom in.
            zoomWebPage(true);
        }
        break;

    // 0
    case 48:
        if (p_ctrl) {
            // Recover zoom.
            webPreviewer->setZoomFactor(1);
        }
        break;

    default:
        break;
    }
}

void VEditTab::wheelEvent(QWheelEvent *p_event)
{
    if (!isEditMode && webPreviewer) {
        QPoint angle = p_event->angleDelta();
        Qt::KeyboardModifiers modifiers = p_event->modifiers();
        if (!angle.isNull() && (angle.y() != 0) && (modifiers & Qt::ControlModifier)) {
            zoomWebPage(angle.y() > 0);
            p_event->accept();
            return;
        }
    }
    p_event->ignore();
}

void VEditTab::zoomWebPage(bool p_zoomIn, qreal p_step)
{
    Q_ASSERT(webPreviewer);
    qreal curFactor = webPreviewer->zoomFactor();
    qreal newFactor = p_zoomIn ? curFactor + p_step : curFactor - p_step;
    if (newFactor < c_webZoomFactorMin) {
        newFactor = c_webZoomFactorMin;
    } else if (newFactor > c_webZoomFactorMax) {
        newFactor = c_webZoomFactorMax;
    }
    webPreviewer->setZoomFactor(newFactor);
}
