#include "markdowneditor.h"

#include <QRegularExpression>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QProgressDialog>
#include <QTemporaryFile>
#include <QTimer>
#include <QBuffer>
#include <QPainter>
#include <QHash>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/networkutils.h>
#include <vtextedit/theme.h>
#include <vtextedit/previewdata.h>
#include <vtextedit/textblockdata.h>

#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/dialogs/tableinsertdialog.h>
#include <widgets/messageboxhelper.h>

#include <widgets/dialogs/selectdialog.h>

#include <buffer/buffer.h>
#include <buffer/markdownbuffer.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/htmlutils.h>
#include <utils/widgetutils.h>
#include <utils/webutils.h>
#include <utils/imageutils.h>
#include <utils/clipboardutils.h>
#include <core/exception.h>
#include <core/markdowneditorconfig.h>
#include <core/texteditorconfig.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/vnotex.h>
#include <core/fileopenparameters.h>
#include <imagehost/imagehostutils.h>
#include <imagehost/imagehost.h>
#include <imagehost/imagehostmgr.h>

#include "previewhelper.h"
#include "../outlineprovider.h"
#include "markdowntablehelper.h"

using namespace vnotex;

MarkdownEditor::Heading::Heading(const QString &p_name,
                                 int p_level,
                                 const QString &p_sectionNumber,
                                 int p_blockNumber)
    : m_name(p_name),
      m_level(p_level),
      m_sectionNumber(p_sectionNumber),
      m_blockNumber(p_blockNumber)
{
}

MarkdownEditor::MarkdownEditor(const MarkdownEditorConfig &p_config,
                               const QSharedPointer<vte::MarkdownEditorConfig> &p_editorConfig,
                               const QSharedPointer<vte::TextEditorParameters> &p_editorParas,
                               QWidget *p_parent)
    : vte::VMarkdownEditor(p_editorConfig, p_editorParas, p_parent),
      m_config(p_config)
{
    setupShortcuts();

    connect(m_textEdit, &vte::VTextEdit::canInsertFromMimeDataRequested,
            this, &MarkdownEditor::handleCanInsertFromMimeData);
    connect(m_textEdit, &vte::VTextEdit::insertFromMimeDataRequested,
            this, &MarkdownEditor::handleInsertFromMimeData);
    connect(m_textEdit, &vte::VTextEdit::contextMenuEventRequested,
            this, &MarkdownEditor::handleContextMenuEvent);

    connect(getHighlighter(), &vte::PegMarkdownHighlighter::headersUpdated,
            this, &MarkdownEditor::updateHeadings);

    setupTableHelper();

    m_headingTimer = new QTimer(this);
    m_headingTimer->setInterval(500);
    m_headingTimer->setSingleShot(true);
    connect(m_headingTimer, &QTimer::timeout,
            this, &MarkdownEditor::currentHeadingChanged);
    connect(m_textEdit, &vte::VTextEdit::cursorLineChanged,
            m_headingTimer, QOverload<>::of(&QTimer::start));

    m_sectionNumberTimer = new QTimer(this);
    m_sectionNumberTimer->setInterval(1000);
    m_sectionNumberTimer->setSingleShot(true);
    connect(m_sectionNumberTimer, &QTimer::timeout,
            this, [this]() {
                updateSectionNumber(m_headings);
            });

    updateFromConfig(false);
}

MarkdownEditor::~MarkdownEditor()
{

}

void MarkdownEditor::setPreviewHelper(PreviewHelper *p_helper)
{
    auto highlighter = getHighlighter();
    connect(highlighter, &vte::PegMarkdownHighlighter::codeBlocksUpdated,
            p_helper, &PreviewHelper::codeBlocksUpdated);
    connect(highlighter, &vte::PegMarkdownHighlighter::mathBlocksUpdated,
            p_helper, &PreviewHelper::mathBlocksUpdated);

    auto previewMgr = getPreviewMgr();
    connect(p_helper, &PreviewHelper::inplacePreviewCodeBlockUpdated,
            previewMgr, &vte::PreviewMgr::updateCodeBlocks);
    connect(p_helper, &PreviewHelper::inplacePreviewMathBlockUpdated,
            previewMgr, &vte::PreviewMgr::updateMathBlocks);
    connect(p_helper, &PreviewHelper::potentialObsoletePreviewBlocksUpdated,
            previewMgr, &vte::PreviewMgr::checkBlocksForObsoletePreview);
}

void MarkdownEditor::typeHeading(int p_level)
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeHeading(m_textEdit, p_level);
}

void MarkdownEditor::typeBold()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeBold(m_textEdit);
}

void MarkdownEditor::typeItalic()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeItalic(m_textEdit);
}

void MarkdownEditor::typeStrikethrough()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeStrikethrough(m_textEdit);
}

void MarkdownEditor::typeMark()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeMark(m_textEdit);
}

void MarkdownEditor::typeUnorderedList()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeUnorderedList(m_textEdit);
}

void MarkdownEditor::typeOrderedList()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeOrderedList(m_textEdit);
}

void MarkdownEditor::typeTodoList(bool p_checked)
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeTodoList(m_textEdit, p_checked);
}

void MarkdownEditor::typeCode()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeCode(m_textEdit);
}

void MarkdownEditor::typeCodeBlock()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeCodeBlock(m_textEdit);
}

void MarkdownEditor::typeMath()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeMath(m_textEdit);
}

void MarkdownEditor::typeMathBlock()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeMathBlock(m_textEdit);
}

void MarkdownEditor::typeQuote()
{
    enterInsertModeIfApplicable();
    vte::MarkdownUtils::typeQuote(m_textEdit);
}

void MarkdownEditor::typeLink()
{
    QString linkText;
    QString linkUrl;

    // Try get Url or text from selection.
    auto cursor = m_textEdit->textCursor();
    QRegularExpression urlReg("[\\.\\\\/]");
    if (cursor.hasSelection()) {
        auto text = vte::TextEditUtils::getSelectedText(cursor).trimmed();
        if (!text.isEmpty() && !text.contains(QLatin1Char('\n'))) {
            if (text.contains(urlReg) && QUrl::fromUserInput(text).isValid()) {
                linkUrl = text;
            } else {
                linkText = text;
            }
        }
    }

    // Fetch link from clipboard.
    if (linkUrl.isEmpty() && linkText.isEmpty()) {
        const auto clipboard = QApplication::clipboard();
        const auto mimeData = clipboard->mimeData();
        const QString text = mimeData->text().trimmed();
        // No multi-line.
        if (!text.isEmpty() && !text.contains(QLatin1Char('\n'))) {
            if (text.contains(urlReg) && QUrl::fromUserInput(text).isValid()) {
                linkUrl = text;
            } else {
                linkText = text;
            }
        }
    }

    LinkInsertDialog dialog(tr("Insert Link"), linkText, linkUrl, false, this);
    if (dialog.exec() == QDialog::Accepted) {
        linkText = dialog.getLinkText();
        linkUrl = dialog.getLinkUrl();

        enterInsertModeIfApplicable();
        vte::MarkdownUtils::typeLink(m_textEdit, linkText, linkUrl);
    }
}

