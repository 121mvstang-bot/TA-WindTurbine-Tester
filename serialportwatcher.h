#ifndef SERIALPORTWATCHER_H
#define SERIALPORTWATCHER_H

#include <QObject>
#include <QSerialPortInfo>
#include <QTimer>

class SerialPortWatcher : public QObject
{
    Q_OBJECT
public:
    explicit SerialPortWatcher(QObject *parent = nullptr,
                               int intervalMs = 1000);

signals:
    void portsChanged(const QList<QSerialPortInfo> &ports);
    void portAdded(const QSerialPortInfo &info);
    void portRemoved(const QSerialPortInfo &info);

private slots:
    void checkPorts();

private:
    QTimer m_timer;
    QList<QSerialPortInfo> m_lastPorts;
};

#endif // SERIALPORTWATCHER_H
