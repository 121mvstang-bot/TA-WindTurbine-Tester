#include "serialportwatcher.h"

SerialPortWatcher::SerialPortWatcher(QObject *parent, int intervalMs)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout,
            this, &SerialPortWatcher::checkPorts);

    m_timer.start(intervalMs);

    // initialize list
    m_lastPorts = QSerialPortInfo::availablePorts();
}

void SerialPortWatcher::checkPorts()
{
    const QList<QSerialPortInfo> current = QSerialPortInfo::availablePorts();

    // simple comparison by systemLocation (unique per port)
    auto samePort = [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
        return a.systemLocation() == b.systemLocation();
    };

    // detect added
    for (const QSerialPortInfo &p : current) {
        bool found = false;
        for (const QSerialPortInfo &oldP : m_lastPorts) {
            if (samePort(p, oldP)) {
                found = true;
                break;
            }
        }
        if (!found) {
            emit portAdded(p);
        }
    }

    // detect removed
    for (const QSerialPortInfo &oldP : m_lastPorts) {
        bool found = false;
        for (const QSerialPortInfo &p : current) {
            if (samePort(p, oldP)) {
                found = true;
                break;
            }
        }
        if (!found) {
            emit portRemoved(oldP);
        }
    }

    if (current != m_lastPorts) {
        m_lastPorts = current;
        emit portsChanged(m_lastPorts);
    }
}
