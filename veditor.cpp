#include <QtWidgets>
#include <QTextBrowser>
#include "veditor.h"
#include "vedit.h"

VEditor::VEditor(const QString &path, const QString &name, bool modifiable,
                 QWidget *parent)
    : QStackedWidget(parent)
{
    DocType docType = isMarkdown(name) ? DocType::Markdown : DocType::Html;
    QString fileText = readFileFromDisk(QDir(path).filePath(name));
    noteFile = new VNoteFile(path, name, fileText, docType, modifiable);

    isEditMode = false;

    setupUI();

    showFileReadMode();
}

VEditor::~VEditor()
{
    delete noteFile;
}

void VEditor::setupUI()
{
    textEditor = new VEdit(noteFile);
    textBrowser = new QTextBrowser();
    addWidget(textBrowser);
    addWidget(textEditor);
}

bool VEditor::isMarkdown(const QString &name)
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

QString VEditor::readFileFromDisk(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "error: fail to read file" << filePath;
        return QString();
    }
    QString fileText(file.readAll());
    file.close();
    qDebug() << "read file content:" << filePath;
    return fileText;
}

bool VEditor::writeFileToDisk(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "error: fail to open file" << filePath << "to write to";
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    file.close();
    qDebug() << "write file content:" << filePath;
    return true;
}

void VEditor::showFileReadMode()
{
    isEditMode = false;
    switch (noteFile->docType) {
    case DocType::Html:
        textBrowser->setHtml(noteFile->content);
        break;
    case DocType::Markdown:

        break;
    default:
        qWarning() << "error: unknown doc type" << int(noteFile->docType);
    }
    setCurrentWidget(textBrowser);
}

void VEditor::showFileEditMode()
{
    isEditMode = true;
    textEditor->beginEdit();
    setCurrentWidget(textEditor);
}

bool VEditor::requestClose()
{
    readFile();
    return !isEditMode;
}

void VEditor::editFile()
{
    if (isEditMode || !noteFile->modifiable) {
        return;
    }
    showFileEditMode();
}

void VEditor::readFile()
{
    if (!isEditMode) {
        return;
    }
    bool canExit = textEditor->tryEndEdit();
    if (!canExit) {
        // Need to save the changes
        QMessageBox msgBox;
        msgBox.setText("The note has been modified.");
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

bool VEditor::saveFile()
{
    if (!isEditMode || !noteFile->modifiable) {
        return true;
    }
    textEditor->beginSave();
    bool ret = writeFileToDisk(QDir(noteFile->path).filePath(noteFile->name),
                               noteFile->content);
    if (!ret) {
        QMessageBox msgBox(QMessageBox::Warning, tr("Fail to save to file"),
                           QString("Fail to write to disk when saving a note. Please try it again."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        return false;
    }
    textEditor->endSave();
    return true;
}
