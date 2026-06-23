#ifndef UDPRECEIVER_H
#define UDPRECEIVER_H

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

class UdpReceiver : public QObject
{
    Q_OBJECT

public:
    explicit UdpReceiver(QObject *parent = nullptr);

    bool start(quint16 port = 2237);

private slots:
    void onReadyRead();

private:
    QUdpSocket m_socket;
};

#endif // UDPRECEIVER_H
