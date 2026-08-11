#ifndef LOTWCREDENTIALS_H
#define LOTWCREDENTIALS_H

#include <QString>

struct LotwCredentials
{
    QString username;
    QString password;
};

class LotwCredentialStore
{
public:
    LotwCredentials load() const;
    void save(const QString &username, const QString &password) const;
};

#endif // LOTWCREDENTIALS_H
