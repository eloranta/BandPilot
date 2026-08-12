#include "lotwimporter.h"

#include <QDate>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>

namespace {

using AdifRecord = QHash<QString, QString>;

QList<AdifRecord> parseAdif(const QByteArray &data)
{
    QList<AdifRecord> records;
    AdifRecord record;
    qsizetype position = 0;

    while (position < data.size()) {
        const qsizetype tagStart = data.indexOf('<', position);
        if (tagStart < 0) {
            break;
        }

        const qsizetype tagEnd = data.indexOf('>', tagStart + 1);
        if (tagEnd < 0) {
            break;
        }

        const QByteArray descriptor = data.mid(tagStart + 1, tagEnd - tagStart - 1).trimmed();
        const QList<QByteArray> parts = descriptor.split(':');
        const QString fieldName = QString::fromLatin1(parts.value(0)).trimmed().toUpper();
        position = tagEnd + 1;

        if (fieldName == QStringLiteral("EOH")) {
            record.clear();
            continue;
        }
        if (fieldName == QStringLiteral("EOR")) {
            if (!record.isEmpty()) {
                records.append(record);
                record.clear();
            }
            continue;
        }
        if (parts.size() < 2) {
            continue;
        }

        bool lengthOk = false;
        const qsizetype valueLength = parts.at(1).trimmed().toLongLong(&lengthOk);
        if (!lengthOk || valueLength < 0 || position + valueLength > data.size()) {
            break;
        }

        record.insert(fieldName, QString::fromUtf8(data.mid(position, valueLength)).trimmed());
        position += valueLength;
    }

    return records;
}

QString adifDate(const QString &value)
{
    const QDate date = QDate::fromString(value.trimmed(), QStringLiteral("yyyyMMdd"));
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

QString adifTime(const QString &value)
{
    QString digits = value.trimmed();
    if (digits.size() == 4) {
        digits.append(QStringLiteral("00"));
    }
    const QTime time = QTime::fromString(digits, QStringLiteral("HHmmss"));
    return time.isValid() ? time.toString(QStringLiteral("HH:mm:ss")) : QString();
}

QString displayBand(const QString &value)
{
    static const QRegularExpression meterPattern(QStringLiteral("^(\\d+(?:\\.\\d+)?)M$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression centimeterPattern(QStringLiteral("^(\\d+(?:\\.\\d+)?)CM$"),
                                                      QRegularExpression::CaseInsensitiveOption);

    const QString trimmed = value.trimmed();
    const QRegularExpressionMatch centimeterMatch = centimeterPattern.match(trimmed);
    if (centimeterMatch.hasMatch()) {
        return centimeterMatch.captured(1) + QStringLiteral(" cm");
    }

    const QRegularExpressionMatch match = meterPattern.match(trimmed);
    return match.hasMatch() ? match.captured(1) + QStringLiteral(" m") : value.trimmed();
}

QString displayGrid(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2})?)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QString grid = value.trimmed().toUpper();
    return pattern.match(grid).hasMatch() ? grid : QString();
}

QString recordGrid(const AdifRecord &record)
{
    const QStringList fields = {
        QStringLiteral("GRIDSQUARE"),
        QStringLiteral("VUCC_GRIDS"),
        QStringLiteral("APP_LOTW_GRIDSQUARE"),
        QStringLiteral("APP_LOTW_VUCC_GRIDS")
    };

    for (const QString &field : fields) {
        const QStringList candidates = record.value(field).split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                                 Qt::SkipEmptyParts);
        for (const QString &candidate : candidates) {
            const QString grid = displayGrid(candidate);
            if (!grid.isEmpty()) {
                return grid;
            }
        }
    }

    for (auto it = record.cbegin(); it != record.cend(); ++it) {
        if (!it.key().contains(QStringLiteral("GRID")) || it.key().startsWith(QStringLiteral("MY_"))) {
            continue;
        }

        const QStringList candidates = it.value().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                        Qt::SkipEmptyParts);
        for (const QString &candidate : candidates) {
            const QString grid = displayGrid(candidate);
            if (!grid.isEmpty()) {
                return grid;
            }
        }
    }

    return QString();
}

QString recordCountry(const AdifRecord &record)
{
    const QStringList fields = {
        QStringLiteral("COUNTRY"),
        QStringLiteral("APP_LOTW_COUNTRY"),
        QStringLiteral("APP_LOTW_DXCCRECORD_COUNTRY")
    };

    for (const QString &field : fields) {
        const QString country = record.value(field).trimmed();
        if (!country.isEmpty()) {
            return country;
        }
    }

    return QString();
}

QString displayMode(const QString &value)
{
    const QString mode = value.trimmed().toUpper();
    if (mode == QStringLiteral("CW")) {
        return QStringLiteral("CW");
    }
    if (mode == QStringLiteral("SSB")
        || mode == QStringLiteral("USB")
        || mode == QStringLiteral("LSB")
        || mode == QStringLiteral("AM")
        || mode == QStringLiteral("FM")) {
        return QStringLiteral("Phone");
    }
    if (mode.contains(QStringLiteral("SAT"))) {
        return QStringLiteral("Sat");
    }
    return QStringLiteral("Data");
}

