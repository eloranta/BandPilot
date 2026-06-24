#include "udpreceiver.h"

#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>

#include <optional>

namespace {

QString readUtf8(QDataStream &stream)
{
    QByteArray bytes;
    stream >> bytes;
    return QString::fromUtf8(bytes);
}

void setStreamVersionFromSchema(QDataStream &stream, quint32 schema)
{
    if (schema >= 3) {
        stream.setVersion(QDataStream::Qt_5_4);
    } else {
        stream.setVersion(QDataStream::Qt_5_2);
    }
}

QString bandFromHz(quint64 hz)
{
    if (hz >= 28000000ULL && hz <= 29700000ULL) return QStringLiteral("10 m");
    if (hz >= 24890000ULL && hz <= 24990000ULL) return QStringLiteral("12 m");
    if (hz >= 21000000ULL && hz <= 21450000ULL) return QStringLiteral("15 m");
    if (hz >= 18068000ULL && hz <= 18168000ULL) return QStringLiteral("17 m");
    if (hz >= 14000000ULL && hz <= 14350000ULL) return QStringLiteral("20 m");
    if (hz >= 10100000ULL && hz <= 10150000ULL) return QStringLiteral("30 m");
    if (hz >= 7000000ULL && hz <= 7300000ULL) return QStringLiteral("40 m");
    if (hz >= 3500000ULL && hz <= 4000000ULL) return QStringLiteral("80 m");
    if (hz >= 144000000ULL && hz <= 148000000ULL) return QStringLiteral("2 m");
    return {};
}

QString frequencyFromHz(quint64 hz)
{
    QString frequency = QString::number(double(hz) / 1000000.0, 'f', 6);
    while (frequency.endsWith(QLatin1Char('0'))) {
        frequency.chop(1);
    }
    if (frequency.endsWith(QLatin1Char('.'))) {
        frequency.chop(1);
    }
    return frequency;
}

QString modeCategory(const QString &mode)
{
    const QString upper = mode.trimmed().toUpper();
    if (upper == QStringLiteral("CW")) {
        return QStringLiteral("CW");
    }
    if (upper == QStringLiteral("SSB")
        || upper == QStringLiteral("USB")
        || upper == QStringLiteral("LSB")
        || upper == QStringLiteral("AM")
        || upper == QStringLiteral("FM")) {
        return QStringLiteral("Phone");
    }
    if (upper.contains(QStringLiteral("SAT"))) {
        return QStringLiteral("Sat");
    }
    return QStringLiteral("Data");
}

QString joinCommentParts(const QStringList &parts)
{
    QStringList filtered;
    for (const QString &part : parts) {
        if (!part.trimmed().isEmpty()) {
            filtered.append(part.trimmed());
        }
    }
    return filtered.join(QStringLiteral("; "));
}

QString validGrid(const QString &grid)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2})?)?$"),
        QRegularExpression::CaseInsensitiveOption);

    const QString trimmed = grid.trimmed();
    return pattern.match(trimmed).hasMatch() ? trimmed : QString();
}

bool decodeType2(QDataStream &stream, const QString &id)
{
    bool isNew = false;
    QTime time;
    qint32 snr = 0;
    double deltaTime = 0.0;
    quint32 deltaFreq = 0;

    stream >> isNew >> time >> snr >> deltaTime >> deltaFreq;

    const QString mode = readUtf8(stream);
    const QString message = readUtf8(stream);

    bool lowConfidence = false;
    bool offAir = false;
    if (stream.device() && stream.device()->bytesAvailable() >= 1) stream >> lowConfidence;
    if (stream.device() && stream.device()->bytesAvailable() >= 1) stream >> offAir;

    if (stream.status() != QDataStream::Ok) {
        qWarning() << "UDP WSJT-X type 2 decode failed:" << stream.status();
        return false;
    }

    qDebug().noquote()
        << "UDP WSJT-X DECODE"
        << "id=" << id
        << "new=" << isNew
        << "time=" << time.toString(QStringLiteral("HH:mm:ss"))
        << "snr=" << snr
        << "dt=" << deltaTime
        << "df=" << deltaFreq
        << "mode=" << mode
        << "message=" << message
        << "lowConfidence=" << lowConfidence
        << "offAir=" << offAir;

    return true;
}

