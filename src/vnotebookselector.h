#ifndef VNOTEBOOKSELECTOR_H
#define VNOTEBOOKSELECTOR_H

#include <QComboBox>
#include <QVector>
#include <QString>

class VNotebook;
class VNote;
class VEditArea;

class VNotebookSelector : public QComboBox
{
    Q_OBJECT
public:
    explicit VNotebookSelector(VNote *vnote, QWidget *p_parent = 0);
    void update();
    inline void setEditArea(VEditArea *p_editArea);

signals:
    void curNotebookChanged(VNotebook *p_notebook);
    // Info of current notebook was changed.
    void notebookUpdated(const VNotebook *p_notebook);

public slots:
    void newNotebook();
    void deleteNotebook();
    void editNotebookInfo();

private slots:
    void handleCurIndexChanged(int p_index);

private:
    void updateComboBox();
    VNotebook *findNotebook(const QString &p_name);
    int indexOfNotebook(const VNotebook *p_notebook);
    void createNotebook(const QString &p_name, const QString &p_path);
    void deleteNotebook(VNotebook *p_notebook);

    VNote *m_vnote;
    QVector<VNotebook *> &m_notebooks;
    VEditArea *m_editArea;
};

inline void VNotebookSelector::setEditArea(VEditArea *p_editArea)
{
    m_editArea = p_editArea;
}

#endif // VNOTEBOOKSELECTOR_H