QString displayQslStatus(const AdifRecord &record)
{
    QString status = record.value(QStringLiteral("LOTW_QSL_RCVD")).trimmed().toUpper();
    if (status.isEmpty()) {
        status = record.value(QStringLiteral("APP_LOTW_QSL_RCVD")).trimmed().toUpper();
    }
    if (status.isEmpty()) {
        status = record.value(QStringLiteral("QSL_RCVD")).trimmed().toUpper();
    }

    if (status == QStringLiteral("Y")) {
        return QStringLiteral("C");
    }
    if (status == QStringLiteral("N") || status.isEmpty()) {
        return QString();
    }
    if (status == QStringLiteral("R")) {
        return QStringLiteral("Requested");
    }
    if (status == QStringLiteral("I")) {
        return QStringLiteral("Ignored");
    }
    if (status == QStringLiteral("V")) {
        return QStringLiteral("V");
    }
    return status;
}

QString lotwResponseMessage(const QByteArray &data)
{
    QString message = QString::fromUtf8(data);
    message.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    message = message.simplified();
    if (message.size() > 800) {
        message = message.left(800) + QStringLiteral("...");
    }
    return message;
}

QString lotwResponseError(const QByteArray &data)
{
    const QString response = QString::fromUtf8(data);
    const QString plainText = lotwResponseMessage(data);

    if (plainText.contains(QStringLiteral("Username/password incorrect"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Log On"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Username:"), Qt::CaseInsensitive)) {
        return QStringLiteral("LoTW rejected the saved username or password. Update them in Settings -> LoTW.");
    }

    if (response.contains(QStringLiteral("<html"), Qt::CaseInsensitive)
        || response.contains(QStringLiteral("<!doctype html"), Qt::CaseInsensitive)) {
        return plainText.isEmpty()
                   ? QStringLiteral("LoTW returned an HTML page instead of an ADIF report.")
                   : QStringLiteral("LoTW returned an HTML page instead of an ADIF report:\n\n%1")
                         .arg(plainText);
    }

    return QString();
}

bool isCredentialsRejectedMessage(const QByteArray &data)
{
    const QString plainText = lotwResponseMessage(data);
    return plainText.contains(QStringLiteral("Username/password incorrect"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Log On"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Username:"), Qt::CaseInsensitive);
}

} // namespace

LotwParseResult LotwImporter::parseReport(const QByteArray &data) const
{
    LotwParseResult result;
    result.errorMessage = lotwResponseError(data);
    result.credentialsRejected = result.errorMessage.contains(QStringLiteral("username or password"),
                                                              Qt::CaseInsensitive);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    const QList<AdifRecord> records = parseAdif(data);
    if (records.isEmpty()) {
        const QString serverMessage = lotwResponseMessage(data);
        result.errorMessage = serverMessage.isEmpty()
                                  ? QStringLiteral("LoTW returned an empty response.")
                                  : QStringLiteral("LoTW returned no QSO records:\n\n%1")
                                        .arg(serverMessage);
        return result;
    }

    for (const AdifRecord &record : records) {
        Contact contact;
        contact.date = adifDate(record.value(QStringLiteral("QSO_DATE")));
        contact.time = adifTime(record.value(QStringLiteral("TIME_ON")));
        if (contact.time.isEmpty() && record.contains(QStringLiteral("APP_LOTW_DXCC_PROCESSED_DTG"))) {
            contact.time = QStringLiteral("00:00:00");
        }
        contact.call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
        contact.band = displayBand(record.value(QStringLiteral("BAND")));

        QString adifMode = record.value(QStringLiteral("MODE")).trimmed().toUpper();
        if (adifMode.isEmpty()) {
            adifMode = record.value(QStringLiteral("APP_LOTW_MODE")).trimmed().toUpper();
        }
        if (adifMode.isEmpty()) {
            adifMode = record.value(QStringLiteral("APP_LOTW_MODEGROUP")).trimmed().toUpper();
        }
        contact.mode = displayMode(adifMode);

        if (contact.date.isEmpty()
            || contact.time.isEmpty()
            || contact.call.isEmpty()
            || contact.band.isEmpty()
            || adifMode.isEmpty()) {
            ++result.invalid;
            continue;
        }

        contact.grid = recordGrid(record);
        contact.country = recordCountry(record);
        contact.qsl = displayQslStatus(record);
        const QString frequency = record.value(QStringLiteral("FREQ")).trimmed();
        contact.frequency = frequency.isNull() ? QStringLiteral("") : frequency;
        contact.rstTx = record.value(QStringLiteral("RST_SENT")).trimmed();
        contact.rstRx = record.value(QStringLiteral("RST_RCVD")).trimmed();
        contact.comment = record.value(QStringLiteral("COMMENT")).trimmed();

        contact.submode = record.value(QStringLiteral("SUBMODE")).trimmed().toUpper();
        if (contact.submode.isEmpty()) {
            contact.submode = record.value(QStringLiteral("APP_LOTW_MODE")).trimmed().toUpper();
        }
        if (contact.submode.isEmpty() && contact.mode != adifMode) {
            contact.submode = adifMode;
        }

        result.contacts.append(contact);
    }

    return result;
}

bool LotwImporter::credentialsRejected(const QByteArray &data) const
{
    return isCredentialsRejectedMessage(data);
}
