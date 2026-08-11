#include "mainwindow.h"
#include "udpreceiver.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>
#include <QUrlQuery>
#include <QVariant>

#include <algorithm>

namespace {

constexpr const char *kConnectionName = "bandpilot";
constexpr const char *kTableName = "contacts";
constexpr const char *kLotwUsernameKey = "lotw/username";
constexpr const char *kLotwPasswordKey = "lotw/password";
constexpr const char *kObfuscationKey = "BandPilotLoTW";

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

using AdifRecord = QHash<QString, QString>;

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

QString lotwUsername()
{
    return QSettings().value(QString::fromLatin1(kLotwUsernameKey)).toString();
}

QString lotwPassword()
{
    const QString storedPassword = QSettings().value(QString::fromLatin1(kLotwPasswordKey)).toString();
    return storedPassword.isEmpty() ? QString() : deobfuscatePassword(storedPassword);
}

void saveLotwCredentials(const QString &username, const QString &password)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLotwUsernameKey), username.trimmed());
    settings.setValue(QString::fromLatin1(kLotwPasswordKey), obfuscatePassword(password.trimmed()));
}

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
    static const QRegularExpression pattern(QStringLiteral("^(\\d+(?:\\.\\d+)?)M$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(value.trimmed());
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

bool lotwCredentialsRejected(const QByteArray &data)
{
    const QString plainText = lotwResponseMessage(data);
    return plainText.contains(QStringLiteral("Username/password incorrect"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Log On"), Qt::CaseInsensitive)
        || plainText.contains(QStringLiteral("Username:"), Qt::CaseInsensitive);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("BandPilot");
    resize(760, 420);

    if (!initializeDatabase()) {
        QMessageBox::critical(this, "Database Error",
                              "Could not initialize the BandPilot database.");
    }

    setupModel();
    setupUi();

    m_networkManager = new QNetworkAccessManager(this);
    m_udpReceiver = new UdpReceiver(this);
    connect(m_udpReceiver, &UdpReceiver::loggedContactReceived, this, [this](const UdpLoggedContact &contact) {
        if (addLoggedContact(contact)) {
            statusBar()->showMessage(QStringLiteral("Logged UDP contact: %1").arg(contact.call), 5000);
        }
    });

    if (!m_udpReceiver->start()) {
        statusBar()->showMessage("UDP listener failed");
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tableView && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete) {
            deleteSelectedContacts();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::initializeDatabase()
{
    QSqlDatabase database;
    if (QSqlDatabase::contains(kConnectionName)) {
        database = QSqlDatabase::database(kConnectionName);
    } else {
        database = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
    }

    const QString databasePath = QCoreApplication::applicationDirPath()
                                 + QDir::separator()
                                 + "bandpilot.sqlite";
    database.setDatabaseName(databasePath);

    if (!database.open()) {
        statusBar()->showMessage(database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (database.tables().contains(QString::fromLatin1(kTableName))) {
        QSqlRecord record = database.record(kTableName);
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
                if (!query.exec(QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"))
                    || !query.exec(QStringLiteral(
                        "CREATE TABLE contacts ("
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
                        ")"))
                    || !query.exec(QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, COALESCE(NULLIF(time_off, ''), time_on), call, band, frequency, mode, submode, '', '', '', '', comment "
                        "FROM contacts_old"))
                    || !query.exec(QStringLiteral("DROP TABLE contacts_old"))) {
                    statusBar()->showMessage(query.lastError().text());
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
                if (!query.exec(QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"))
                    || !query.exec(QStringLiteral(
                        "CREATE TABLE contacts ("
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
                        ")"))
                    || !query.exec(QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, time, call, band, frequency, mode, submode, '', '', '', '', comment "
                        "FROM contacts_old"))
                    || !query.exec(QStringLiteral("DROP TABLE contacts_old"))) {
                    statusBar()->showMessage(query.lastError().text());
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
                if (!query.exec(QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"))
                    || !query.exec(QStringLiteral(
                        "CREATE TABLE contacts ("
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
                        ")"))
                    || !query.exec(QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
                        "SELECT id, date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, '', comment "
                        "FROM contacts_old"))
                    || !query.exec(QStringLiteral("DROP TABLE contacts_old"))) {
                    statusBar()->showMessage(query.lastError().text());
                    return false;
                }
            } else if (!query.exec(QStringLiteral("DROP TABLE contacts"))) {
                statusBar()->showMessage(query.lastError().text());
                return false;
            }
        }
    }

    if (!query.exec(QStringLiteral(
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
            ")"))) {
        statusBar()->showMessage(query.lastError().text());
        return false;
    }

    return true;
}

bool MainWindow::addLoggedContact(const UdpLoggedContact &contact)
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));
    insertQuery.bindValue(QStringLiteral(":date"), contact.date.toString(Qt::ISODate));
    insertQuery.bindValue(QStringLiteral(":time"), contact.time.toString(QStringLiteral("HH:mm:ss")));
    insertQuery.bindValue(QStringLiteral(":call"), contact.call);
    insertQuery.bindValue(QStringLiteral(":band"), contact.band);
    insertQuery.bindValue(QStringLiteral(":frequency"), contact.frequency);
    insertQuery.bindValue(QStringLiteral(":mode"), contact.mode);
    insertQuery.bindValue(QStringLiteral(":submode"), contact.submode);
    insertQuery.bindValue(QStringLiteral(":grid"), contact.grid);
    insertQuery.bindValue(QStringLiteral(":rst_tx"), contact.rstTx);
    insertQuery.bindValue(QStringLiteral(":rst_rx"), contact.rstRx);
    insertQuery.bindValue(QStringLiteral(":qsl"), QString());
    insertQuery.bindValue(QStringLiteral(":comment"), contact.comment);

    if (!insertQuery.exec()) {
        qWarning().noquote()
            << "UDP contact insert failed"
            << insertQuery.lastError().text()
            << "date=" << contact.date.toString(Qt::ISODate)
            << "time=" << contact.time.toString(QStringLiteral("HH:mm:ss"))
            << "call=" << contact.call
            << "band=" << contact.band
            << "frequency=" << contact.frequency
            << "mode=" << contact.mode
            << "submode=" << contact.submode;
        statusBar()->showMessage(insertQuery.lastError().text(), 5000);
        return false;
    }

    const QVariant insertedId = insertQuery.lastInsertId();
    qDebug().noquote()
        << "UDP contact inserted"
        << "id=" << insertedId.toString()
        << "call=" << contact.call;

    if (!m_model->select()) {
        qWarning().noquote() << "Contact model refresh failed" << m_model->lastError().text();
        statusBar()->showMessage(m_model->lastError().text(), 5000);
        return true;
    }

    if (insertedId.isValid()) {
        const int idColumn = m_model->fieldIndex(QStringLiteral("id"));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            if (m_model->index(row, idColumn).data().toLongLong() == insertedId.toLongLong()) {
                m_tableView->selectRow(row);
                m_tableView->scrollTo(m_model->index(row, 0), QAbstractItemView::PositionAtCenter);
                break;
            }
        }
    }

    m_tableView->resizeColumnsToContents();
    return true;
}

bool MainWindow::deleteSelectedContacts()
{
    const QModelIndexList selectedRows = m_tableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        return false;
    }

    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (const QModelIndex &index : selectedRows) {
        rows.append(index.row());
    }

    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows) {
        if (!m_model->removeRow(row)) {
            statusBar()->showMessage(m_model->lastError().text(), 5000);
            m_model->revertAll();
            return false;
        }
    }

    if (!m_model->submitAll()) {
        statusBar()->showMessage(m_model->lastError().text(), 5000);
        m_model->revertAll();
        m_model->select();
        return false;
    }

    const int deletedCount = rows.size();
    m_model->select();
    statusBar()->showMessage(QStringLiteral("Deleted %1 contact%2")
                                 .arg(deletedCount)
                                 .arg(deletedCount == 1 ? QString() : QStringLiteral("s")),
                             5000);
    return true;
}

void MainWindow::clearAllContacts()
{
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        QStringLiteral("Clear Database"),
        QStringLiteral("This will permanently delete all contacts from the database.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.transaction()) {
        QMessageBox::warning(this, QStringLiteral("Clear Database"), database.lastError().text());
        return;
    }

    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")) || !countQuery.next()) {
        database.rollback();
        QMessageBox::warning(this, QStringLiteral("Clear Database"), countQuery.lastError().text());
        return;
    }

    const int deletedCount = countQuery.value(0).toInt();
    QSqlQuery deleteQuery(database);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM contacts"))) {
        database.rollback();
        QMessageBox::warning(this, QStringLiteral("Clear Database"), deleteQuery.lastError().text());
        return;
    }

    if (!database.commit()) {
        database.rollback();
        QMessageBox::warning(this, QStringLiteral("Clear Database"), database.lastError().text());
        return;
    }

    m_model->select();
    m_tableView->resizeColumnsToContents();
    statusBar()->showMessage(QStringLiteral("Cleared %1 contact%2")
                                 .arg(deletedCount)
                                 .arg(deletedCount == 1 ? QString() : QStringLiteral("s")),
                             5000);
}

