#include "contactdatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QStringList>

namespace {

constexpr const char *kConnectionName = "bandpilot";
constexpr const char *kTableName = "contacts";

const QStringList kContactFields = {
    QStringLiteral("date"),
    QStringLiteral("time"),
    QStringLiteral("call"),
    QStringLiteral("band"),
    QStringLiteral("frequency"),
    QStringLiteral("mode"),
    QStringLiteral("submode"),
    QStringLiteral("grid"),
    QStringLiteral("rst_tx"),
    QStringLiteral("rst_rx"),
    QStringLiteral("qsl"),
    QStringLiteral("comment")
};

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

QSqlDatabase database()
{
    return QSqlDatabase::database(kConnectionName);
}

bool execOrError(QSqlQuery &query, const QString &statement, QString *errorMessage)
{
    if (query.exec(statement)) {
        return true;
    }

    setError(errorMessage, query.lastError().text());
    return false;
}

void bindContact(QSqlQuery &query, const Contact &contact)
{
    query.bindValue(QStringLiteral(":date"), contact.date);
    query.bindValue(QStringLiteral(":time"), contact.time);
    query.bindValue(QStringLiteral(":call"), contact.call);
    query.bindValue(QStringLiteral(":band"), contact.band);
    query.bindValue(QStringLiteral(":frequency"), contact.frequency);
    query.bindValue(QStringLiteral(":mode"), contact.mode);
    query.bindValue(QStringLiteral(":submode"), contact.submode);
    query.bindValue(QStringLiteral(":grid"), contact.grid);
    query.bindValue(QStringLiteral(":rst_tx"), contact.rstTx);
    query.bindValue(QStringLiteral(":rst_rx"), contact.rstRx);
    query.bindValue(QStringLiteral(":qsl"), contact.qsl);
    query.bindValue(QStringLiteral(":comment"), contact.comment);
}

} // namespace

QString ContactDatabase::connectionName()
{
    return QString::fromLatin1(kConnectionName);
}

QString ContactDatabase::tableName()
{
    return QString::fromLatin1(kTableName);
}

bool ContactDatabase::initialize(QString *errorMessage)
{
    QSqlDatabase db;
    if (QSqlDatabase::contains(kConnectionName)) {
        db = QSqlDatabase::database(kConnectionName);
    } else {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    }

    const QString databasePath = QCoreApplication::applicationDirPath()
                                 + QDir::separator()
                                 + QStringLiteral("bandpilot.sqlite");
    db.setDatabaseName(databasePath);

    if (!db.open()) {
        setError(errorMessage, db.lastError().text());
        return false;
    }

    QSqlQuery query(db);
    if (db.tables().contains(QString::fromLatin1(kTableName))) {
        QSqlRecord record = db.record(QString::fromLatin1(kTableName));
        QStringList existingFields;
        for (int index = 0; index < record.count(); ++index) {
            if (record.fieldName(index) != QStringLiteral("id")) {
                existingFields.append(record.fieldName(index));
            }
        }

        if (existingFields != kContactFields) {
            if (existingFields == QStringList{
                    QStringLiteral("date"),
                    QStringLiteral("time_on"),
                    QStringLiteral("time_off"),
                    QStringLiteral("call"),
                    QStringLiteral("band"),
                    QStringLiteral("frequency"),
                    QStringLiteral("mode"),
                    QStringLiteral("submode"),
                    QStringLiteral("comment")
                }) {
                if (!execOrError(query, QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"), errorMessage)
                    || !createContactsTable(errorMessage)
                    || !execOrError(query, QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, COALESCE(NULLIF(time_off, ''), time_on), call, band, frequency, mode, submode, '', '', '', '', comment "
                        "FROM contacts_old"), errorMessage)
                    || !execOrError(query, QStringLiteral("DROP TABLE contacts_old"), errorMessage)) {
                    return false;
                }
            } else if (existingFields == QStringList{
                    QStringLiteral("date"),
                    QStringLiteral("time"),
                    QStringLiteral("call"),
                    QStringLiteral("band"),
                    QStringLiteral("frequency"),
                    QStringLiteral("mode"),
                    QStringLiteral("submode"),
                    QStringLiteral("comment")
                }) {
                if (!execOrError(query, QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"), errorMessage)
                    || !createContactsTable(errorMessage)
                    || !execOrError(query, QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, time, call, band, frequency, mode, submode, '', '', '', '', comment "
                        "FROM contacts_old"), errorMessage)
                    || !execOrError(query, QStringLiteral("DROP TABLE contacts_old"), errorMessage)) {
                    return false;
                }
            } else if (existingFields == QStringList{
                    QStringLiteral("date"),
                    QStringLiteral("time"),
                    QStringLiteral("call"),
                    QStringLiteral("band"),
                    QStringLiteral("frequency"),
                    QStringLiteral("mode"),
                    QStringLiteral("submode"),
                    QStringLiteral("grid"),
                    QStringLiteral("rst_tx"),
                    QStringLiteral("rst_rx"),
                    QStringLiteral("comment")
                }) {
                if (!execOrError(query, QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"), errorMessage)
                    || !createContactsTable(errorMessage)
                    || !execOrError(query, QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, '', comment "
                        "FROM contacts_old"), errorMessage)
                    || !execOrError(query, QStringLiteral("DROP TABLE contacts_old"), errorMessage)) {
                    return false;
                }
            } else if (!execOrError(query, QStringLiteral("DROP TABLE contacts"), errorMessage)) {
                return false;
            }
        }
    }

    return createContactsTable(errorMessage);
}

