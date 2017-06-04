#include <QtWidgets>
#include <QWebChannel>
#include <QFileInfo>
#include <QXmlStreamReader>
#include "vmdtab.h"
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
#include "vwebview.h"

extern VConfigManager vconfig;

VMdTab::VMdTab(VFile *p_file, VEditArea *p_editArea,
               OpenFileMode p_mode, QWidget *p_parent)
    : VEditTab(p_file, p_editArea, p_parent), m_editor(NULL), m_webViewer(NULL),
      m_document(NULL), m_mdConType(vconfig.getMdConverterType())
{
    V_ASSERT(m_file->getDocType() == DocType::Markdown);

    m_file->open();

    setupUI();

    if (p_mode == OpenFileMode::Edit) {
        showFileEditMode();
    } else {
        showFileReadMode();
    }
}

void VMdTab::setupUI()
{
    m_stacks = new QStackedLayout(this);

    setupMarkdownViewer();

    if (m_file->isModifiable()) {
        m_editor = new VMdEdit(m_file, m_document, m_mdConType, this);
        connect(dynamic_cast<VMdEdit *>(m_editor), &VMdEdit::headersChanged,
                this, &VMdTab::updateTocFromHeaders);
        connect(dynamic_cast<VMdEdit *>(m_editor), &VMdEdit::statusChanged,
                this, &VMdTab::noticeStatusChanged);
        connect(m_editor, SIGNAL(curHeaderChanged(VAnchor)),
                this, SLOT(updateCurHeader(VAnchor)));
        connect(m_editor, &VEdit::textChanged,
                this, &VMdTab::handleTextChanged);
        connect(m_editor, &VEdit::saveAndRead,
                this, &VMdTab::saveAndRead);
        connect(m_editor, &VEdit::discardAndRead,
                this, &VMdTab::discardAndRead);

        m_editor->reloadFile();
        m_stacks->addWidget(m_editor);
    } else {
        m_editor = NULL;
    }

    setLayout(m_stacks);
}

void VMdTab::handleTextChanged()
{
    V_ASSERT(m_file->isModifiable());

    if (m_modified) {
        return;
    }

    noticeStatusChanged();
}

void VMdTab::noticeStatusChanged()
{
    m_modified = m_file->isModified();

    emit statusChanged();
}

void VMdTab::showFileReadMode()
{
    m_isEditMode = false;

    int outlineIndex = m_curHeader.m_outlineIndex;

    if (m_mdConType == MarkdownConverterType::Hoedown) {
        viewWebByConverter();
    } else {
        m_document->updateText();
        updateTocFromHtml(m_document->getToc());
    }

    m_stacks->setCurrentWidget(m_webViewer);
    clearSearchedWordHighlight();

    scrollWebViewToHeader(outlineIndex);

    noticeStatusChanged();
}

void VMdTab::scrollWebViewToHeader(int p_outlineIndex)
{
    QString anchor;

    m_curHeader = VAnchor(m_file, anchor, -1, p_outlineIndex);

    if (p_outlineIndex < m_toc.headers.size() && p_outlineIndex >= 0) {
        QString tmp = m_toc.headers[p_outlineIndex].anchor;
        V_ASSERT(!tmp.isEmpty());
        m_curHeader.anchor = tmp;
        anchor = tmp.mid(1);
    }

    m_document->scrollToAnchor(anchor);

    emit curHeaderChanged(m_curHeader);
}

void VMdTab::viewWebByConverter()
{
    VMarkdownConverter mdConverter;
    QString toc;
    QString html = mdConverter.generateHtml(m_file->getContent(),
                                            vconfig.getMarkdownExtensions(),
                                            toc);
    m_document->setHtml(html);
    updateTocFromHtml(toc);
}

void VMdTab::showFileEditMode()
{
    if (!m_file->isModifiable()) {
        return;
    }

    m_isEditMode = true;

    VMdEdit *mdEdit = dynamic_cast<VMdEdit *>(m_editor);
    V_ASSERT(mdEdit);

    // beginEdit() may change m_curHeader.
    int outlineIndex = m_curHeader.m_outlineIndex;
    int lineNumber = -1;
    auto headers = mdEdit->getHeaders();
    if (outlineIndex < 0 || outlineIndex >= headers.size()) {
        lineNumber = -1;
        outlineIndex = -1;
    } else {
        lineNumber = headers[outlineIndex].lineNumber;
    }

    VAnchor anchor(m_file, "", lineNumber, outlineIndex);

    mdEdit->beginEdit();
    m_stacks->setCurrentWidget(mdEdit);

    mdEdit->scrollToHeader(anchor);

    mdEdit->setFocus();

    noticeStatusChanged();
}

