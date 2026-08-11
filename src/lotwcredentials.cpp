#include "lotwcredentials.h"

#include <QByteArray>
#include <QSettings>

namespace {

constexpr const char *kLotwUsernameKey = "lotw/username";
constexpr const char *kLotwPasswordKey = "lotw/password";
constexpr const char *kObfuscationKey = "BandPilotLoTW";

QString obfuscatePassword(const QString &password)
{
    QByteArray data = password.toUtf8();
    const QByteArray key(kObfuscationKey);
    for (qsizetype i = 0; i < data.size(); ++i) {
        data[i] = data.at(i) ^ key.at(i % key.size());
    }
    return QString::fromLatin1(data.toBase64());
}

QString deobfuscatePassword(const QString &storedPassword)
{
    QByteArray data = QByteArray::fromBase64(storedPassword.toLatin1());
    const QByteArray key(kObfuscationKey);
    for (qsizetype i = 0; i < data.size(); ++i) {
        data[i] = data.at(i) ^ key.at(i % key.size());
    }
    return QString::fromUtf8(data);
}

} // namespace

LotwCredentials LotwCredentialStore::load() const
{
    QSettings settings;
    LotwCredentials credentials;
    credentials.username = settings.value(QString::fromLatin1(kLotwUsernameKey)).toString();

    const QString storedPassword = settings.value(QString::fromLatin1(kLotwPasswordKey)).toString();
    credentials.password = storedPassword.isEmpty() ? QString() : deobfuscatePassword(storedPassword);
    return credentials;
}

void LotwCredentialStore::save(const QString &username, const QString &password) const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLotwUsernameKey), username.trimmed());
    settings.setValue(QString::fromLatin1(kLotwPasswordKey), obfuscatePassword(password.trimmed()));
}