void MainWindow::configureLotwSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("LoTW Settings"));

    QLineEdit usernameEdit(lotwUsername(), &dialog);
    QLineEdit passwordEdit(lotwPassword(), &dialog);
    passwordEdit.setEchoMode(QLineEdit::Password);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout layout(&dialog);
    layout.addRow(QStringLiteral("LoTW username:"), &usernameEdit);
    layout.addRow(QStringLiteral("LoTW password:"), &passwordEdit);
    layout.addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString username = usernameEdit.text().trimmed();
    const QString password = passwordEdit.text().trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Settings"),
                             QStringLiteral("Username and password are required."));
        return;
    }

    saveLotwCredentials(username, password);
    statusBar()->showMessage(QStringLiteral("LoTW settings saved"), 5000);
}

void MainWindow::downloadLotwReport(const QString &login,
                                    const QString &password,
                                    const QString &qsoSince,
                                    bool usePost)
{
    QUrl url(QStringLiteral("https://lotw.arrl.org/lotwuser/lotwreport.adi"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("login"), login);
    query.addQueryItem(QStringLiteral("password"), password);
    query.addQueryItem(QStringLiteral("qso_query"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("qso_qsl"), QStringLiteral("no"));
    query.addQueryItem(QStringLiteral("qso_qsldetail"), QStringLiteral("yes"));
    query.addQueryItem(QStringLiteral("qso_startdate"), qsoSince);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "BandPilot");

    QNetworkReply *reply = nullptr;
    if (usePost) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        reply = m_networkManager->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    } else {
        url.setQuery(query);
        request.setUrl(url);
        reply = m_networkManager->get(request);
    }

    statusBar()->showMessage(usePost
                                 ? QStringLiteral("Retrying LoTW report download...")
                                 : QStringLiteral("Downloading LoTW report..."));

    connect(reply, &QNetworkReply::finished, this, [this, reply, login, password, qsoSince, usePost]() {
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            statusBar()->showMessage(QStringLiteral("LoTW download failed"), 5000);
            QMessageBox::warning(this, QStringLiteral("LoTW Import"), errorText);
            return;
        }

        if (!usePost && lotwCredentialsRejected(data)) {
            downloadLotwReport(login, password, qsoSince, true);
            return;
        }

        importLotwData(data);
    });
}

