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

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/markdownutils.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/networkutils.h>

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
#include <utils/textutils.h>
#include <utils/webutils.h>
#include <core/exception.h>
#include <core/markdowneditorconfig.h>
#include <core/texteditorconfig.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>

#include "previewhelper.h"
#include "../outlineprovider.h"
#include "markdowntablehelper.h"

using namespace vnotex;

// We set the property of the clipboard to mark that we are requesting a rich paste.
static const char *c_clipboardPropertyMark = "RichPaste";

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
                               QWidget *p_parent)
    : vte::VMarkdownEditor(p_editorConfig, p_parent),
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
    try {
        destFilePath = m_buffer->insertImage(p_srcImagePath, destFileName);
    } catch (Exception e) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 QString("Failed to insert image from local file %1 (%2)").arg(p_srcImagePath, e.what()),
                                 this);
        return false;
    }

    insertImageLink(p_title,
                    p_altText,
                    destFilePath,
                    p_scaledWidth,
                    p_scaledHeight,
                    p_insertText,
                    p_urlInLink);
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
    auto destFileName = generateImageFileNameToInsertAs(p_title, QStringLiteral("png"));

    QString destFilePath;
    try {
        destFilePath = m_buffer->insertImage(p_image, destFileName);
    } catch (Exception e) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 QString("Failed to insert image from data (%1)").arg(e.what()),
                                 this);
        return false;
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
    if (p_source->hasImage() || p_source->hasUrls()) {
        if (p_source->hasImage() || (!p_source->hasText() && !p_source->hasHtml())) {
            // Change to Rich Paste.
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setProperty(c_clipboardPropertyMark, true);
        }
        *p_handled = true;
        *p_allowed = true;
    }
}

void MarkdownEditor::handleInsertFromMimeData(const QMimeData *p_source, bool *p_handled)
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard->property(c_clipboardPropertyMark).toBool()) {
        // Default paste.
        return;
    } else {
        clipboard->setProperty(c_clipboardPropertyMark, false);
    }

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
}

bool MarkdownEditor::processHtmlFromMimeData(const QMimeData *p_source)
{
    if (!p_source->hasHtml()) {
        return false;
    }

    // Process <img>.
    QRegExp reg("<img ([^>]*)src=\"([^\"]+)\"([^>]*)>");
    const QString html(p_source->html());
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
    QUrl url;
    if (p_source->hasUrls()) {
        const auto urls = p_source->urls();
        if (urls.size() == 1) {
            url = urls[0];
        }
    } else if (p_source->hasText()) {
        // Try to get URL from text.
        const QString text = p_source->text();
        if (QFileInfo::exists(text)) {
            url = QUrl::fromLocalFile(text);
        } else {
            url = QUrl::fromUserInput(text);
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

void MarkdownEditor::insertImageFromUrl(const QString &p_url)
{
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

QString MarkdownEditor::getRelativeLink(const QString &p_path)
{
    auto relativePath = PathUtils::relativePath(PathUtils::parentDirPath(m_buffer->getContentPath()), p_path);
    auto link = PathUtils::encodeSpacesInPath(QDir::fromNativeSeparators(relativePath));
    if (m_config.getPrependDotInRelativeLink()) {
        PathUtils::prependDotIfRelative(link);
    }

    return link;
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
    *p_handled = true;
    p_menu->reset(m_textEdit->createStandardContextMenu(p_event->pos()));
    auto menu = p_menu->data();

    QAction *copyAct = nullptr;
    QAction *pasteAct = nullptr;
    QAction *firstAct = nullptr;
    {
        const auto actions = menu->actions();
        firstAct = actions.isEmpty() ? nullptr : actions.first();
        copyAct = WidgetUtils::findActionByObjectName(actions, "edit-copy");
        pasteAct = WidgetUtils::findActionByObjectName(actions, "edit-paste");
    }

    if (!m_textEdit->hasSelection()) {
        auto readAct = new QAction(tr("&Read"), menu);
        WidgetUtils::addActionShortcutText(readAct,
                                           ConfigMgr::getInst().getEditorConfig().getShortcut(EditorConfig::Shortcut::EditRead));
        connect(readAct, &QAction::triggered,
                this, &MarkdownEditor::readRequested);
        menu->insertAction(firstAct, readAct);
        if (firstAct) {
            menu->insertSeparator(firstAct);
        }
    }

    if (pasteAct && pasteAct->isEnabled()) {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();

        // Rich Paste.
        auto richPasteAct = new QAction(tr("Rich Paste"), menu);
        WidgetUtils::addActionShortcutText(richPasteAct,
                                           ConfigMgr::getInst().getEditorConfig().getShortcut(EditorConfig::Shortcut::RichPaste));
        connect(richPasteAct, &QAction::triggered,
                this, &MarkdownEditor::richPaste);
        WidgetUtils::insertActionAfter(menu, pasteAct, richPasteAct);

        if (mimeData->hasHtml()) {
            // Parse To Markdown And Paste.
            auto parsePasteAct = new QAction(tr("Parse To Markdown And Paste"), menu);
            connect(parsePasteAct, &QAction::triggered,
                    this, &MarkdownEditor::parseToMarkdownAndPaste);
            WidgetUtils::insertActionAfter(menu, richPasteAct, parsePasteAct);
        }
    }
}

void MarkdownEditor::richPaste()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setProperty(c_clipboardPropertyMark, true);
    m_textEdit->paste();
    clipboard->setProperty(c_clipboardPropertyMark, false);
}

void MarkdownEditor::setupShortcuts()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    {
        auto shortcut = WidgetUtils::createShortcut(editorConfig.getShortcut(EditorConfig::Shortcut::RichPaste),
                                                    this);
        if (shortcut) {
            connect(shortcut, &QShortcut::activated,
                    this, &MarkdownEditor::richPaste);
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

        const QString imageTitle = purifyImageTitle(regExp.cap(1).trimmed());
        const QString imageUrl = regExp.cap(2).trimmed();

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
            QByteArray data = vte::Downloader::download(QUrl(imageUrl));
            if (!data.isEmpty()) {
                tmpFile.reset(FileUtils::createTemporaryFile(info.suffix()));
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

static void increaseSectionNumber(QVector<int> &p_sectionNumber, int p_level, int p_baseLevel)
{
    Q_ASSERT(p_level >= 1 && p_level < p_sectionNumber.size());
    if (p_level < p_baseLevel) {
        p_sectionNumber.fill(0);
        return;
    }

    ++p_sectionNumber[p_level];
    for (int i = p_level + 1; i < p_sectionNumber.size(); ++i) {
        p_sectionNumber[i] = 0;
    }
}

static QString joinSectionNumberStr(const QVector<int> &p_sectionNumber, bool p_endingDot)
{
    QString res;
    for (auto sec : p_sectionNumber) {
        if (sec != 0) {
            if (res.isEmpty()) {
                res = QString::number(sec);
            } else {
                res += '.' + QString::number(sec);
            }
        } else if (res.isEmpty()) {
            continue;
        } else {
            break;
        }
    }

    if (p_endingDot && !res.isEmpty()) {
        return res + '.';
    } else {
        return res;
    }
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
    QVector<int> sectionNumber(7, 0);
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
        increaseSectionNumber(sectionNumber, heading.m_level, baseLevel);
        auto sectionStr = m_sectionNumberEnabled ? joinSectionNumberStr(sectionNumber, endingDot) : QString();
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