void MarkdownEditor::typeImage()
{
    Q_ASSERT(m_buffer);
    ImageInsertDialog dialog(tr("Insert Image"), "", "", "", true, this);

    // Try fetch image from clipboard.
    {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();

        QUrl url;
        if (mimeData->hasImage()) {
            QImage im = qvariant_cast<QImage>(mimeData->imageData());
            if (im.isNull()) {
                return;
            }

            dialog.setImage(im);
            dialog.setImageSource(ImageInsertDialog::Source::ImageData);
        } else if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            if (urls.size() == 1) {
                url = urls[0];
            }
        } else if (mimeData->hasText()) {
            url = QUrl::fromUserInput(mimeData->text());
        }

        if (url.isValid()) {
            if (url.isLocalFile()) {
                dialog.setImagePath(url.toLocalFile());
            } else {
                dialog.setImagePath(url.toString());
            }
        }
    }

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    enterInsertModeIfApplicable();
    if (dialog.getImageSource() == ImageInsertDialog::Source::LocalFile) {
        insertImageToBufferFromLocalFile(dialog.getImageTitle(),
                                         dialog.getImageAltText(),
                                         dialog.getImagePath(),
                                         dialog.getScaledWidth());
    } else {
        auto image = dialog.getImage();
        if (!image.isNull()) {
            insertImageToBufferFromData(dialog.getImageTitle(),
                                        dialog.getImageAltText(),
                                        image,
                                        dialog.getScaledWidth());
        }
    }
}

void MarkdownEditor::typeTable()
{
    TableInsertDialog dialog(tr("Insert Table"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto cursor = m_textEdit->textCursor();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        cursor.setPosition(qMax(cursor.selectionStart(), cursor.selectionEnd()));
    }

    bool newBlock = !cursor.atBlockEnd();
    if (!newBlock && !cursor.atBlockStart()) {
        QString text = cursor.block().text().trimmed();
        if (!text.isEmpty() && text != QStringLiteral(">")) {
            // Insert a new block before inserting table.
            newBlock = true;
        }
    }

    if (newBlock) {
        auto indentationStr = vte::TextEditUtils::fetchIndentationSpaces(cursor.block());
        vte::TextEditUtils::insertBlock(cursor, false);
        cursor.insertText(indentationStr);
    }

    cursor.endEditBlock();
    m_textEdit->setTextCursor(cursor);

    // Insert table.
    m_tableHelper->insertTable(dialog.getRowCount(), dialog.getColumnCount(), dialog.getAlignment());
}

void MarkdownEditor::setBuffer(Buffer *p_buffer)
{
    m_buffer = p_buffer;
}

bool MarkdownEditor::insertImageToBufferFromLocalFile(const QString &p_title,
                                                      const QString &p_altText,
                                                      const QString &p_srcImagePath,
                                                      int p_scaledWidth,
                                                      int p_scaledHeight,
                                                      bool p_insertText,
                                                      QString *p_urlInLink)
{
    auto destFileName = generateImageFileNameToInsertAs(p_title, QFileInfo(p_srcImagePath).suffix());

    QString destFilePath;

    if (m_imageHost) {
        // Save to image host.
        QByteArray ba;
        try {
            ba = FileUtils::readFile(p_srcImagePath);
        } catch (Exception &e) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     QString("Failed to read local image file (%1) (%2).").arg(p_srcImagePath, e.what()),
                                     this);
            return false;
        }
        destFilePath = saveToImageHost(ba, destFileName);
        if (destFilePath.isEmpty()) {
            return false;
        }
    } else {
        try {
            destFilePath = m_buffer->insertImage(p_srcImagePath, destFileName);
        } catch (Exception &e) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     QString("Failed to insert image from local file (%1) (%2).").arg(p_srcImagePath, e.what()),
                                     this);
            return false;
        }
    }

    insertImageLink(p_title, p_altText, destFilePath, p_scaledWidth, p_scaledHeight, p_insertText, p_urlInLink);
    return true;
}

QString MarkdownEditor::generateImageFileNameToInsertAs(const QString &p_title, const QString &p_suffix)
{
    return FileUtils::generateRandomFileName(p_title, p_suffix);
}

bool MarkdownEditor::insertImageToBufferFromData(const QString &p_title,
                                                 const QString &p_altText,
                                                 const QImage &p_image,
                                                 int p_scaledWidth,
                                                 int p_scaledHeight)
{
    // Save as PNG by default.
    const QString format("png");
    const auto destFileName = generateImageFileNameToInsertAs(p_title, format);

    QString destFilePath;

    if (m_imageHost) {
        // Save to image host.
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        p_image.save(&buffer, format.toStdString().c_str());

        destFilePath = saveToImageHost(ba, destFileName);
        if (destFilePath.isEmpty()) {
            return false;
        }
    } else {
        try {
            destFilePath = m_buffer->insertImage(p_image, destFileName);
        } catch (Exception &e) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     QString("Failed to insert image from data (%1).").arg(e.what()),
                                     this);
            return false;
        }
    }

    insertImageLink(p_title, p_altText, destFilePath, p_scaledWidth, p_scaledHeight);
    return true;
}

void MarkdownEditor::insertImageLink(const QString &p_title,
                                    const QString &p_altText,
                                    const QString &p_destImagePath,
                                    int p_scaledWidth,
                                    int p_scaledHeight,
                                    bool p_insertText,
                                    QString *p_urlInLink)
{
    const auto urlInLink = getRelativeLink(p_destImagePath);
    if (p_urlInLink) {
        *p_urlInLink = urlInLink;
    }
    static_cast<MarkdownBuffer *>(m_buffer)->addInsertedImage(p_destImagePath, urlInLink);
    if (p_insertText) {
        const auto imageLink = vte::MarkdownUtils::generateImageLink(p_title,
                                                                     urlInLink,
                                                                     p_altText,
                                                                     p_scaledWidth,
                                                                     p_scaledHeight);
        m_textEdit->insertPlainText(imageLink);
    }
}

