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
constexpr const char *kDxccTableName = "DXCC";

const QStringList kContactFields = {
    QStringLiteral("date"),
    QStringLiteral("time"),
    QStringLiteral("call"),
    QStringLiteral("band"),
    QStringLiteral("frequency"),
    QStringLiteral("mode"),
    QStringLiteral("submode"),
    QStringLiteral("country"),
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
    query.bindValue(QStringLiteral(":date"), contact.date.isNull() ? QStringLiteral("") : contact.date);
    query.bindValue(QStringLiteral(":time"), contact.time.isNull() ? QStringLiteral("") : contact.time);
    query.bindValue(QStringLiteral(":call"), contact.call.isNull() ? QStringLiteral("") : contact.call);
    query.bindValue(QStringLiteral(":band"), contact.band.isNull() ? QStringLiteral("") : contact.band);
    query.bindValue(QStringLiteral(":frequency"), contact.frequency.isNull() ? QStringLiteral("") : contact.frequency);
    query.bindValue(QStringLiteral(":mode"), contact.mode.isNull() ? QStringLiteral("") : contact.mode);
    query.bindValue(QStringLiteral(":submode"), contact.submode);
    query.bindValue(QStringLiteral(":country"), contact.country);
    query.bindValue(QStringLiteral(":grid"), contact.grid);
    query.bindValue(QStringLiteral(":rst_tx"), contact.rstTx);
    query.bindValue(QStringLiteral(":rst_rx"), contact.rstRx);
    query.bindValue(QStringLiteral(":qsl"), contact.qsl);
    query.bindValue(QStringLiteral(":comment"), contact.comment);
}

QString confirmedCountryPredicate()
{
    return QStringLiteral(
        "TRIM(country) <> '' "
        "AND (UPPER(TRIM(qsl)) = 'C' OR TRIM(qsl) = '')");
}

QString countDistinctCountries(QSqlDatabase db, const QString &extraCondition, QString *errorMessage)
{
    QString statement = QStringLiteral("SELECT COUNT(DISTINCT UPPER(TRIM(country))) FROM contacts WHERE ")
                        + confirmedCountryPredicate();
    if (!extraCondition.isEmpty()) {
        statement += QStringLiteral(" AND ") + extraCondition;
    }

    QSqlQuery query(db);
    if (!query.exec(statement) || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return QStringLiteral("0");
    }

    return query.value(0).toString();
}

QString countChallengeSlots(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT COUNT(DISTINCT UPPER(TRIM(country)) || '|' || LOWER(TRIM(band))) "
            "FROM contacts "
            "WHERE %1 "
            "AND LOWER(TRIM(band)) IN ("
            "'160 m', '80 m', '40 m', '30 m', '20 m', '17 m', '15 m', '12 m', "
            "'10 m', '6 m', '2 m', '70 cm')").arg(confirmedCountryPredicate()))
        || !query.next()) {
        setError(errorMessage, query.lastError().text());
        return QStringLiteral("0");
    }

    return query.value(0).toString();
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
        const bool hasIdField = record.indexOf(QStringLiteral("id")) >= 0;
        for (int index = 0; index < record.count(); ++index) {
            if (record.fieldName(index) != QStringLiteral("id")) {
                existingFields.append(record.fieldName(index));
            }
        }

        if (existingFields != kContactFields) {
            QStringList insertFields = kContactFields;
            QStringList selectExpressions;
            if (hasIdField) {
                insertFields.prepend(QStringLiteral("id"));
                selectExpressions.append(QStringLiteral("id"));
            }

            for (const QString &field : kContactFields) {
                if (existingFields.contains(field)) {
                    if (field == QStringLiteral("date")
                        || field == QStringLiteral("time")
                        || field == QStringLiteral("call")
                        || field == QStringLiteral("band")
                        || field == QStringLiteral("frequency")
                        || field == QStringLiteral("mode")) {
                        selectExpressions.append(QStringLiteral("COALESCE(%1, '')").arg(field));
                    } else {
                        selectExpressions.append(field);
                    }
                } else if (field == QStringLiteral("time")
                           && existingFields.contains(QStringLiteral("time_on"))
                           && existingFields.contains(QStringLiteral("time_off"))) {
                    selectExpressions.append(QStringLiteral("COALESCE(NULLIF(time_off, ''), time_on, '')"));
                } else {
                    selectExpressions.append(QStringLiteral("''"));
                }
            }

            if (!execOrError(query, QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"), errorMessage)
                || !createContactsTable(errorMessage)
                || !execOrError(query,
                                QStringLiteral("INSERT INTO contacts (%1) SELECT %2 FROM contacts_old")
                                    .arg(insertFields.join(QStringLiteral(", ")),
                                         selectExpressions.join(QStringLiteral(", "))),
                                errorMessage)
                || !execOrError(query, QStringLiteral("DROP TABLE contacts_old"), errorMessage)) {
                return false;
            }
        }
    }

    return createContactsTable(errorMessage)
        && createDxccTable(errorMessage)
        && refreshDxccSummary(errorMessage);
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
    model->setHeaderData(model->fieldIndex(QStringLiteral("country")), Qt::Horizontal, QStringLiteral("Country"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("grid")), Qt::Horizontal, QStringLiteral("Grid"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("rst_tx")), Qt::Horizontal, QStringLiteral("RST TX"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("rst_rx")), Qt::Horizontal, QStringLiteral("RST RX"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("qsl")), Qt::Horizontal, QStringLiteral("QSL"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("comment")), Qt::Horizontal, QStringLiteral("Comment"));

    return model;
}

