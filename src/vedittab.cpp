#include <QtWidgets>
#include <QTextBrowser>
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

extern VConfigManager vconfig;

VEditTab::VEditTab(const QString &path, bool modifiable, QWidget *parent)
    : QStackedWidget(parent), mdConverterType(vconfig.getMdConverterType())
{
    DocType docType = isMarkdown(path) ? DocType::Markdown : DocType::Html;
    QString basePath = QFileInfo(path).path();
    QString fileName = QFileInfo(path).fileName();
    qDebug() << "VEditTab basePath" << basePath << "file" << fileName;
    QString fileText = VUtils::readFileFromDisk(path);
    noteFile = new VNoteFile(basePath, fileName, fileText,
                             docType, modifiable);

    isEditMode = false;

    setupUI();

    showFileReadMode();

    connect(qApp, &QApplication::focusChanged,
            this, &VEditTab::handleFocusChanged);
}

VEditTab::~VEditTab()
{
    if (noteFile) {
        delete noteFile;
    }
}

void VEditTab::setupUI()
{
    textEditor = new VEdit(noteFile);
    addWidget(textEditor);

    switch (noteFile->docType) {
    case DocType::Markdown:
        setupMarkdownPreview();
        textBrowser = NULL;
        break;

    case DocType::Html:
        textBrowser = new QTextBrowser();
        addWidget(textBrowser);
        textBrowser->setFont(vconfig.getBaseEditFont());
        textBrowser->setPalette(vconfig.getBaseEditPalette());
        webPreviewer = NULL;
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}

bool VEditTab::isMarkdown(const QString &name)
{
    const QVector<QString> mdPostfix({"md", "markdown", "mkd"});

    QStringList list = name.split('.', QString::SkipEmptyParts);
    if (list.isEmpty()) {
        return false;
    }
    const QString &postfix = list.last();
    for (int i = 0; i < mdPostfix.size(); ++i) {
        if (postfix == mdPostfix[i]) {
            return true;
        }
    }
    return false;
}

void VEditTab::showFileReadMode()
{
    isEditMode = false;
    switch (noteFile->docType) {
    case DocType::Html:
        textBrowser->setHtml(noteFile->content);
        textBrowser->setFont(vconfig.getBaseEditFont());
        textBrowser->setPalette(vconfig.getBaseEditPalette());
        setCurrentWidget(textBrowser);
        break;
    case DocType::Markdown:
        if (mdConverterType == MarkdownConverterType::Marked) {
            document.setText(noteFile->content);
        } else {
            previewByConverter();
        }
        setCurrentWidget(webPreviewer);
        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
}

void VEditTab::previewByConverter()
{
    VMarkdownConverter mdConverter;
    QString content = noteFile->content;
    QString html = mdConverter.generateHtml(content, vconfig.getMarkdownExtensions());
    QRegularExpression tocExp("<p>\\[TOC\\]<\\/p>", QRegularExpression::CaseInsensitiveOption);
    QString toc = mdConverter.generateToc(content, vconfig.getMarkdownExtensions());
    html.replace(tocExp, toc);
    QString completeHtml = VNote::preTemplateHtml + html + VNote::postTemplateHtml;
    webPreviewer->setHtml(completeHtml, QUrl::fromLocalFile(noteFile->basePath + QDir::separator()));
    // Hoedown will add '\n' while Marked does not
    updateTocFromHtml(toc.replace("\n", ""));
}

void VEditTab::showFileEditMode()
{
    isEditMode = true;
    textEditor->beginEdit();
    setCurrentWidget(textEditor);
    textEditor->setFocus();
}

bool VEditTab::requestClose()
{
    readFile();
    return !isEditMode;
}

void VEditTab::editFile()
{
    if (isEditMode || !noteFile->modifiable) {
        return;
    }

    showFileEditMode();
}

void VEditTab::readFile()
{
    if (!isEditMode) {
        return;
    }

    if (textEditor->isModified()) {
        // Need to save the changes
        QMessageBox msgBox(this);
        msgBox.setText(QString("The note \"%1\" has been modified.").arg(noteFile->fileName));
        msgBox.setInformativeText("Do you want to save your changes?");
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        int ret = msgBox.exec();
        switch (ret) {
        case QMessageBox::Save:
            saveFile();
            // Fall through
        case QMessageBox::Discard:
            textEditor->reloadFile();
            break;
        case QMessageBox::Cancel:
            // Nothing to do if user cancel this action
            return;
        default:
            qWarning() << "error: wrong return value from QMessageBox:" << ret;
            return;
        }
    }
    textEditor->setReadOnly(true);
    showFileReadMode();
}

bool VEditTab::saveFile()
{
    if (!isEditMode || !noteFile->modifiable || !textEditor->isModified()) {
        return true;
    }
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    QString filePath = QDir(noteFile->basePath).filePath(noteFile->fileName);
    if (!QFile(filePath).exists()) {
        qWarning() << "error:" << filePath << "being written has been removed";
        QMessageBox msgBox(QMessageBox::Warning, tr("Fail to save to file"),
                           QString("%1 being written has been removed.").arg(filePath),
                           QMessageBox::Ok, this);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        return false;
    }
    textEditor->saveFile();
    bool ret = VUtils::writeFileToDisk(filePath, noteFile->content);
    if (!ret) {
        QMessageBox msgBox(QMessageBox::Warning, tr("Fail to save to file"),
                           QString("Fail to write to disk when saving a note. Please try it again."),
                           QMessageBox::Ok, this);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        textEditor->setModified(true);
        return false;
    }
    return true;
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
    page->setWebChannel(channel);

    if (mdConverterType == MarkdownConverterType::Marked) {
        webPreviewer->setHtml(VNote::templateHtml,
                              QUrl::fromLocalFile(noteFile->basePath + QDir::separator()));
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
    qDebug() << tocHtml;
    tableOfContent.type = VHeaderType::Anchor;
    QVector<VHeader> &headers = tableOfContent.headers;
    headers.clear();

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

    tableOfContent.curHeaderIndex = 0;
    tableOfContent.filePath = QDir::cleanPath(QDir(noteFile->basePath).filePath(noteFile->fileName));
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

const VToc& VEditTab::getOutline() const
{
    return tableOfContent;
}

void VEditTab::scrollToAnchor(const VAnchor &anchor)
{
    if (isEditMode) {
        if (anchor.lineNumber > -1) {

        }
    } else {
        if (!anchor.anchor.isEmpty()) {
            document.scrollToAnchor(anchor.anchor.mid(1), mdConverterType);
        }
    }
}
