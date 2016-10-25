#ifndef VTABWIDGET_H
#define VTABWIDGET_H

#include <QTabWidget>
#include <QJsonObject>
#include <QString>
#include <QFileInfo>
#include <QDir>

class VNote;

class VTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    explicit VTabWidget(VNote *vnote, QWidget *parent = 0);

signals:

public slots:
    void openFile(QJsonObject fileJson);
    // Close the file forcely
    void closeFile(QJsonObject fileJson);
    void editFile();
    void saveFile();
    void readFile();

private slots:
    void handleTabCloseRequest(int index);

private:
    void openWelcomePage();
    int insertTabWithData(int index, QWidget *page, const QJsonObject &tabData);
    int appendTabWithData(QWidget *page, const QJsonObject &tabData);
    int findTabByFile(const QString &notebook, const QString &relativePath) const;
    int openFileInTab(const QString &notebook, const QString &relativePath, bool modifiable);
    inline QString getFileName(const QString &relativePath) const;

    VNote *vnote;
};

inline QString VTabWidget::getFileName(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).fileName();
}

#endif // VTABWIDGET_H