void MainWindow::importFromLotw()
{
    const QString login = lotwUsername().trimmed();
    const QString password = lotwPassword();
    if (login.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"),
                             QStringLiteral("Configure LoTW username and password in Settings first."));
        configureLotwSettings();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Import from LoTW"));

    QLineEdit qsoSinceEdit(QStringLiteral("2026-01-01"), &dialog);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout layout(&dialog);
    layout.addRow(QStringLiteral("QSOs since:"), &qsoSinceEdit);
    layout.addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString qsoSince = qsoSinceEdit.text().trimmed();
    if (!QDate::fromString(qsoSince, Qt::ISODate).isValid()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"),
                             QStringLiteral("QSOs since must use YYYY-MM-DD format."));
        return;
    }

    downloadLotwReport(login, password, qsoSince, false);
}

void MainWindow::importLotwData(const QByteArray &data)
{
    const QString responseError = lotwResponseError(data);
    if (!responseError.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"), responseError);
        if (responseError.contains(QStringLiteral("username or password"), Qt::CaseInsensitive)) {
            configureLotwSettings();
        }
        return;
    }

    const QList<AdifRecord> records = parseAdif(data);
    if (records.isEmpty()) {
        const QString serverMessage = lotwResponseMessage(data);
        QMessageBox::warning(this, QStringLiteral("LoTW Import"),
                             serverMessage.isEmpty()
                                 ? QStringLiteral("LoTW returned an empty response.")
                                 : QStringLiteral("LoTW returned no QSO records:\n\n%1")
                                       .arg(serverMessage));
        return;
    }

    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    if (!database.transaction()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"), database.lastError().text());
        return;
    }

    QSqlQuery duplicateQuery(database);
    duplicateQuery.prepare(QStringLiteral(
        "SELECT id, grid, qsl FROM contacts "
        "WHERE date = :date AND time = :time AND UPPER(call) = UPPER(:call) "
        "AND band = :band AND mode = :mode LIMIT 1"));

    QSqlQuery updateContactQuery(database);
    updateContactQuery.prepare(QStringLiteral("UPDATE contacts SET grid = :grid, qsl = :qsl WHERE id = :id"));

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES "
        "(:date, :time, :call, :band, :frequency, :mode, :submode, :grid, :rst_tx, :rst_rx, :qsl, :comment)"));

    int imported = 0;
    int duplicates = 0;
    int invalid = 0;
    int gridsUpdated = 0;
    int qslUpdated = 0;

    for (const AdifRecord &record : records) {
        const QString date = adifDate(record.value(QStringLiteral("QSO_DATE")));
        QString time = adifTime(record.value(QStringLiteral("TIME_ON")));
        if (time.isEmpty() && record.contains(QStringLiteral("APP_LOTW_DXCC_PROCESSED_DTG"))) {
            time = QStringLiteral("00:00:00");
        }
        const QString call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
        const QString band = displayBand(record.value(QStringLiteral("BAND")));
        QString adifMode = record.value(QStringLiteral("MODE")).trimmed().toUpper();
        if (adifMode.isEmpty()) {
            adifMode = record.value(QStringLiteral("APP_LOTW_MODE")).trimmed().toUpper();
        }
        if (adifMode.isEmpty()) {
            adifMode = record.value(QStringLiteral("APP_LOTW_MODEGROUP")).trimmed().toUpper();
        }
        const QString mode = displayMode(adifMode);

        if (date.isEmpty() || time.isEmpty() || call.isEmpty() || band.isEmpty() || adifMode.isEmpty()) {
            ++invalid;
            continue;
        }

        const QString grid = recordGrid(record);
        const QString qsl = displayQslStatus(record);

        duplicateQuery.bindValue(QStringLiteral(":date"), date);
        duplicateQuery.bindValue(QStringLiteral(":time"), time);
        duplicateQuery.bindValue(QStringLiteral(":call"), call);
        duplicateQuery.bindValue(QStringLiteral(":band"), band);
        duplicateQuery.bindValue(QStringLiteral(":mode"), mode);
        if (!duplicateQuery.exec()) {
            database.rollback();
            QMessageBox::warning(this, QStringLiteral("LoTW Import"), duplicateQuery.lastError().text());
            return;
        }
        if (duplicateQuery.next()) {
            const QString existingGrid = duplicateQuery.value(1).toString().trimmed();
            const QString existingQsl = duplicateQuery.value(2).toString().trimmed();
            const bool shouldUpdateGrid = existingGrid.isEmpty() && !grid.isEmpty();
            const bool shouldUpdateQsl = existingQsl != qsl;
            if (shouldUpdateGrid || shouldUpdateQsl) {
                updateContactQuery.bindValue(QStringLiteral(":grid"),
                                             shouldUpdateGrid ? grid : existingGrid);
                updateContactQuery.bindValue(QStringLiteral(":qsl"), qsl);
                updateContactQuery.bindValue(QStringLiteral(":id"), duplicateQuery.value(0));
                if (!updateContactQuery.exec()) {
                    database.rollback();
                    QMessageBox::warning(this, QStringLiteral("LoTW Import"), updateContactQuery.lastError().text());
                    return;
                }
                if (shouldUpdateGrid) {
                    ++gridsUpdated;
                }
                if (shouldUpdateQsl) {
                    ++qslUpdated;
                }
            }
            ++duplicates;
            continue;
        }

        QString submode = record.value(QStringLiteral("SUBMODE")).trimmed().toUpper();
        if (submode.isEmpty()) {
            submode = record.value(QStringLiteral("APP_LOTW_MODE")).trimmed().toUpper();
        }
        if (submode.isEmpty() && mode != adifMode) {
            submode = adifMode;
        }

        insertQuery.bindValue(QStringLiteral(":date"), date);
        insertQuery.bindValue(QStringLiteral(":time"), time);
        insertQuery.bindValue(QStringLiteral(":call"), call);
        insertQuery.bindValue(QStringLiteral(":band"), band);
        const QString frequency = record.value(QStringLiteral("FREQ")).trimmed();
        insertQuery.bindValue(QStringLiteral(":frequency"),
                              frequency.isEmpty() ? QStringLiteral("") : frequency);
        insertQuery.bindValue(QStringLiteral(":mode"), mode);
        insertQuery.bindValue(QStringLiteral(":submode"), submode);
        insertQuery.bindValue(QStringLiteral(":grid"), grid);
        insertQuery.bindValue(QStringLiteral(":rst_tx"), record.value(QStringLiteral("RST_SENT")).trimmed());
        insertQuery.bindValue(QStringLiteral(":rst_rx"), record.value(QStringLiteral("RST_RCVD")).trimmed());
        insertQuery.bindValue(QStringLiteral(":qsl"), qsl);
        insertQuery.bindValue(QStringLiteral(":comment"), record.value(QStringLiteral("COMMENT")).trimmed());

        if (!insertQuery.exec()) {
            database.rollback();
            QMessageBox::warning(this, QStringLiteral("LoTW Import"), insertQuery.lastError().text());
            return;
        }
        ++imported;
    }

    if (!database.commit()) {
        database.rollback();
        QMessageBox::warning(this, QStringLiteral("LoTW Import"), database.lastError().text());
        return;
    }

    m_model->select();
    m_tableView->resizeColumnsToContents();
    const QString statusSummary = QStringLiteral("LoTW import: %1 imported, %2 duplicates, %3 grids updated, "
                                                 "%4 QSL statuses updated, %5 invalid")
                                      .arg(imported)
                                      .arg(duplicates)
                                      .arg(gridsUpdated)
                                      .arg(qslUpdated)
                                      .arg(invalid);
    statusBar()->showMessage(statusSummary);
}