QSqlTableModel *ContactDatabase::createModel(QObject *parent) const
{
    QSqlTableModel *model = new QSqlTableModel(parent, database());
    model->setTable(QString::fromLatin1(kTableName));
    model->setEditStrategy(QSqlTableModel::OnFieldChange);
    model->setSort(model->fieldIndex(QStringLiteral("date")), Qt::AscendingOrder);
    model->select();

    model->setHeaderData(model->fieldIndex(QStringLiteral("date")), Qt::Horizontal, QStringLiteral("Date"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("time")), Qt::Horizontal, QStringLiteral("Time"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("call")), Qt::Horizontal, QStringLiteral("Call"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("band")), Qt::Horizontal, QStringLiteral("Band"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("frequency")), Qt::Horizontal, QStringLiteral("Frequency"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("mode")), Qt::Horizontal, QStringLiteral("Mode"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("submode")), Qt::Horizontal, QStringLiteral("Submode"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("grid")), Qt::Horizontal, QStringLiteral("Grid"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("rst_tx")), Qt::Horizontal, QStringLiteral("RST TX"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("rst_rx")), Qt::Horizontal, QStringLiteral("RST RX"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("qsl")), Qt::Horizontal, QStringLiteral("QSL"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("comment")), Qt::Horizontal, QStringLiteral("Comment"));

    return model;
}

bool ContactDatabase::addContact(const Contact &contact, QVariant *insertedId, QString *errorMessage) const
{
    QSqlQuery insertQuery(database());
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));
    bindContact(insertQuery, contact);

    if (!insertQuery.exec()) {
        setError(errorMessage, insertQuery.lastError().text());
        return false;
    }

    if (insertedId) {
        *insertedId = insertQuery.lastInsertId();
    }
    return true;
}

bool ContactDatabase::clearAllContacts(int *deletedCount, QString *errorMessage) const
{
    QSqlDatabase db = database();
    if (!db.transaction()) {
        setError(errorMessage, db.lastError().text());
        return false;
    }

    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")) || !countQuery.next()) {
        db.rollback();
        setError(errorMessage, countQuery.lastError().text());
        return false;
    }

    const int count = countQuery.value(0).toInt();
    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM contacts"))) {
        db.rollback();
        setError(errorMessage, deleteQuery.lastError().text());
        return false;
    }

    if (!db.commit()) {
        db.rollback();
        setError(errorMessage, db.lastError().text());
        return false;
    }

    if (deletedCount) {
        *deletedCount = count;
    }
    return true;
}