bool VMdTab::closeFile(bool p_forced)
{
    if (p_forced && m_isEditMode) {
        // Discard buffer content
        m_editor->reloadFile();
        m_editor->endEdit();

        showFileReadMode();
    } else {
        readFile();
    }

    return !m_isEditMode;
}

void VMdTab::editFile()
{
    if (m_isEditMode || !m_file->isModifiable()) {
        return;
    }

    showFileEditMode();
}

void VMdTab::readFile()
{
    if (!m_isEditMode) {
        return;
    }

    if (m_editor && m_editor->isModified()) {
        // Prompt to save the changes.
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      tr("Note <span style=\"%1\">%2</span> has been modified.")
                                        .arg(vconfig.c_dataTextStyle).arg(m_file->getName()),
                                      tr("Do you want to save your changes?"),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                      QMessageBox::Save, this);
        switch (ret) {
        case QMessageBox::Save:
            saveFile();
            // Fall through

        case QMessageBox::Discard:
            m_editor->reloadFile();
            break;

        case QMessageBox::Cancel:
            // Nothing to do if user cancel this action
            return;

        default:
            qWarning() << "wrong return value from QMessageBox:" << ret;
            return;
        }
    }

    if (m_editor) {
        m_editor->endEdit();
    }

    showFileReadMode();
}

bool VMdTab::saveFile()
{
    if (!m_isEditMode || !m_editor->isModified()) {
        return true;
    }

    bool ret;
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    QString filePath = m_file->retrivePath();
    if (!QFileInfo::exists(filePath)) {
        qWarning() << filePath << "being written has been removed";
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("File <span style=\"%1\">%2</span> being written has been removed.")
                              .arg(vconfig.c_dataTextStyle).arg(filePath),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        return false;
    }

    m_editor->saveFile();
    ret = m_file->save();
    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("Fail to write to disk when saving a note. Please try it again."),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        m_editor->setModified(true);
    }

    noticeStatusChanged();

    return ret;
}

void VMdTab::saveAndRead()
{
    saveFile();
    readFile();
}

void VMdTab::discardAndRead()
{
    readFile();
}