QSqlTableModel *ContactDatabase::createDxccModel(QObject *parent) const
{
    QSqlTableModel *model = new QSqlTableModel(parent, database());
    model->setTable(QString::fromLatin1(kDxccTableName));
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    model->setSort(model->fieldIndex(QStringLiteral("sort_order")), Qt::AscendingOrder);
    model->select();

    model->setHeaderData(model->fieldIndex(QStringLiteral("award")), Qt::Horizontal, QStringLiteral("Award"));
    model->setHeaderData(model->fieldIndex(QStringLiteral("countries")), Qt::Horizontal, QStringLiteral("Countries"));

    return model;
}

bool ContactDatabase::addContact(const Contact &contact, QVariant *insertedId, QString *errorMessage) const
{
    QSqlQuery insertQuery(database());
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, country, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :country, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));
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
        "SELECT id, country, grid, qsl FROM contacts "
        "WHERE date = :date AND time = :time AND UPPER(call) = UPPER(:call) "
        "AND band = :band AND mode = :mode LIMIT 1"));

    QSqlQuery updateContactQuery(db);
    updateContactQuery.prepare(QStringLiteral("UPDATE contacts SET country = :country, grid = :grid, qsl = :qsl WHERE id = :id"));

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, country, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES "
        "(:date, :time, :call, :band, :frequency, :mode, :submode, :country, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));

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
            const QString existingCountry = duplicateQuery.value(1).toString().trimmed();
            const QString existingGrid = duplicateQuery.value(2).toString().trimmed();
            const QString existingQsl = duplicateQuery.value(3).toString().trimmed();
            const bool shouldUpdateCountry = existingCountry.isEmpty() && !contact.country.isEmpty();
            const bool shouldUpdateGrid = existingGrid.isEmpty() && !contact.grid.isEmpty();
            const bool shouldUpdateQsl = existingQsl != contact.qsl;
            if (shouldUpdateCountry || shouldUpdateGrid || shouldUpdateQsl) {
                updateContactQuery.bindValue(QStringLiteral(":country"),
                                             shouldUpdateCountry ? contact.country : existingCountry);
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

bool ContactDatabase::refreshDxccSummary(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        setError(errorMessage, db.lastError().text());
        return false;
    }

    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM DXCC"))) {
        db.rollback();
        setError(errorMessage, deleteQuery.lastError().text());
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO DXCC (sort_order, award, countries) VALUES (:sort_order, :award, :countries)"));

    struct AwardQuery
    {
        const char *award;
        QString condition;
        QString band;
        bool challenge = false;
    };

    const QList<AwardQuery> awards = {
        {"Mixed", QString(), QString(), false},
        {"CW", QStringLiteral("mode = 'CW'"), QString(), false},
        {"Phone", QStringLiteral("mode = 'Phone'"), QString(), false},
        {"Digital", QStringLiteral("mode = 'Data'"), QString(), false},
        {"Satellite", QStringLiteral("mode = 'Sat'"), QString(), false},
        {"160M", QString(), QStringLiteral("160 m"), false},
        {"80M", QString(), QStringLiteral("80 m"), false},
        {"40M", QString(), QStringLiteral("40 m"), false},
        {"30M", QString(), QStringLiteral("30 m"), false},
        {"20M", QString(), QStringLiteral("20 m"), false},
        {"17M", QString(), QStringLiteral("17 m"), false},
        {"15M", QString(), QStringLiteral("15 m"), false},
        {"12M", QString(), QStringLiteral("12 m"), false},
        {"10M", QString(), QStringLiteral("10 m"), false},
        {"6M", QString(), QStringLiteral("6 m"), false},
        {"2m", QString(), QStringLiteral("2 m"), false},
        {"70CM", QString(), QStringLiteral("70 cm"), false},
        {"Challenge", QString(), QString(), true}
    };

    for (int index = 0; index < awards.size(); ++index) {
        const AwardQuery &award = awards.at(index);
        QString condition = award.condition;
        if (!award.band.isEmpty()) {
            if (!condition.isEmpty()) {
                condition += QStringLiteral(" AND ");
            }
            condition += QStringLiteral("LOWER(TRIM(band)) = LOWER('%1')").arg(award.band);
        }

        const QString count = award.challenge
                                  ? countChallengeSlots(db, errorMessage)
                                  : countDistinctCountries(db, condition, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            db.rollback();
            return false;
        }

        insertQuery.bindValue(QStringLiteral(":sort_order"), index);
        insertQuery.bindValue(QStringLiteral(":award"), QString::fromLatin1(award.award));
        insertQuery.bindValue(QStringLiteral(":countries"), count.toInt());
        if (!insertQuery.exec()) {
            db.rollback();
            setError(errorMessage, insertQuery.lastError().text());
            return false;
        }
    }

    if (!db.commit()) {
        db.rollback();
        setError(errorMessage, db.lastError().text());
        return false;
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
        "country TEXT,"
        "grid TEXT,"
        "rst_tx TEXT,"
        "rst_rx TEXT,"
        "qsl TEXT,"
        "comment TEXT"
        ")"), errorMessage);
}

bool ContactDatabase::createDxccTable(QString *errorMessage)
{
    QSqlQuery query(database());
    return execOrError(query, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS DXCC ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sort_order INTEGER NOT NULL,"
        "award TEXT NOT NULL UNIQUE,"
        "countries INTEGER NOT NULL"
        ")"), errorMessage);
}