void MarkdownEditor::handleCanInsertFromMimeData(const QMimeData *p_source, bool *p_handled, bool *p_allowed)
{
    m_shouldTriggerRichPaste = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getRichPasteByDefaultEnabled();

    if (m_plainTextPasteAsked) {
        m_shouldTriggerRichPaste = false;
        return;
    }

    if (m_richPasteAsked) {
        m_shouldTriggerRichPaste = true;
        *p_handled = true;
        *p_allowed = true;
        return;
    }

    if (QGuiApplication::keyboardModifiers() == Qt::ShiftModifier) {
        m_shouldTriggerRichPaste = !m_shouldTriggerRichPaste;
    }

    if (m_shouldTriggerRichPaste) {
        *p_handled = true;
        *p_allowed = true;
        return;
    }

    if (p_source->hasImage()) {
        m_shouldTriggerRichPaste = true;
        *p_handled = true;
        *p_allowed = true;
        return;
    }

    if (p_source->hasUrls()) {
        *p_handled = true;
        *p_allowed = true;
        return;
    }
}

void MarkdownEditor::handleInsertFromMimeData(const QMimeData *p_source, bool *p_handled)
{
    if (!m_shouldTriggerRichPaste) {
        // Default paste.
        // Give tips about the Rich Paste and Parse to Markdown And Paste features.
        VNoteX::getInst().showStatusMessageShort(
            tr("For advanced paste, try the \"Rich Paste\" and \"Parse to Markdown and Paste\" on the editor's context menu"));
        return;
    }

    m_shouldTriggerRichPaste = false;

    if (processHtmlFromMimeData(p_source)) {
        *p_handled = true;
        return;
    }

    if (processImageFromMimeData(p_source)) {
        *p_handled = true;
        return;
    }

    if (processUrlFromMimeData(p_source)) {
        *p_handled = true;
        return;
    }

    if (processMultipleUrlsFromMimeData(p_source)) {
        *p_handled = true;
        return;
    }
}

bool MarkdownEditor::processHtmlFromMimeData(const QMimeData *p_source)
{
    if (!p_source->hasHtml()) {
        return false;
    }

    const QString html(p_source->html());

    // Process <img>.
    QRegExp reg("<img ([^>]*)src=\"([^\"]+)\"([^>]*)>");
    if (reg.indexIn(html) != -1 && HtmlUtils::hasOnlyImgTag(html)) {
        if (p_source->hasImage()) {
            // Both image data and URL are embedded.
            SelectDialog dialog(tr("Insert From Clipboard"), this);
            dialog.addSelection(tr("Insert From URL"), 0);
            dialog.addSelection(tr("Insert From Image Data"), 1);
            dialog.addSelection(tr("Insert As Image Link"), 2);

            if (dialog.exec() == QDialog::Accepted) {
                int selection = dialog.getSelection();
                if (selection == 1) {
                    // Insert from image data.
                    insertImageFromMimeData(p_source);
                    return true;
                } else if (selection == 2) {
                    // Insert as link.
                    auto imageLink = vte::MarkdownUtils::generateImageLink("", reg.cap(2), "");
                    m_textEdit->insertPlainText(imageLink);
                    return true;
                }
            } else {
                return true;
            }
        }

        insertImageFromUrl(reg.cap(2));
        return true;
    }

    return false;
}

bool MarkdownEditor::processImageFromMimeData(const QMimeData *p_source)
{
    if (!p_source->hasImage()) {
        return false;
    }

    // Image url in the clipboard.
    if (p_source->hasText()) {
        SelectDialog dialog(tr("Insert From Clipboard"), this);
        dialog.addSelection(tr("Insert As Image"), 0);
        dialog.addSelection(tr("Insert As Text"), 1);
        dialog.addSelection(tr("Insert As Image Link"), 2);

        if (dialog.exec() == QDialog::Accepted) {
            int selection = dialog.getSelection();
            if (selection == 1) {
                // Insert as text.
                Q_ASSERT(p_source->hasText() && p_source->hasImage());
                m_textEdit->insertFromMimeDataOfBase(p_source);
                return true;
            } else if (selection == 2) {
                // Insert as link.
                auto imageLink = vte::MarkdownUtils::generateImageLink("", p_source->text(), "");
                m_textEdit->insertPlainText(imageLink);
                return true;
            }
        } else {
            return true;
        }
    }

    insertImageFromMimeData(p_source);
    return true;
}

bool MarkdownEditor::processUrlFromMimeData(const QMimeData *p_source)
{
    const auto urls = p_source->urls();
    if (urls.size() > 1) {
        return false;
    }

    QUrl url;
    if (p_source->hasUrls()) {
        if (urls.size() == 1) {
            url = urls[0];
        }
    } else if (p_source->hasText()) {
        // Try to get URL from text.
        const QString text = p_source->text();
        if (QFileInfo::exists(text)) {
            url = QUrl::fromLocalFile(text);
        } else {
            url.setUrl(text);
            if (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) {
                url.clear();
            }
        }
    }

    if (!url.isValid()) {
        return false;
    }

    const bool isImage = PathUtils::isImageUrl(PathUtils::urlToPath(url));
    QString localFile = url.toLocalFile();
    if (!url.isLocalFile() || !QFileInfo::exists(localFile)) {
        localFile.clear();
    }

    bool isTextFile = false;
    if (!isImage && !localFile.isEmpty()) {
        const auto mimeType = QMimeDatabase().mimeTypeForFile(localFile);
        if (mimeType.isValid() && mimeType.inherits(QStringLiteral("text/plain"))) {
            isTextFile = true;
        }
    }

    SelectDialog dialog(tr("Insert From Clipboard"), this);
    if (isImage) {
        dialog.addSelection(tr("Insert As Image"), 0);
        dialog.addSelection(tr("Insert As Image Link"), 1);
        if (!localFile.isEmpty()) {
            dialog.addSelection(tr("Insert As Relative Image Link"), 7);
        }
    }

    dialog.addSelection(tr("Insert As Link"), 2);
    if (!localFile.isEmpty()) {
        dialog.addSelection(tr("Insert As Relative Link"), 3);

        if (m_buffer->isAttachmentSupported() && !m_buffer->isAttachment(localFile) && !PathUtils::isDir(localFile)) {
            dialog.addSelection(tr("Attach And Insert Link"), 6);
        }
    }

    dialog.addSelection(tr("Insert As Text"), 4);
    if (!localFile.isEmpty() && isTextFile) {
        dialog.addSelection(tr("Insert File Content"), 5);
    }

    // FIXME: After calling dialog.exec(), p_source->hasUrl() returns false.
    if (dialog.exec() == QDialog::Accepted) {
        bool relativeLink = false;
        switch (dialog.getSelection()) {
        case 0:
        {
            // Insert As Image.
            insertImageFromUrl(PathUtils::urlToPath(url));
            return true;
        }

        case 7:
            // Insert As Relative Image Link.
            relativeLink = true;
            Q_FALLTHROUGH();

        case 1:
        {
            // Insert As Image Link.
            QString urlInLink;
            if (relativeLink) {
                urlInLink = getRelativeLink(localFile);
            } else {
                urlInLink = url.toString(QUrl::EncodeSpaces);
            }

            enterInsertModeIfApplicable();
            const auto imageLink = vte::MarkdownUtils::generateImageLink("", urlInLink, "");
            m_textEdit->insertPlainText(imageLink);
            return true;
        }

        case 6:
        {
            // Attach And Insert Link.
            QStringList fileList;
            fileList << localFile;
            fileList = m_buffer->addAttachment(QString(), fileList);

            // Update localFile to point to the attachment file.
            localFile = fileList[0];
            Q_FALLTHROUGH();
        }

        case 3:
            // Insert As Relative link.
            relativeLink = true;
            Q_FALLTHROUGH();

        case 2:
        {
            // Insert As Link.
            QString linkText;
            if (!localFile.isEmpty()) {
                linkText = QFileInfo(localFile).fileName();
            }

            QString linkUrl;
            if (relativeLink) {
                Q_ASSERT(!localFile.isEmpty());
                linkUrl = getRelativeLink(localFile);
            } else {
                linkUrl = url.toString(QUrl::EncodeSpaces);
            }

            LinkInsertDialog linkDialog(tr("Insert Link"), linkText, linkUrl, false, this);
            if (linkDialog.exec() == QDialog::Accepted) {
                linkText = linkDialog.getLinkText();
                linkUrl = linkDialog.getLinkUrl();

                enterInsertModeIfApplicable();
                vte::MarkdownUtils::typeLink(m_textEdit, linkText, linkUrl);
            }

            return true;
        }

        case 4:
        {
            // Insert As Text.
            enterInsertModeIfApplicable();
            if (p_source->hasText()) {
                m_textEdit->insertPlainText(p_source->text());
            } else {
                m_textEdit->insertPlainText(url.toString());
            }

            return true;
        }

        case 5:
        {
            // Insert File Content.
            Q_ASSERT(!localFile.isEmpty() && isTextFile);
            enterInsertModeIfApplicable();
            m_textEdit->insertPlainText(FileUtils::readTextFile(localFile));
            return true;
        }

        default:
            Q_ASSERT(false);
            break;
        }
    } else {
        // Nothing happens.
        return true;
    }

    return false;
}