std::optional<UdpLoggedContact> decodeType5(QDataStream &stream, const QString &id)
{
    QDateTime dateTimeOff;
    stream >> dateTimeOff;

    const QString dxCall = readUtf8(stream);
    const QString dxGrid = readUtf8(stream);

    quint64 dialFreqHz = 0;
    stream >> dialFreqHz;

    const QString submode = readUtf8(stream);
    const QString reportSent = readUtf8(stream);
    const QString reportReceived = readUtf8(stream);
    const QString txPower = readUtf8(stream);
    const QString comment = readUtf8(stream);
    const QString name = readUtf8(stream);

    if (stream.status() != QDataStream::Ok) {
        qWarning() << "UDP WSJT-X type 5 decode failed:" << stream.status();
        return std::nullopt;
    }

    const QString band = bandFromHz(dialFreqHz);
    const QString mode = modeCategory(submode);
    const QString grid = validGrid(dxGrid);

    qDebug().noquote()
        << "UDP WSJT-X QSO_LOGGED"
        << "id=" << id
        << "date=" << dateTimeOff.date().toString(Qt::ISODate)
        << "time_off=" << dateTimeOff.time().toString(QStringLiteral("HH:mm:ss"))
        << "call=" << dxCall
        << "grid=" << dxGrid
        << "band=" << band
        << "frequency_hz=" << dialFreqHz
        << "mode=" << mode
        << "submode=" << submode
        << "rst_sent=" << reportSent
        << "rst_received=" << reportReceived
        << "power=" << txPower
        << "name=" << name
        << "comment=" << comment;

    UdpLoggedContact contact;
    contact.date = dateTimeOff.date();
    contact.time = dateTimeOff.time();
    contact.call = dxCall;
    contact.band = band;
    contact.frequency = frequencyFromHz(dialFreqHz);
    contact.mode = mode;
    contact.submode = submode;
    contact.comment = joinCommentParts({
        comment,
        grid.isEmpty() ? QString() : QStringLiteral("Grid: %1").arg(grid),
        reportSent.isEmpty() ? QString() : QStringLiteral("RST sent: %1").arg(reportSent),
        reportReceived.isEmpty() ? QString() : QStringLiteral("RST received: %1").arg(reportReceived),
        txPower.isEmpty() ? QString() : QStringLiteral("Power: %1").arg(txPower),
        name.isEmpty() ? QString() : QStringLiteral("Name: %1").arg(name)
    });

    return contact;
}