void MainWindow::setupModel()
{
    m_model = new QSqlTableModel(this, QSqlDatabase::database(kConnectionName));
    m_model->setTable(kTableName);
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->setSort(m_model->fieldIndex("date"), Qt::AscendingOrder);
    m_model->select();

    m_model->setHeaderData(m_model->fieldIndex("date"), Qt::Horizontal, "Date");
    m_model->setHeaderData(m_model->fieldIndex("time"), Qt::Horizontal, "Time");
    m_model->setHeaderData(m_model->fieldIndex("call"), Qt::Horizontal, "Call");
    m_model->setHeaderData(m_model->fieldIndex("band"), Qt::Horizontal, "Band");
    m_model->setHeaderData(m_model->fieldIndex("frequency"), Qt::Horizontal, "Frequency");
    m_model->setHeaderData(m_model->fieldIndex("mode"), Qt::Horizontal, "Mode");
    m_model->setHeaderData(m_model->fieldIndex("submode"), Qt::Horizontal, "Submode");
    m_model->setHeaderData(m_model->fieldIndex("grid"), Qt::Horizontal, "Grid");
    m_model->setHeaderData(m_model->fieldIndex("rst_tx"), Qt::Horizontal, "RST TX");
    m_model->setHeaderData(m_model->fieldIndex("rst_rx"), Qt::Horizontal, "RST RX");
    m_model->setHeaderData(m_model->fieldIndex("qsl"), Qt::Horizontal, "QSL");
    m_model->setHeaderData(m_model->fieldIndex("comment"), Qt::Horizontal, "Comment");
}

void MainWindow::setupUi()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QMenu *importMenu = fileMenu->addMenu(QStringLiteral("&Import from"));
    QAction *lotwAction = importMenu->addAction(QStringLiteral("&LoTW..."));
    connect(lotwAction, &QAction::triggered, this, &MainWindow::importFromLotw);

    QMenu *databaseMenu = menuBar()->addMenu(QStringLiteral("&Database"));
    QAction *clearAllAction = databaseMenu->addAction(QStringLiteral("&Clear All"));
    connect(clearAllAction, &QAction::triggered, this, &MainWindow::clearAllContacts);

    QMenu *settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));
    QAction *lotwSettingsAction = settingsMenu->addAction(QStringLiteral("&LoTW..."));
    connect(lotwSettingsAction, &QAction::triggered, this, &MainWindow::configureLotwSettings);

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setSortingEnabled(true);
    m_tableView->installEventFilter(this);
    m_tableView->hideColumn(m_model->fieldIndex("id"));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->resizeColumnsToContents();

    setCentralWidget(m_tableView);
    statusBar()->showMessage("Database ready");
}
