#ifndef CONTACTDATABASE_H
#define CONTACTDATABASE_H

#include "contact.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

class QSqlTableModel;

struct ContactImportSummary
{
    int imported = 0;
    int duplicates = 0;
    int gridsUpdated = 0;
    int qslUpdated = 0;
};

class ContactDatabase
{
public:
    bool initialize(QString *errorMessage = nullptr);
    QSqlTableModel *createModel(QObject *parent = nullptr) const;
    QSqlTableModel *createDxccModel(QObject *parent = nullptr) const;

    bool addContact(const Contact &contact, QVariant *insertedId = nullptr, QString *errorMessage = nullptr) const;
    bool clearAllContacts(int *deletedCount = nullptr, QString *errorMessage = nullptr) const;
    bool importContacts(const QList<Contact> &contacts,
                        ContactImportSummary *summary = nullptr,
                        QString *errorMessage = nullptr) const;
    bool refreshDxccSummary(QString *errorMessage = nullptr) const;

    static QString connectionName();
    static QString tableName();

private:
    static bool createContactsTable(QString *errorMessage);
    static bool createDxccTable(QString *errorMessage);
};

#endif // CONTACTDATABASE_H
