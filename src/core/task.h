#ifndef TASKCONFIG_H
#define TASKCONFIG_H

#include <QObject>

class TaskConfig : public QObject
{
    Q_OBJECT
public:
    explicit TaskConfig(QObject *parent = nullptr);
    
signals:
    
};

#endif // TASKCONFIG_H