bool MarkdownEditor::processMultipleUrlsFromMimeData(const QMimeData *p_source) {
    const auto urls = p_source->urls();
    if (urls.size() <= 1) {
        return false;
    }

    bool isProcessed = false;
    // Judgment if all QMimeData are images.
    bool isAllImage = true;
    for (const QUrl &url : urls) {
        if (!PathUtils::isImageUrl(PathUtils::urlToPath(url))) {
            isAllImage = false;
            break;
        }
    }

    SelectDialog dialog(tr("Insert From Clipboard (%n items)", "", urls.size()), this);
    if (isAllImage) {
        dialog.addSelection(tr("Insert As Image"), 0);
    }
    if (m_buffer->isAttachmentSupported()) {
        dialog.addSelection(tr("Attach And Insert Link"), 1);
    }
    dialog.setMinimumWidth(400);

    if (dialog.exec() == QDialog::Accepted) {
        switch (dialog.getSelection()) {
        case 0:
        {
            // Insert As Image.
            for (const QUrl &url : urls) {
                insertImageFromUrl(PathUtils::urlToPath(url), true);
                m_textEdit->insertPlainText("\n\n");
            }
            isProcessed = true;
            break;
        }
        case 1:
        {
            // Attach And Insert Link.
            QStringList fileList;
            for (const QUrl &url : urls) {
                fileList << url.toLocalFile();
            }
            fileList = m_buffer->addAttachment(QString(), fileList);
            enterInsertModeIfApplicable();
            for (int i = 0; i < fileList.length(); ++i) {
                vte::MarkdownUtils::typeLink(
                            m_textEdit, QFileInfo(fileList[i]).fileName(),
                            getRelativeLink(fileList[i]));

                m_textEdit->insertPlainText("\n\n");
            }
            isProcessed = true;
            break;
        }
        }
    }

    return isProcessed;
}

void MarkdownEditor::insertImageFromMimeData(const QMimeData *p_source)
{
    QImage image = qvariant_cast<QImage>(p_source->imageData());
    if (image.isNull()) {
        return;
    }

    ImageInsertDialog dialog(tr("Insert Image From Clipboard"), "", "", "", false, this);
    dialog.setImage(image);
    if (dialog.exec() == QDialog::Accepted) {
        enterInsertModeIfApplicable();
        insertImageToBufferFromData(dialog.getImageTitle(),
                                    dialog.getImageAltText(),
                                    image,
                                    dialog.getScaledWidth());
    }
}

void MarkdownEditor::insertImageFromUrl(const QString &p_url, bool p_quiet)
{
    if (p_quiet) {
        insertImageToBufferFromLocalFile("", "", p_url, 0);
    } else {
        ImageInsertDialog dialog(tr("Insert Image From URL"), "", "", "", false, this);
        dialog.setImagePath(p_url);
        if (dialog.exec() == QDialog::Accepted) {
            enterInsertModeIfApplicable();
            if (dialog.getImageSource() == ImageInsertDialog::Source::LocalFile) {
                insertImageToBufferFromLocalFile(dialog.getImageTitle(),
                                                 dialog.getImageAltText(),
                                                 dialog.getImagePath(),
                                                 dialog.getScaledWidth());
            } else {
                auto image = dialog.getImage();
                if (!image.isNull()) {
                    insertImageToBufferFromData(dialog.getImageTitle(),
                                                dialog.getImageAltText(),
                                                image,
                                                dialog.getScaledWidth());
                }
            }
        }
    }
}

QString MarkdownEditor::getRelativeLink(const QString &p_path)
{
    if (PathUtils::isLocalFile(p_path)) {
        auto relativePath = PathUtils::relativePath(PathUtils::parentDirPath(m_buffer->getContentPath()), p_path);
        auto link = PathUtils::encodeSpacesInPath(QDir::fromNativeSeparators(relativePath));
        if (m_config.getPrependDotInRelativeLink()) {
            PathUtils::prependDotIfRelative(link);
        }

        return link;
    } else {
        return p_path;
    }
}

const QVector<MarkdownEditor::Heading> &MarkdownEditor::getHeadings() const
{
    return m_headings;
}

int MarkdownEditor::getCurrentHeadingIndex() const
{
    int blockNumber = m_textEdit->textCursor().blockNumber();
    return getHeadingIndexByBlockNumber(blockNumber);
}

