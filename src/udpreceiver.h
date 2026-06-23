#ifndef UDPRECEIVER_H
#define UDPRECEIVER_H

#include <QHostAddress>
#include <QDate>
#include <QObject>
#include <QString>
#include <QTime>
#include <QUdpSocket>

struct UdpLoggedContact
{
    QDate date;
    QTime timeOn;
    QTime timeOff;
    QString call;
    QString band;
    QString frequency;
    QString mode;
    QString submode;
    QString comment;
};

class UdpReceiver : public QObject
{
    Q_OBJECT

public:
    explicit UdpReceiver(QObject *parent = nullptr);

    bool start(quint16 port = 2237);

signals:
    void loggedContactReceived(const UdpLoggedContact &contact);

private slots:
    void onReadyRead();

private:
    QUdpSocket m_socket;
};

#endif // UDPRECEIVER_H
