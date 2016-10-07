#ifndef VTABWIDGET_H
#define VTABWIDGET_H

#include <QTabWidget>
#include <QJsonObject>
#include <QString>

class VTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    explicit VTabWidget(const QString &welcomePageUrl, QWidget *parent = 0);

signals:

public slots:
    void openFile(QJsonObject fileJson);
    void editFile();
    void saveFile();
    void readFile();

private slots:
    void handleTabCloseRequest(int index);

private:
    void openWelcomePage();
    int insertTabWithData(int index, QWidget *page, const QString &label, const QJsonObject &tabData);
    int appendTabWithData(QWidget *page, const QString &label, const QJsonObject &tabData);
    int findTabByFile(const QString &path, const QString &name);
    int openFileInTab(const QString &path, const QString &name, bool modifiable);
    QString welcomePageUrl;
};

#endif // VTABWIDGET_H