void MarkdownEditor::updateHeadings(const QVector<vte::peg::ElementRegion> &p_headerRegions)
{
    bool needUpdateSectionNumber = false;
    if (isReadOnly()) {
        m_sectionNumberEnabled = false;
    } else {
        needUpdateSectionNumber = m_config.getSectionNumberMode() == MarkdownEditorConfig::SectionNumberMode::Edit;
        if (m_overriddenSectionNumber != OverrideState::NoOverride) {
            needUpdateSectionNumber = m_overriddenSectionNumber == OverrideState::ForceEnable;
        }
        if (needUpdateSectionNumber) {
            m_sectionNumberEnabled = true;
        } else if (m_sectionNumberEnabled) {
            // On -> Off. We still need to do the clean up.
            needUpdateSectionNumber = true;
            m_sectionNumberEnabled = false;
        }
    }

    QVector<Heading> headings;
    headings.reserve(p_headerRegions.size());

    // Assume that each block contains only one line.
    // Only support # syntax for now.
    auto doc = document();
    for (auto const &reg : p_headerRegions) {
        auto block = doc->findBlock(reg.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        if (!block.contains(reg.m_endPos - 1)) {
            qWarning() << "header accross multiple blocks, starting from block" << block.blockNumber() << block.text();
        }

        auto match = vte::MarkdownUtils::matchHeader(block.text());
        if (match.m_matched) {
            Heading heading(match.m_header,
                            match.m_level,
                            match.m_sequence,
                            block.blockNumber());
            headings.append(heading);
        }
    }

    OutlineProvider::makePerfectHeadings(headings, m_headings);

    if (needUpdateSectionNumber) {
        // Use a timer to kick off the update to let user have time to undo.
        m_sectionNumberTimer->start();
    }

    emit headingsChanged();

    emit currentHeadingChanged();
}

int MarkdownEditor::getHeadingIndexByBlockNumber(int p_blockNumber) const
{
    if (m_headings.isEmpty()) {
        return -1;
    }

    int left = 0, right = m_headings.size() - 1;
    while (left < right) {
        int mid = left + (right - left + 1) / 2;
        int val = m_headings[mid].m_blockNumber;
        if (val == -1) {
            // Search to right.
            for (int i = mid + 1; i <= right; ++i) {
                if (m_headings[i].m_blockNumber != -1) {
                    mid = i;
                    val = m_headings[i].m_blockNumber;
                    break;
                }
            }

            if (val == -1) {
                // Search to left.
                for (int i = mid - 1; i >= left; --i) {
                    if (m_headings[i].m_blockNumber != -1) {
                        mid = i;
                        val = m_headings[i].m_blockNumber;
                        break;
                    }
                }
            }
        }

        if (val == -1) {
            // No more valid values.
            break;
        }

        if (val == p_blockNumber) {
            return mid;
        } else if (val > p_blockNumber) {
            // Skip the -1 headings.
            // Bad case: [0, 2, 3, 43, 44, -1, 46, 60].
            // If not skipped, [left, right] will be stuck at [4, 5].
            right = mid - 1;
            while (right >= left && m_headings[right].m_blockNumber == -1) {
                --right;
            }
        } else {
            left = mid;
        }
    }

    if (m_headings[left].m_blockNumber <= p_blockNumber && m_headings[left].m_blockNumber != -1) {
        return left;
    }

    return -1;
}

void MarkdownEditor::scrollToHeading(int p_idx)
{
    if (p_idx < 0 || p_idx >= m_headings.size()) {
        return;
    }

    if (m_headings[p_idx].m_blockNumber == -1) {
        return;
    }

    scrollToLine(m_headings[p_idx].m_blockNumber, true);
}

void MarkdownEditor::handleContextMenuEvent(QContextMenuEvent *p_event, bool *p_handled, QScopedPointer<QMenu> *p_menu)
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    *p_handled = true;
    p_menu->reset(m_textEdit->createStandardContextMenu(p_event->pos()));
    auto menu = p_menu->data();

    const auto actions = menu->actions();
    QAction *firstAct = actions.isEmpty() ? nullptr : actions.first();
    // QAction *copyAct = WidgetUtils::findActionByObjectName(actions, "edit-copy");
    QAction *pasteAct = WidgetUtils::findActionByObjectName(actions, "edit-paste");

    const bool hasSelection = m_textEdit->hasSelection();

    if (!hasSelection) {
        auto readAct = new QAction(tr("&Read"), menu);
        WidgetUtils::addActionShortcutText(readAct, editorConfig.getShortcut(EditorConfig::Shortcut::EditRead));
        connect(readAct, &QAction::triggered,
                this, &MarkdownEditor::readRequested);
        menu->insertAction(firstAct, readAct);
        if (firstAct) {
            menu->insertSeparator(firstAct);
        }

        prependContextSensitiveMenu(menu, p_event->pos());
    }

    if (pasteAct && pasteAct->isEnabled()) {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();

        // Rich Paste or Plain Text Paste.
        const bool richPasteByDefault = editorConfig.getMarkdownEditorConfig().getRichPasteByDefaultEnabled();
        auto altPasteAct = new QAction(richPasteByDefault ? tr("Paste as Plain Text") : tr("Rich Paste"), menu);
        WidgetUtils::addActionShortcutText(altPasteAct,
                                           editorConfig.getShortcut(EditorConfig::Shortcut::AltPaste));
        connect(altPasteAct, &QAction::triggered,
                this, &MarkdownEditor::altPaste);
        WidgetUtils::insertActionAfter(menu, pasteAct, altPasteAct);

        if (mimeData->hasHtml()) {
            // Parse to Markdown and Paste.
            auto parsePasteAct = new QAction(tr("Parse to Markdown and Paste"), menu);
            WidgetUtils::addActionShortcutText(parsePasteAct,
                                               editorConfig.getShortcut(EditorConfig::Shortcut::ParseToMarkdownAndPaste));
            connect(parsePasteAct, &QAction::triggered,
                    this, &MarkdownEditor::parseToMarkdownAndPaste);
            WidgetUtils::insertActionAfter(menu, altPasteAct, parsePasteAct);
        }
    }

    {
        menu->addSeparator();

        auto snippetAct = menu->addAction(tr("Insert Snippet"), this, &MarkdownEditor::applySnippetRequested);
        WidgetUtils::addActionShortcutText(snippetAct,
                                           editorConfig.getShortcut(EditorConfig::Shortcut::ApplySnippet));
    }

    if (!hasSelection) {
        appendImageHostMenu(menu);
    }

    appendSpellCheckMenu(p_event, menu);
}

void MarkdownEditor::altPaste()
{
    const bool richPasteByDefault = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getRichPasteByDefaultEnabled();

    if (richPasteByDefault) {
        // Paste as plain text.
        m_plainTextPasteAsked = true;
        m_richPasteAsked = false;
    } else {
        // Rich paste.
        m_plainTextPasteAsked = false;
        m_richPasteAsked = true;
    }

    // handleCanInsertFromMimeData() is called before this function. Call it manually.
    if (m_textEdit->canPaste()) {
        m_textEdit->paste();
    }

    m_plainTextPasteAsked = false;
    m_richPasteAsked = false;
}

