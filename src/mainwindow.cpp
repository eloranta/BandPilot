#include "mainwindow.h"
#include "udpreceiver.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDate>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
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
#include <QSqlError>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QTableView>
#include <QUrlQuery>
#include <QVariant>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("BandPilot"));
    resize(760, 420);

    QString errorMessage;
    if (!m_database.initialize(&errorMessage)) {
        QMessageBox::critical(this,
                              QStringLiteral("Database Error"),
                              QStringLiteral("Could not initialize the BandPilot database."));
        if (!errorMessage.isEmpty()) {
            statusBar()->showMessage(errorMessage);
        }
    }

    setupModel();
    setupUi();

    m_networkManager = new QNetworkAccessManager(this);
    m_udpReceiver = new UdpReceiver(this);
    connect(m_udpReceiver, &UdpReceiver::loggedContactReceived, this, [this](const Contact &contact) {
        if (addLoggedContact(contact)) {
            statusBar()->showMessage(QStringLiteral("Logged UDP contact: %1").arg(contact.call), 5000);
        }
    });

    if (!m_udpReceiver->start()) {
        statusBar()->showMessage(QStringLiteral("UDP listener failed"));
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

bool MainWindow::addLoggedContact(const Contact &contact)
{
    QVariant insertedId;
    QString errorMessage;
    if (!m_database.addContact(contact, &insertedId, &errorMessage)) {
        qWarning().noquote()
            << "UDP contact insert failed"
            << errorMessage
            << "date=" << contact.date
            << "time=" << contact.time
            << "call=" << contact.call
            << "band=" << contact.band
            << "frequency=" << contact.frequency
            << "mode=" << contact.mode
            << "submode=" << contact.submode;
        statusBar()->showMessage(errorMessage, 5000);
        return false;
    }

    qDebug().noquote()
        << "UDP contact inserted"
        << "id=" << insertedId.toString()
        << "call=" << contact.call;

    refreshTable();
    selectContactById(insertedId);
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
    refreshTable();
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

    int deletedCount = 0;
    QString errorMessage;
    if (!m_database.clearAllContacts(&deletedCount, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("Clear Database"), errorMessage);
        return;
    }

    refreshTable();
    statusBar()->showMessage(QStringLiteral("Cleared %1 contact%2")
                                 .arg(deletedCount)
                                 .arg(deletedCount == 1 ? QString() : QStringLiteral("s")),
                             5000);
}

void MainWindow::configureLotwSettings()
{
    const LotwCredentials credentials = m_lotwCredentials.load();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("LoTW Settings"));

    QLineEdit usernameEdit(credentials.username, &dialog);
    QLineEdit passwordEdit(credentials.password, &dialog);
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

    m_lotwCredentials.save(username, password);
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

        if (!usePost && m_lotwImporter.credentialsRejected(data)) {
            downloadLotwReport(login, password, qsoSince, true);
            return;
        }

        importLotwData(data);
    });
}

void MainWindow::importFromLotw()
{
    const LotwCredentials credentials = m_lotwCredentials.load();
    const QString login = credentials.username.trimmed();
    const QString password = credentials.password;
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
    const LotwParseResult parseResult = m_lotwImporter.parseReport(data);
    if (!parseResult.errorMessage.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"), parseResult.errorMessage);
        if (parseResult.credentialsRejected) {
            configureLotwSettings();
        }
        return;
    }

    ContactImportSummary importSummary;
    QString errorMessage;
    if (!m_database.importContacts(parseResult.contacts, &importSummary, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("LoTW Import"), errorMessage);
        return;
    }

    refreshTable();
    statusBar()->showMessage(QStringLiteral("LoTW import: %1 imported, %2 duplicates, %3 grids updated, "
                                            "%4 QSL statuses updated, %5 invalid")
                                 .arg(importSummary.imported)
                                 .arg(importSummary.duplicates)
                                 .arg(importSummary.gridsUpdated)
                                 .arg(importSummary.qslUpdated)
                                 .arg(parseResult.invalid));
}

void MainWindow::setupModel()
{
    m_model = m_database.createModel(this);
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
    m_tableView->hideColumn(m_model->fieldIndex(QStringLiteral("id")));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->resizeColumnsToContents();

    setCentralWidget(m_tableView);
    statusBar()->showMessage(QStringLiteral("Database ready"));
}

void MainWindow::refreshTable()
{
    if (!m_model->select()) {
        qWarning().noquote() << "Contact model refresh failed" << m_model->lastError().text();
        statusBar()->showMessage(m_model->lastError().text(), 5000);
        return;
    }

    m_tableView->resizeColumnsToContents();
}

void MainWindow::selectContactById(const QVariant &id)
{
    if (!id.isValid()) {
        return;
    }

    const int idColumn = m_model->fieldIndex(QStringLiteral("id"));
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (m_model->index(row, idColumn).data().toLongLong() == id.toLongLong()) {
            m_tableView->selectRow(row);
            m_tableView->scrollTo(m_model->index(row, 0), QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}
