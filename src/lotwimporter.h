#ifndef LOTWIMPORTER_H
#define LOTWIMPORTER_H

#include "contact.h"

#include <QByteArray>
#include <QList>
#include <QString>

struct LotwParseResult
{
    QList<Contact> contacts;
    int invalid = 0;
    QString errorMessage;
    bool credentialsRejected = false;
};

class LotwImporter
{
public:
    LotwParseResult parseReport(const QByteArray &data) const;
    bool credentialsRejected(const QByteArray &data) const;
};

#endif // LOTWIMPORTER_H