void MarkdownEditor::setupShortcuts()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    // Alt paste.
    {
        auto shortcut = WidgetUtils::createShortcut(editorConfig.getShortcut(EditorConfig::Shortcut::AltPaste),
                                                    this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &MarkdownEditor::altPaste);
        }
    }

    // Parse to Markdown and Paste.
    {
        auto shortcut = WidgetUtils::createShortcut(editorConfig.getShortcut(EditorConfig::Shortcut::ParseToMarkdownAndPaste),
                                                    this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &MarkdownEditor::parseToMarkdownAndPaste);
        }
    }
}

void MarkdownEditor::parseToMarkdownAndPaste()
{
    if (isReadOnly()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    QString html(mimeData->html());
    if (!html.isEmpty()) {
        emit htmlToMarkdownRequested(0, ++m_timeStamp, html);
    }
}

void MarkdownEditor::handleHtmlToMarkdownData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_text)
{
    Q_UNUSED(p_id);
    if (m_timeStamp == p_timeStamp && !p_text.isEmpty()) {
        QString text(p_text);

        const auto &editorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
        if (editorConfig.getFetchImagesInParseAndPaste()) {
            fetchImagesToLocalAndReplace(text);
        }

        insertText(text);
    }
}

static QString purifyImageTitle(QString p_title)
{
    return p_title.remove(QRegExp("[\\r\\n\\[\\]]"));
}

void MarkdownEditor::fetchImagesToLocalAndReplace(QString &p_text)
{
    auto regs = vte::MarkdownUtils::fetchImageRegionsViaParser(p_text);
    if (regs.isEmpty()) {
        return;
    }

    // Sort it in ascending order.
    std::sort(regs.begin(), regs.end());

    QProgressDialog proDlg(tr("Fetching images to local..."),
                           tr("Abort"),
                           0,
                           regs.size(),
                           this);
    proDlg.setWindowModality(Qt::WindowModal);
    proDlg.setWindowTitle(tr("Fetch Images To Local"));

    QRegExp zhihuRegExp("^https?://www\\.zhihu\\.com/equation\\?tex=(.+)$");

    QRegExp regExp(vte::MarkdownUtils::c_imageLinkRegExp);
    for (int i = regs.size() - 1; i >= 0; --i) {
        proDlg.setValue(regs.size() - 1 - i);
        if (proDlg.wasCanceled()) {
            break;
        }

        const auto &reg = regs[i];
        QString linkText = p_text.mid(reg.m_startPos, reg.m_endPos - reg.m_startPos);
        if (regExp.indexIn(linkText) == -1) {
            continue;
        }

        qDebug() << "fetching image link" << linkText;

        const QString imageTitle = purifyImageTitle(regExp.cap(1).trimmed());
        QString imageUrl = regExp.cap(2).trimmed();

        const int maxUrlLength = 100;
        QString urlToDisplay(imageUrl);
        if (urlToDisplay.size() > maxUrlLength) {
            urlToDisplay = urlToDisplay.left(maxUrlLength) + "...";
        }
        proDlg.setLabelText(tr("Fetching image (%1)").arg(urlToDisplay));

        // Handle equation from zhihu.com like http://www.zhihu.com/equation?tex=P.
        if (zhihuRegExp.indexIn(imageUrl) != -1) {
            QString tex = zhihuRegExp.cap(1).trimmed();

            // Remove the +.
            tex.replace(QChar('+'), " ");

            tex = QUrl::fromPercentEncoding(tex.toUtf8());
            if (tex.isEmpty()) {
                continue;
            }

            tex = "$" + tex + "$";
            p_text.replace(reg.m_startPos,
                           reg.m_endPos - reg.m_startPos,
                           tex);
            continue;
        }

        // Only handle absolute file path or network path.
        QString srcImagePath;
        QFileInfo info(WebUtils::purifyUrl(imageUrl));

        // For network image.
        QScopedPointer<QTemporaryFile> tmpFile;

        if (info.exists()) {
            if (info.isAbsolute()) {
                // Absolute local path.
                srcImagePath = info.absoluteFilePath();
            }
        } else {
            // Network path.
            // Prepend the protocol if missing.
            if (imageUrl.startsWith(QStringLiteral("//"))) {
                imageUrl.prepend(QStringLiteral("https:"));
            }
            QByteArray data = vte::NetworkAccess::request(QUrl(imageUrl)).m_data;
            if (!data.isEmpty()) {
                // Prefer the suffix from the real data.
                auto suffix = ImageUtils::guessImageSuffix(data);
                if (suffix.isEmpty()) {
                    suffix = info.suffix();
                } else if (info.suffix() != suffix) {
                    qWarning() << "guess a different suffix from image data" << info.suffix() << suffix;
                }
                tmpFile.reset(FileUtils::createTemporaryFile(suffix));
                if (tmpFile->open() && tmpFile->write(data) > -1) {
                    srcImagePath = tmpFile->fileName();
                }

                // Need to close it explicitly to flush cache of small file.
                tmpFile->close();
            }
        }

        if (srcImagePath.isEmpty()) {
            continue;
        }

        // Insert image without inserting text.
        QString urlInLink;
        bool ret = insertImageToBufferFromLocalFile(imageTitle,
                                                    QString(),
                                                    srcImagePath,
                                                    0,
                                                    0,
                                                    false,
                                                    &urlInLink);
        if (!ret || urlInLink.isEmpty()) {
            continue;
        }

        // Replace URL in link.
        QString newLink = QString("![%1](%2%3%4)")
                                 .arg(imageTitle, urlInLink, regExp.cap(3), regExp.cap(6));
        p_text.replace(reg.m_startPos,
                       reg.m_endPos - reg.m_startPos,
                       newLink);
    }

    proDlg.setValue(regs.size());
}

static bool updateHeadingSectionNumber(QTextCursor &p_cursor,
                                       const QTextBlock &p_block,
                                       const QString &p_sectionNumber,
                                       bool p_endingDot)
{
    if (!p_block.isValid()) {
        return false;
    }

    QString text = p_block.text();
    auto match = vte::MarkdownUtils::matchHeader(text);
    Q_ASSERT(match.m_matched);

    bool isSequence = false;
    if (!match.m_sequence.isEmpty()) {
        // Check if this sequence is the real sequence matching current style.
        if (match.m_sequence.endsWith('.')) {
            isSequence = p_endingDot;
        } else {
            isSequence = !p_endingDot;
        }
    }

    int start = match.m_level + 1;
    int end = match.m_level + match.m_spacesAfterMarker;
    if (isSequence) {
        end += match.m_sequence.size() + match.m_spacesAfterSequence;
    }

    Q_ASSERT(start <= end);

    p_cursor.setPosition(p_block.position() + start);
    if (start != end) {
        p_cursor.setPosition(p_block.position() + end, QTextCursor::KeepAnchor);
    }

    if (p_sectionNumber.isEmpty()) {
        p_cursor.removeSelectedText();
    } else {
        p_cursor.insertText(p_sectionNumber + ' ');
    }
    return true;
}