std::optional<UdpLoggedContact> decodeType6(QDataStream &stream, const QString &id)
{
    QDateTime dateTimeOff;
    stream >> dateTimeOff;

    const QString dxCall = readUtf8(stream);
    const QString dxGrid = readUtf8(stream);

    quint64 txFreqHz = 0;
    stream >> txFreqHz;

    const QString submode = readUtf8(stream);
    const QString reportSent = readUtf8(stream);
    const QString reportReceived = readUtf8(stream);
    const QString txPower = readUtf8(stream);
    const QString comment = readUtf8(stream);
    const QString name = readUtf8(stream);

    QDateTime dateTimeOn;
    stream >> dateTimeOn;

    const QString operatorCall = readUtf8(stream);
    const QString myCall = readUtf8(stream);
    const QString myGrid = readUtf8(stream);
    const QString exchangeSent = readUtf8(stream);
    const QString exchangeReceived = readUtf8(stream);
    const QString adifPropagationMode = readUtf8(stream);

    if (stream.status() != QDataStream::Ok) {
        qWarning() << "UDP WSJT-X type 6 decode failed:" << stream.status();
        return std::nullopt;
    }

    const QString band = bandFromHz(txFreqHz);
    const QString mode = modeCategory(submode);
    const QString grid = validGrid(dxGrid);

    qDebug().noquote()
        << "UDP WSJT-X LOGGED_ADIF"
        << "id=" << id
        << "date=" << dateTimeOff.date().toString(Qt::ISODate)
        << "time_on=" << dateTimeOn.time().toString(QStringLiteral("HH:mm:ss"))
        << "time_off=" << dateTimeOff.time().toString(QStringLiteral("HH:mm:ss"))
        << "call=" << dxCall
        << "grid=" << dxGrid
        << "band=" << band
        << "frequency_hz=" << txFreqHz
        << "mode=" << mode
        << "submode=" << submode
        << "rst_sent=" << reportSent
        << "rst_received=" << reportReceived
        << "power=" << txPower
        << "name=" << name
        << "operator=" << operatorCall
        << "station_call=" << myCall
        << "station_grid=" << myGrid
        << "exchange_sent=" << exchangeSent
        << "exchange_received=" << exchangeReceived
        << "propagation=" << adifPropagationMode
        << "comment=" << comment;

    UdpLoggedContact contact;
    contact.date = dateTimeOff.date();
    contact.time = dateTimeOff.time();
    contact.call = dxCall;
    contact.band = band;
    contact.frequency = frequencyFromHz(txFreqHz);
    contact.mode = mode;
    contact.submode = submode;
    contact.comment = joinCommentParts({
        comment,
        grid.isEmpty() ? QString() : QStringLiteral("Grid: %1").arg(grid),
        reportSent.isEmpty() ? QString() : QStringLiteral("RST sent: %1").arg(reportSent),
        reportReceived.isEmpty() ? QString() : QStringLiteral("RST received: %1").arg(reportReceived),
        txPower.isEmpty() ? QString() : QStringLiteral("Power: %1").arg(txPower),
        name.isEmpty() ? QString() : QStringLiteral("Name: %1").arg(name),
        operatorCall.isEmpty() ? QString() : QStringLiteral("Operator: %1").arg(operatorCall),
        myCall.isEmpty() ? QString() : QStringLiteral("Station: %1").arg(myCall),
        myGrid.isEmpty() ? QString() : QStringLiteral("Station grid: %1").arg(myGrid),
        exchangeSent.isEmpty() ? QString() : QStringLiteral("Exchange sent: %1").arg(exchangeSent),
        exchangeReceived.isEmpty() ? QString() : QStringLiteral("Exchange received: %1").arg(exchangeReceived),
        adifPropagationMode.isEmpty() ? QString() : QStringLiteral("Propagation: %1").arg(adifPropagationMode)
    });

    return contact;
}

std::optional<UdpLoggedContact> decodeWsjtxDatagram(const QByteArray &datagram)
{
    QDataStream stream(datagram);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    quint32 schema = 0;
    quint32 type = 0;
    stream >> magic >> schema >> type;

    if (magic != 0xadbccbda) {
        qDebug() << "UDP datagram is not WSJT-X. First bytes:" << datagram.left(16).toHex(' ');
        return std::nullopt;
    }

    setStreamVersionFromSchema(stream, schema);

    const QString id = readUtf8(stream);
    qDebug().noquote()
        << "UDP WSJT-X"
        << "schema=" << schema
        << "type=" << type
        << "id=" << id
        << "bytes=" << datagram.size();

    switch (type) {
    case 2:
        decodeType2(stream, id);
        break;
    case 5:
        return decodeType5(stream, id);
    case 6:
        return decodeType6(stream, id);
    default:
        qDebug() << "UDP WSJT-X unhandled message type:" << type
                 << "remaining bytes:" << (stream.device() ? stream.device()->bytesAvailable() : -1);
        break;
    }

    return std::nullopt;
}

} // namespace

UdpReceiver::UdpReceiver(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &UdpReceiver::onReadyRead);
}

bool UdpReceiver::start(quint16 port)
{
    const bool ok = m_socket.bind(QHostAddress::LocalHost,
                                  port,
                                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    if (!ok) {
        qWarning() << "UDP bind failed on 127.0.0.1:" << port << "-" << m_socket.errorString();
        return false;
    }

    qDebug() << "UDP listening on 127.0.0.1:" << port;
    return true;
}

void UdpReceiver::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;

        const qint64 bytesRead = m_socket.readDatagram(datagram.data(),
                                                       datagram.size(),
                                                       &sender,
                                                       &senderPort);
        if (bytesRead < 0) {
            qWarning() << "UDP readDatagram failed:" << m_socket.errorString();
            continue;
        }

        qDebug() << "UDP datagram from" << sender.toString() << ":" << senderPort
                 << "bytes=" << bytesRead;
        const std::optional<UdpLoggedContact> loggedContact = decodeWsjtxDatagram(datagram);
        if (loggedContact.has_value()) {
            emit loggedContactReceived(loggedContact.value());
        }
    }
}
