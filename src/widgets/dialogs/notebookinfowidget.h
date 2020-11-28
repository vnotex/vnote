#ifndef NOTEBOOKINFOWIDGET_H
#define NOTEBOOKINFOWIDGET_H

#include <QWidget>

class QComboBox;
class QPushButton;
class QLineEdit;
class QGroupBox;

namespace vnotex
{
    class Notebook;

    class NotebookInfoWidget : public QWidget
    {
        Q_OBJECT
    public:
        enum Mode { Create, CreateFromFolder, Edit, Import, CreateFromLegacy };

        explicit NotebookInfoWidget(NotebookInfoWidget::Mode p_mode, QWidget *p_parent = nullptr);

        QLineEdit *getNameLineEdit() const;

        QLineEdit *getRootFolderPathLineEdit() const;
        void setRootFolderPath(const QString &p_path);

        QString getName() const;

        QString getDescription() const;

        QString getRootFolderPath() const;

        QIcon getIcon() const;

        QString getType() const;

        QString getConfigMgr() const;

        QString getVersionController() const;

        QString getBackend() const;

        void clear(bool p_skipRootFolder = false, bool p_skipBackend = false);

        void setMode(Mode p_mode);

        const Notebook *getNotebook() const;

    public slots:
        void setNotebook(const Notebook *p_notebook);

    signals:
        // Give caller a chance to change the name according to the root folder.
        void rootFolderEdited();

        // Emit when name, description, or root folder path is edited.
        void basicInfoEdited();

        void notebookBackendEdited();

    private:
        void setupUI();

        void setStateAccordingToMode();

        QGroupBox *setupBasicInfoGroupBox(QWidget *p_parent = nullptr);

        void setupNotebookTypeComboBox(QWidget *p_parent = nullptr);

        QLayout *setupNotebookRootFolderPath(QWidget *p_parent = nullptr);

        QGroupBox *setupAdvancedInfoGroupBox(QWidget *p_parent = nullptr);

        void setupConfigMgrComboBox(QWidget *p_parent = nullptr);

        void setupVersionControllerComboBox(QWidget *p_parent = nullptr);

        void setupBackendComboBox(QWidget *p_parent = nullptr);

        QLineEdit *getDescriptionLineEdit() const;

        QComboBox *getTypeComboBox() const;

        QComboBox *getConfigMgrComboBox() const;

        QComboBox *getVersionControllerComboBox() const;

        QComboBox *getBackendComboBox() const;

        Mode m_mode = Mode::Create;

        const Notebook *m_notebook = nullptr;

        QLineEdit *m_nameLineEdit = nullptr;

        QLineEdit *m_descriptionLineEdit = nullptr;

        QComboBox *m_typeComboBox = nullptr;

        QComboBox *m_configMgrComboBox = nullptr;

        QComboBox *m_versionControllerComboBox = nullptr;

        QComboBox *m_backendComboBox = nullptr;

        QLineEdit *m_rootFolderPathLineEdit = nullptr;
        QPushButton *m_rootFolderPathBrowseButton = nullptr;
    };
} // ns vnotex

#endif // NOTEBOOKINFOWIDGET_H