bool MarkdownEditor::updateSectionNumber(const QVector<Heading> &p_headings)
{
    SectionNumber sectionNumber(7, 0);
    int baseLevel = m_config.getSectionNumberBaseLevel();
    if (baseLevel < 1 || baseLevel > 6) {
        baseLevel = 1;
    }

    bool changed = false;
    bool endingDot = m_config.getSectionNumberStyle() == MarkdownEditorConfig::SectionNumberStyle::DigDotDigDot;
    auto doc = document();
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (const auto &heading : p_headings) {
        OutlineProvider::increaseSectionNumber(sectionNumber, heading.m_level, baseLevel);
        auto sectionStr = m_sectionNumberEnabled ? OutlineProvider::joinSectionNumber(sectionNumber, endingDot) : QString();
        if (heading.m_blockNumber > -1 && sectionStr != heading.m_sectionNumber) {
            if (updateHeadingSectionNumber(cursor,
                                           doc->findBlockByNumber(heading.m_blockNumber),
                                           sectionStr,
                                           endingDot)) {
                changed = true;
            }
        }
    }
    cursor.endEditBlock();

    return changed;
}

void MarkdownEditor::overrideSectionNumber(OverrideState p_state)
{
    if (m_overriddenSectionNumber == p_state) {
        return;
    }

    m_overriddenSectionNumber = p_state;
    getHighlighter()->updateHighlight();
}

void MarkdownEditor::updateFromConfig(bool p_initialized)
{
    if (m_config.getTextEditorConfig().getZoomDelta() != 0) {
        zoom(m_config.getTextEditorConfig().getZoomDelta());
    }

    if (p_initialized) {
        getHighlighter()->updateHighlight();
    }
}

void MarkdownEditor::setupTableHelper()
{
    m_tableHelper = new MarkdownTableHelper(this, this);
    connect(getHighlighter(), &vte::PegMarkdownHighlighter::tableBlocksUpdated,
            m_tableHelper, &MarkdownTableHelper::updateTableBlocks);
}

QRgb MarkdownEditor::getPreviewBackground() const
{
    auto th = theme();
    const auto &fmt = th->editorStyle(vte::Theme::EditorStyle::Preview);
    return fmt.m_backgroundColor;
}

void MarkdownEditor::setImageHost(ImageHost *p_host)
{
    // It may be different than the global default image host.
    m_imageHost = p_host;
}

static QString generateImageHostFileName(const Buffer *p_buffer, const QString &p_destFileName)
{
    auto destPath = ImageHostUtils::generateRelativePath(p_buffer);
    if (destPath.isEmpty()) {
        destPath = p_destFileName;
    } else {
        destPath += "/" + p_destFileName;
    }
    return destPath;
}

QString MarkdownEditor::saveToImageHost(const QByteArray &p_imageData, const QString &p_destFileName)
{
    Q_ASSERT(m_imageHost);

    const auto destPath = generateImageHostFileName(m_buffer, p_destFileName);

    QString errMsg;

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    auto targetUrl = m_imageHost->create(p_imageData, destPath, errMsg);
    QApplication::restoreOverrideCursor();

    if (targetUrl.isEmpty()) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 QString("Failed to upload image to image host (%1) as (%2).").arg(m_imageHost->getName(), destPath),
                                 QString(),
                                 errMsg,
                                 this);
    }

    return targetUrl;
}

void MarkdownEditor::appendImageHostMenu(QMenu *p_menu)
{
    p_menu->addSeparator();
    auto subMenu = p_menu->addMenu(tr("Upload Images To Image Host"));

    const auto &hosts = ImageHostMgr::getInst().getImageHosts();
    if (hosts.isEmpty()) {
        auto act = subMenu->addAction(tr("None"));
        act->setEnabled(false);
        return;
    }

    for (const auto &host : hosts) {
        auto act = subMenu->addAction(host->getName(),
                                      this,
                                      &MarkdownEditor::uploadImagesToImageHost);
        act->setData(host->getName());
    }
}

void MarkdownEditor::uploadImagesToImageHost()
{
    auto act = static_cast<QAction *>(sender());
    auto host = ImageHostMgr::getInst().find(act->data().toString());
    Q_ASSERT(host);

    // Only LocalRelativeInternal images.
    // Descending order of the link position.
    auto images = vte::MarkdownUtils::fetchImagesFromMarkdownText(m_buffer->getContent(),
                                                                  m_buffer->getResourcePath(),
                                                                  vte::MarkdownLink::TypeFlag::LocalRelativeInternal);
    if (images.isEmpty()) {
        return;
    }

    QProgressDialog proDlg(tr("Uploading local images..."),
                           tr("Abort"),
                           0,
                           images.size(),
                           this);
    proDlg.setWindowModality(Qt::WindowModal);
    proDlg.setWindowTitle(tr("Upload Images To Image Host"));

    QHash<QString, QString> uploadedImages;

    int cnt = 0;
    auto cursor = m_textEdit->textCursor();
    cursor.beginEditBlock();
    for (int i = 0; i < images.size(); ++i) {
        const auto &link = images[i];

        auto it = uploadedImages.find(link.m_path);
        if (it != uploadedImages.end()) {
            cursor.setPosition(link.m_urlInLinkPos);
            cursor.setPosition(link.m_urlInLinkPos + link.m_urlInLink.size(), QTextCursor::KeepAnchor);
            cursor.insertText(it.value());
            continue;
        }

        proDlg.setValue(i + 1);
        if (proDlg.wasCanceled()) {
            break;
        }
        proDlg.setLabelText(tr("Upload image (%1)").arg(link.m_path));

        Q_ASSERT(i == 0 || link.m_urlInLinkPos < images[i - 1].m_urlInLinkPos);

        QByteArray ba;
        try {
            ba = FileUtils::readFile(link.m_path);
        } catch (Exception &e) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     QString("Failed to read local image file (%1) (%2).").arg(link.m_path, e.what()),
                                     this);
            continue;
        }

        if (ba.isEmpty()) {
            qWarning() << "Skipped uploading empty image" << link.m_path;
            continue;
        }

        const auto destPath = generateImageHostFileName(m_buffer, PathUtils::fileName(link.m_path));
        QString errMsg;
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        const auto targetUrl = host->create(ba, destPath, errMsg);
        QApplication::restoreOverrideCursor();

        if (targetUrl.isEmpty()) {
            MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                     QString("Failed to upload image to image host (%1) as (%2).").arg(host->getName(), destPath),
                                     QString(),
                                     errMsg,
                                     this);
            continue;
        }

        // Update the link URL.
        cursor.setPosition(link.m_urlInLinkPos);
        cursor.setPosition(link.m_urlInLinkPos + link.m_urlInLink.size(), QTextCursor::KeepAnchor);
        cursor.insertText(targetUrl);
        uploadedImages.insert(link.m_path, targetUrl);
        ++cnt;
    }
    cursor.endEditBlock();

    proDlg.setValue(images.size());

    if (cnt > 0) {
        m_textEdit->setTextCursor(cursor);
    }
}