QString VMdTab::fillHtmlTemplate() const
{
    const QString &jsHolder = c_htmlJSHolder;
    const QString &extraHolder = c_htmlExtraHolder;

    QString jsFile, extraFile;
    switch (m_mdConType) {
    case MarkdownConverterType::Marked:
        jsFile = "qrc" + VNote::c_markedJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Hoedown:
        jsFile = "qrc" + VNote::c_hoedownJsFile;
        // Use Marked to highlight code blocks.
        extraFile = "<script src=\"qrc" + VNote::c_markedExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::MarkdownIt:
        jsFile = "qrc" + VNote::c_markdownitJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_markdownitExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitAnchorExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitTaskListExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitSubExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitSupExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_markdownitFootnoteExtraFile + "\"></script>\n";
        break;

    case MarkdownConverterType::Showdown:
        jsFile = "qrc" + VNote::c_showdownJsFile;
        extraFile = "<script src=\"qrc" + VNote::c_showdownExtraFile + "\"></script>\n" +
                    "<script src=\"qrc" + VNote::c_showdownAnchorExtraFile + "\"></script>\n";

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

    if (vconfig.getEnableImageCaption()) {
        extraFile += "<script>var VEnableImageCaption = true;</script>\n";
    }

    QString htmlTemplate = VNote::s_markdownTemplate;
    htmlTemplate.replace(jsHolder, jsFile);
    if (!extraFile.isEmpty()) {
        htmlTemplate.replace(extraHolder, extraFile);
    }

    return htmlTemplate;
}

void VMdTab::setupMarkdownViewer()
{
    m_webViewer = new VWebView(m_file, this);
    connect(m_webViewer, &VWebView::editNote,
            this, &VMdTab::editFile);

    VPreviewPage *page = new VPreviewPage(m_webViewer);
    m_webViewer->setPage(page);
    m_webViewer->setZoomFactor(vconfig.getWebZoomFactor());

    m_document = new VDocument(m_file, m_webViewer);

    QWebChannel *channel = new QWebChannel(m_webViewer);
    channel->registerObject(QStringLiteral("content"), m_document);
    connect(m_document, &VDocument::tocChanged,
            this, &VMdTab::updateTocFromHtml);
    connect(m_document, SIGNAL(headerChanged(const QString&)),
            this, SLOT(updateCurHeader(const QString &)));
    connect(m_document, &VDocument::keyPressed,
            this, &VMdTab::handleWebKeyPressed);
    page->setWebChannel(channel);

    m_webViewer->setHtml(fillHtmlTemplate(), m_file->getBaseUrl());

    m_stacks->addWidget(m_webViewer);
}

static void parseTocUl(QXmlStreamReader &p_xml, QVector<VHeader> &p_headers,
                       int p_level);

static void parseTocLi(QXmlStreamReader &p_xml, QVector<VHeader> &p_headers, int p_level)
{
    Q_ASSERT(p_xml.isStartElement() && p_xml.name() == "li");

    if (p_xml.readNextStartElement()) {
        if (p_xml.name() == "a") {
            QString anchor = p_xml.attributes().value("href").toString();
            QString name;
            if (p_xml.readNext()) {
                if (p_xml.tokenString() == "Characters") {
                    name = p_xml.text().toString();
                } else if (!p_xml.isEndElement()) {
                    qWarning() << "TOC HTML <a> should be ended by </a>" << p_xml.name();
                    return;
                }

                VHeader header(p_level, name, anchor, -1, p_headers.size());
                p_headers.append(header);
            } else {
                // Error
                return;
            }
        } else if (p_xml.name() == "ul") {
            // Such as header 3 under header 1 directly
            VHeader header(p_level, c_emptyHeaderName, "#", -1, p_headers.size());
            p_headers.append(header);
            parseTocUl(p_xml, p_headers, p_level + 1);
        } else {
            qWarning() << "TOC HTML <li> should contain <a> or <ul>" << p_xml.name();
            return;
        }
    }

    while (p_xml.readNext()) {
        if (p_xml.isEndElement()) {
            if (p_xml.name() == "li") {
                return;
            }
            continue;
        }
        if (p_xml.name() == "ul") {
            // Nested unordered list
            parseTocUl(p_xml, p_headers, p_level + 1);
        } else {
            return;
        }
    }
}

static void parseTocUl(QXmlStreamReader &p_xml, QVector<VHeader> &p_headers,
                       int p_level)
{
    Q_ASSERT(p_xml.isStartElement() && p_xml.name() == "ul");

    while (p_xml.readNextStartElement()) {
        if (p_xml.name() == "li") {
            parseTocLi(p_xml, p_headers, p_level);
        } else {
            qWarning() << "TOC HTML <ul> should contain <li>" << p_xml.name();
            break;
        }
    }
}

static bool parseTocHtml(const QString &p_tocHtml,
                         QVector<VHeader> &p_headers)
{
    if (!p_tocHtml.isEmpty()) {
        QXmlStreamReader xml(p_tocHtml);
        if (xml.readNextStartElement()) {
            if (xml.name() == "ul") {
                parseTocUl(xml, p_headers, 1);
            } else {
                qWarning() << "TOC HTML does not start with <ul>";
            }
        }

        if (xml.hasError()) {
            qWarning() << "fail to parse TOC in HTML";
            return false;
        }
    }

    return true;
}

void VMdTab::updateTocFromHtml(const QString &p_tocHtml)
{
    if (m_isEditMode) {
        return;
    }

    m_toc.type = VHeaderType::Anchor;
    m_toc.headers.clear();

    if (!parseTocHtml(p_tocHtml, m_toc.headers)) {
        return;
    }

    m_toc.m_file = m_file;
    m_toc.valid = true;

    emit outlineChanged(m_toc);
}

void VMdTab::updateTocFromHeaders(const QVector<VHeader> &p_headers)
{
    if (!m_isEditMode) {
        return;
    }

    m_toc.type = VHeaderType::LineNumber;
    m_toc.headers = p_headers;
    m_toc.m_file = m_file;
    m_toc.valid = true;

    // Clear current header.
    m_curHeader = VAnchor(m_file, "", -1, -1);
    emit curHeaderChanged(m_curHeader);

    emit outlineChanged(m_toc);
}

void VMdTab::scrollToAnchor(const VAnchor &p_anchor)
{
    if (p_anchor == m_curHeader) {
        return;
    }

    m_curHeader = p_anchor;

    if (m_isEditMode) {
        dynamic_cast<VMdEdit *>(m_editor)->scrollToHeader(p_anchor);
    } else {
        if (!p_anchor.anchor.isEmpty()) {
            m_document->scrollToAnchor(p_anchor.anchor.mid(1));
        }
    }
}

void VMdTab::updateCurHeader(const QString &p_anchor)
{
    if (m_isEditMode || m_curHeader.anchor.mid(1) == p_anchor) {
        return;
    }

    m_curHeader = VAnchor(m_file, "#" + p_anchor, -1);
    if (!p_anchor.isEmpty()) {
        const QVector<VHeader> &headers = m_toc.headers;
        for (int i = 0; i < headers.size(); ++i) {
            if (headers[i].anchor == m_curHeader.anchor) {
                V_ASSERT(headers[i].index == i);
                m_curHeader.m_outlineIndex = headers[i].index;
                break;
            }
        }
    }

    emit curHeaderChanged(m_curHeader);
}

void VMdTab::updateCurHeader(VAnchor p_anchor)
{
    if (m_isEditMode) {
        if (!p_anchor.anchor.isEmpty() || p_anchor.lineNumber == m_curHeader.lineNumber) {
            return;
        }
    } else {
        if (p_anchor.lineNumber != -1 || p_anchor.anchor == m_curHeader.anchor) {
            return;
        }
    }

    m_curHeader = p_anchor;

    emit curHeaderChanged(m_curHeader);
}

void VMdTab::insertImage()
{
    if (!m_isEditMode) {
        return;
    }

    m_editor->insertImage();
}

void VMdTab::findText(const QString &p_text, uint p_options, bool p_peek,
                      bool p_forward)
{
    if (m_isEditMode || !m_webViewer) {
        if (p_peek) {
            m_editor->peekText(p_text, p_options);
        } else {
            m_editor->findText(p_text, p_options, p_forward);
        }
    } else {
        findTextInWebView(p_text, p_options, p_peek, p_forward);
    }
}

void VMdTab::replaceText(const QString &p_text, uint p_options,
                         const QString &p_replaceText, bool p_findNext)
{
    if (m_isEditMode) {
        m_editor->replaceText(p_text, p_options, p_replaceText, p_findNext);
    }
}

void VMdTab::replaceTextAll(const QString &p_text, uint p_options,
                            const QString &p_replaceText)
{
    if (m_isEditMode) {
        m_editor->replaceTextAll(p_text, p_options, p_replaceText);
    }
}

void VMdTab::findTextInWebView(const QString &p_text, uint p_options,
                               bool /* p_peek */, bool p_forward)
{
    V_ASSERT(m_webViewer);

    QWebEnginePage::FindFlags flags;
    if (p_options & FindOption::CaseSensitive) {
        flags |= QWebEnginePage::FindCaseSensitively;
    }

    if (!p_forward) {
        flags |= QWebEnginePage::FindBackward;
    }

    m_webViewer->findText(p_text, flags);
}

QString VMdTab::getSelectedText() const
{
    if (m_isEditMode || !m_webViewer) {
        QTextCursor cursor = m_editor->textCursor();
        return cursor.selectedText();
    } else {
        return m_webViewer->selectedText();
    }
}

void VMdTab::clearSearchedWordHighlight()
{
    if (m_webViewer) {
        m_webViewer->findText("");
    }

    if (m_editor) {
        m_editor->clearSearchedWordHighlight();
    }
}

void VMdTab::handleWebKeyPressed(int p_key, bool p_ctrl, bool /* p_shift */)
{
    V_ASSERT(m_webViewer);

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
            m_webViewer->setZoomFactor(1);
        }
        break;

    default:
        break;
    }
}

void VMdTab::zoom(bool p_zoomIn, qreal p_step)
{
    if (m_isEditMode) {
        // TODO
    } else {
        zoomWebPage(p_zoomIn, p_step);
    }
}

void VMdTab::zoomWebPage(bool p_zoomIn, qreal p_step)
{
    V_ASSERT(m_webViewer);

    qreal curFactor = m_webViewer->zoomFactor();
    qreal newFactor = p_zoomIn ? curFactor + p_step : curFactor - p_step;
    if (newFactor < c_webZoomFactorMin) {
        newFactor = c_webZoomFactorMin;
    } else if (newFactor > c_webZoomFactorMax) {
        newFactor = c_webZoomFactorMax;
    }

    m_webViewer->setZoomFactor(newFactor);
}

VWebView *VMdTab::getWebViewer() const
{
    return m_webViewer;
}

MarkdownConverterType VMdTab::getMarkdownConverterType() const
{
    return m_mdConType;
}