bool ContactDatabase::importContacts(const QList<Contact> &contacts,
                                     ContactImportSummary *summary,
                                     QString *errorMessage) const
{
    ContactImportSummary result;
    QSqlDatabase db = database();
    if (!db.transaction()) {
        setError(errorMessage, db.lastError().text());
        return false;
    }

    QSqlQuery duplicateQuery(db);
    duplicateQuery.prepare(QStringLiteral(
        "SELECT id, grid, qsl FROM contacts "
        "WHERE date = :date AND time = :time AND UPPER(call) = UPPER(:call) "
        "AND band = :band AND mode = :mode LIMIT 1"));

    QSqlQuery updateContactQuery(db);
    updateContactQuery.prepare(QStringLiteral("UPDATE contacts SET grid = :grid, qsl = :qsl WHERE id = :id"));

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES "
        "(:date, :time, :call, :band, :frequency, :mode, :submode, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));

    for (const Contact &contact : contacts) {
        duplicateQuery.bindValue(QStringLiteral(":date"), contact.date);
        duplicateQuery.bindValue(QStringLiteral(":time"), contact.time);
        duplicateQuery.bindValue(QStringLiteral(":call"), contact.call);
        duplicateQuery.bindValue(QStringLiteral(":band"), contact.band);
        duplicateQuery.bindValue(QStringLiteral(":mode"), contact.mode);
        if (!duplicateQuery.exec()) {
            db.rollback();
            setError(errorMessage, duplicateQuery.lastError().text());
            return false;
        }

        if (duplicateQuery.next()) {
            const QString existingGrid = duplicateQuery.value(1).toString().trimmed();
            const QString existingQsl = duplicateQuery.value(2).toString().trimmed();
            const bool shouldUpdateGrid = existingGrid.isEmpty() && !contact.grid.isEmpty();
            const bool shouldUpdateQsl = existingQsl != contact.qsl;
            if (shouldUpdateGrid || shouldUpdateQsl) {
                updateContactQuery.bindValue(QStringLiteral(":grid"),
                                             shouldUpdateGrid ? contact.grid : existingGrid);
                updateContactQuery.bindValue(QStringLiteral(":qsl"), contact.qsl);
                updateContactQuery.bindValue(QStringLiteral(":id"), duplicateQuery.value(0));
                if (!updateContactQuery.exec()) {
                    db.rollback();
                    setError(errorMessage, updateContactQuery.lastError().text());
                    return false;
                }
                if (shouldUpdateGrid) {
                    ++result.gridsUpdated;
                }
                if (shouldUpdateQsl) {
                    ++result.qslUpdated;
                }
            }
            ++result.duplicates;
            continue;
        }

        bindContact(insertQuery, contact);
        if (!insertQuery.exec()) {
            db.rollback();
            setError(errorMessage, insertQuery.lastError().text());
            return false;
        }
        ++result.imported;
    }

    if (!db.commit()) {
        db.rollback();
        setError(errorMessage, db.lastError().text());
        return false;
    }

    if (summary) {
        *summary = result;
    }
    return true;
}

bool ContactDatabase::createContactsTable(QString *errorMessage)
{
    QSqlQuery query(database());
    return execOrError(query, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS contacts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "date TEXT NOT NULL,"
        "time TEXT NOT NULL,"
        "call TEXT NOT NULL,"
        "band TEXT NOT NULL,"
        "frequency TEXT NOT NULL,"
        "mode TEXT NOT NULL,"
        "submode TEXT,"
        "grid TEXT,"
        "rst_tx TEXT,"
        "rst_rx TEXT,"
        "qsl TEXT,"
        "comment TEXT"
        ")"), errorMessage);
}