void MarkdownEditor::prependContextSensitiveMenu(QMenu *p_menu, const QPoint &p_pos)
{
    auto cursor = m_textEdit->cursorForPosition(p_pos);
    const int pos = cursor.position();
    const auto block = cursor.block();

    Q_ASSERT(!p_menu->isEmpty());
    auto firstAct = p_menu->actions().at(0);

    bool ret = prependImageMenu(p_menu, firstAct, pos, block);
    if (ret) {
        return;
    }

    ret = prependLinkMenu(p_menu, firstAct, pos, block);
    if (ret) {
        return;
    }

    if (prependInPlacePreviewMenu(p_menu, firstAct, pos, block)) {
        p_menu->insertSeparator(firstAct);
    }
}

bool MarkdownEditor::prependImageMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos, const QTextBlock &p_block)
{
    const auto text = p_block.text();

    if (!vte::MarkdownUtils::hasImageLink(text)) {
        return false;
    }

    QString imgPath;

    const auto &regions = getHighlighter()->getImageRegions();
    for (const auto &reg : regions) {
        if (!reg.contains(p_cursorPos) && (!reg.contains(p_cursorPos - 1) || p_cursorPos != p_block.position() + text.size())) {
            continue;
        }

        if (reg.m_endPos > p_block.position() + text.size()) {
            return true;
        }

        const auto linkText = text.mid(reg.m_startPos - p_block.position(), reg.m_endPos - reg.m_startPos);
        int linkWidth = 0;
        int linkHeight = 0;
        const auto shortUrl = vte::MarkdownUtils::fetchImageLinkUrl(linkText, linkWidth, linkHeight);
        if (shortUrl.isEmpty()) {
            return true;
        }

        imgPath = vte::MarkdownUtils::linkUrlToPath(getBasePath(), shortUrl);
        break;
    }

    {
        auto act = new QAction(tr("View Image"), p_menu);
        connect(act, &QAction::triggered,
                p_menu, [imgPath]() {
                    WidgetUtils::openUrlByDesktop(PathUtils::pathToUrl(imgPath));
                });
        p_menu->insertAction(p_before, act);
    }

    {
        auto act = new QAction(tr("Copy Image URL"), p_menu);
        connect(act, &QAction::triggered,
                p_menu, [imgPath]() {
                    ClipboardUtils::setLinkToClipboard(imgPath);
                });
        p_menu->insertAction(p_before, act);
    }

    if (QFileInfo::exists(imgPath)) {
        // Local image.
        auto act = new QAction(tr("Copy Image"), p_menu);
        connect(act, &QAction::triggered,
                p_menu, [imgPath]() {
                    auto clipboard = QApplication::clipboard();
                    clipboard->clear();

                    auto img = FileUtils::imageFromFile(imgPath);
                    if (!img.isNull()) {
                        ClipboardUtils::setImageToClipboard(clipboard, img);
                    }
                });
        p_menu->insertAction(p_before, act);
    } else {
        // Online image.
        prependInPlacePreviewMenu(p_menu, p_before, p_cursorPos, p_block);
    }

    p_menu->insertSeparator(p_before);

    return true;
}

bool MarkdownEditor::prependInPlacePreviewMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos, const QTextBlock &p_block)
{
    auto data = vte::TextBlockData::get(p_block);
    if (!data) {
        return false;
    }

    auto previewData = data->getBlockPreviewData();
    if (!previewData) {
        return false;
    }

    QPixmap image;
    QRgb background = 0;
    const int pib = p_cursorPos - p_block.position();
    for (const auto &info : previewData->getPreviewData()) {
        const auto *imageData = info->getImageData();
        if (!imageData) {
            continue;
        }

        if (imageData->contains(pib) || (imageData->contains(pib - 1) && pib == p_block.length() - 1)) {
            const auto *img = findImageFromDocumentResourceMgr(imageData->m_imageName);
            if (img) {
                image = *img;
                background = imageData->m_backgroundColor;
            }
            break;
        }
    }

    if (image.isNull()) {
        return false;
    }

    auto act = new QAction(tr("Copy In-Place Preview"), p_menu);
    connect(act, &QAction::triggered,
            p_menu, [this, image, background]() {
                QColor color(background);
                if (background == 0) {
                    color = m_textEdit->palette().color(QPalette::Base);
                }
                QImage img(image.size(), QImage::Format_ARGB32);
                img.fill(color);
                QPainter painter(&img);
                painter.drawPixmap(img.rect(), image);

                auto clipboard = QApplication::clipboard();
                clipboard->clear();
                ClipboardUtils::setImageToClipboard(clipboard, img);
            });

    p_menu->insertAction(p_before, act);

    return true;
}

bool MarkdownEditor::prependLinkMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos, const QTextBlock &p_block)
{
    const auto text = p_block.text();

    QRegularExpression regExp(vte::MarkdownUtils::c_linkRegExp);
    QString linkText;
    const int pib = p_cursorPos - p_block.position();
    auto matchIter = regExp.globalMatch(text);
    while (matchIter.hasNext()) {
        auto match = matchIter.next();
        if (pib >= match.capturedStart() && pib < match.capturedEnd()) {
            linkText = match.captured(2);
            break;
        }
    }

    if (linkText.isEmpty()) {
        return false;
    }

    const auto linkUrl = vte::MarkdownUtils::linkUrlToPath(getBasePath(), linkText);

    {
        auto act = new QAction(tr("Open Link"), p_menu);
        connect(act, &QAction::triggered,
                p_menu, [linkUrl]() {
                    emit VNoteX::getInst().openFileRequested(linkUrl, QSharedPointer<FileOpenParameters>::create());
                });
        p_menu->insertAction(p_before, act);
    }

    {
        auto act = new QAction(tr("Copy Link"), p_menu);
        connect(act, &QAction::triggered,
                p_menu, [linkUrl]() {
                    ClipboardUtils::setLinkToClipboard(linkUrl);
                });
        p_menu->insertAction(p_before, act);
    }

    p_menu->insertSeparator(p_before);

    return true;
}
