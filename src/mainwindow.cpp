#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlRelationalDelegate>
#include <QSqlRelationalTableModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "database.h"
#include "version.h"

namespace {

// LoTW's own error responses (e.g. bad login/password) come back as an HTML
// page with HTTP 200, not a network error -- so a zero-record import isn't
// itself distinguishable from "no new QSOs" without looking at the body.
// Strips tags down to a short plain-text summary suitable for a status
// message.
QString lotwResponseMessage(const QByteArray &data)
{
    QString message = QString::fromUtf8(data);
    message.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    message = message.simplified();
    if (message.size() > 200)
        message = message.left(200) + QStringLiteral("...");
    return message;
}

// XORs against a fixed, non-secret key. This is obfuscation, not
// encryption -- it only stops the LoTW password from sitting in
// QSettings' backing store (an .ini file or registry key) as plain,
// human-readable text; anyone with access to that store and this source
// file can trivially recover it. Self-inverse, so the same function
// applies and reverses the XOR.
QByteArray xorObfuscate(QByteArray data)
{
    static const char kKey[] = "BandPilot-LoTW-settings-obfuscation";
    constexpr int keyLen = sizeof(kKey) - 1;
    for (int i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ kKey[i % keyLen];
    return data;
}

QString obfuscatePassword(const QString &password)
{
    return QString::fromLatin1(xorObfuscate(password.toUtf8()).toBase64());
}

QString deobfuscatePassword(const QString &stored)
{
    return QString::fromUtf8(xorObfuscate(QByteArray::fromBase64(stored.toLatin1())));
}

const char *const kSettingsLotwLogin = "LoTW/Login";
const char *const kSettingsLotwPassword = "LoTW/Password";

// DXCC tab mode columns, in LoTW's own display order (Mix, Ph, CW, RT,
// SAT) -- rowIndex is the matching index into
// Database::dxccChallengeRowNames() (Mixed, CW, Phone, Digital,
// Satellite), which uses a different order internally.
struct DxccModeColumn
{
    const char *label;
    int rowIndex;
};
const DxccModeColumn kDxccModeColumns[] = {
    {"Mix", 0}, {"Ph", 2}, {"CW", 1}, {"RT", 3}, {"SAT", 4},
};
constexpr int kDxccModeColumnCount = 5;
constexpr int kDxccInfoColumnCount = 2; // Prefix, Entity

// Short band column headers ("160", not "160M"), matching
// Database::dxccChallengeBandNames() 1:1 by position.
QStringList dxccBandColumnLabels()
{
    return {QStringLiteral("160"), QStringLiteral("80"), QStringLiteral("40"), QStringLiteral("30"),
            QStringLiteral("20"),  QStringLiteral("17"), QStringLiteral("15"), QStringLiteral("12"),
            QStringLiteral("10"),  QStringLiteral("6"),  QStringLiteral("2"),  QStringLiteral("70CM")};
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("BandPilot %1").arg(QStringLiteral(BANDPILOT_VERSION)));
    resize(1000, 600);

    m_networkManager = new QNetworkAccessManager(this);

    setupUi();
    setupMenuBar();
    loadDatabase();
}

void MainWindow::setupUi()
{
    m_tableView = new QTableView(this);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this,
            &MainWindow::showTableContextMenu);

    // One row per current DXCC entity (worked or not), one column per
    // single-mode and single-band award -- mirrors LoTW's own per-entity
    // "countries confirmed" checklist. Column order matches that report's:
    // Prefix, Entity, then mode columns (Mix/Ph/CW/RT/SAT), then bands.
    QStringList dxccHeaders;
    dxccHeaders << tr("Prefix") << tr("Entity");
    for (const DxccModeColumn &col : kDxccModeColumns)
        dxccHeaders << tr(col.label);
    dxccHeaders << dxccBandColumnLabels();

    m_dxccChallengeTable = new QTableWidget(0, dxccHeaders.size(), this);
    m_dxccChallengeTable->setHorizontalHeaderLabels(dxccHeaders);
    m_dxccChallengeTable->verticalHeader()->setVisible(false);
    m_dxccChallengeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dxccChallengeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dxccChallengeTable->setAlternatingRowColors(true);
    m_dxccChallengeTable->setSortingEnabled(true);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_tableView, tr("Contacts"));
    m_tabWidget->addTab(m_dxccChallengeTable, tr("DXCC"));
    setCentralWidget(m_tabWidget);

    m_statsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statsLabel);

    statusBar()->showMessage(tr("Starting up..."));
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QMenu *importMenu = fileMenu->addMenu(tr("&Import"));
    QAction *importAdifAction = importMenu->addAction(tr("&Adif..."));
    connect(importAdifAction, &QAction::triggered, this, &MainWindow::importAdif);

    QAction *importLotwAction = importMenu->addAction(tr("&LoTW..."));
    connect(importLotwAction, &QAction::triggered, this, &MainWindow::importLotw);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(
            tr("BandPilot %1 — ham radio QSO logger").arg(QStringLiteral(BANDPILOT_VERSION)), 5000);
    });
}

void MainWindow::loadDatabase()
{
    QString errorMessage;
    if (!Database::initialize(&errorMessage)) {
        statusBar()->showMessage(tr("Database error: %1").arg(errorMessage));
        return;
    }

    m_model = new QSqlRelationalTableModel(this, QSqlDatabase::database(Database::connectionName()));
    m_model->setTable(QStringLiteral("contacts"));

    // fieldIndex("dxcc_entity") stops resolving once the relation below is active and
    // select() has run (the model renames that field after the related table's display
    // column), so capture its position now while the name still resolves.
    const int dxccEntityColumn = m_model->fieldIndex(QStringLiteral("dxcc_entity"));

    m_model->setRelation(dxccEntityColumn,
                          QSqlRelation(QStringLiteral("dxcc_entity"), QStringLiteral("entity_code"),
                                       QStringLiteral("entity")));
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->select();

    const auto setHeader = [this](const char *field, const QString &label) {
        m_model->setHeaderData(m_model->fieldIndex(QString::fromLatin1(field)), Qt::Horizontal, label);
    };
    setHeader("id", tr("ID"));
    setHeader("date", tr("Date"));
    setHeader("time", tr("Time"));
    setHeader("call", tr("Call"));
    setHeader("band", tr("Band"));
    setHeader("frequency", tr("Frequency"));
    setHeader("mode", tr("Mode"));
    setHeader("submode", tr("Submode"));
    setHeader("deleted_entity", tr("Deleted Entity"));
    setHeader("grid", tr("Grid"));
    setHeader("rst_tx", tr("RST Tx"));
    setHeader("rst_rx", tr("RST Rx"));
    setHeader("qsl", tr("QSL"));
    setHeader("comment", tr("Comment"));
    m_model->setHeaderData(dxccEntityColumn, Qt::Horizontal, tr("DXCC Entity"));

    m_tableView->setModel(m_model);
    m_tableView->setItemDelegate(new QSqlRelationalDelegate(m_tableView));
    m_tableView->resizeColumnsToContents();

    // Keep the stats label current for in-place edits too (e.g. flipping a
    // row's QSL status by hand), not just bulk import/clear actions.
    connect(m_model, &QSqlRelationalTableModel::dataChanged, this, &MainWindow::updateStats);

    statusBar()->showMessage(
        tr("Database ready: %1 (%2 contacts)")
            .arg(Database::databaseFilePath())
            .arg(m_model->rowCount()));
    updateStats();
}

void MainWindow::importAdif()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Import ADIF Log"), QString(), tr("ADIF files (*.adi *.adif);;All files (*)"));
    if (filePath.isEmpty())
        return;

    QString errorMessage;
    const int imported = Database::importAdif(filePath, &errorMessage);
    if (imported < 0) {
        statusBar()->showMessage(tr("ADIF import failed: %1").arg(errorMessage));
        return;
    }

    m_model->select();
    updateStats();
    statusBar()->showMessage(tr("Imported %1 contact(s) from %2").arg(imported).arg(filePath));
}

void MainWindow::importLotw()
{
    QSettings settings;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import from LoTW"));

    auto *loginEdit = new QLineEdit(settings.value(QString::fromLatin1(kSettingsLotwLogin)).toString(), &dialog);
    auto *passwordEdit = new QLineEdit(
        deobfuscatePassword(settings.value(QString::fromLatin1(kSettingsLotwPassword)).toString()), &dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    auto *qslSinceEdit = new QLineEdit(QStringLiteral("2000-01-01"), &dialog);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QFormLayout(&dialog);
    layout->addRow(tr("Login:"), loginEdit);
    layout->addRow(tr("Password:"), passwordEdit);
    layout->addRow(tr("QSLs since:"), qslSinceEdit);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString login = loginEdit->text().trimmed();
    const QString password = passwordEdit->text();
    const QString qslSince = qslSinceEdit->text().trimmed();
    if (login.isEmpty() || password.isEmpty()) {
        statusBar()->showMessage(tr("LoTW login and password are required."), 8000);
        return;
    }
    if (!QDate::fromString(qslSince, Qt::ISODate).isValid()) {
        statusBar()->showMessage(tr("\"QSLs since\" must use YYYY-MM-DD format."), 8000);
        return;
    }

    settings.setValue(QString::fromLatin1(kSettingsLotwLogin), login);
    settings.setValue(QString::fromLatin1(kSettingsLotwPassword), obfuscatePassword(password));

    QNetworkRequest request(Database::lotwReportUrl(login, password, qslSince));
    // The request URL carries the LoTW password as a query parameter;
    // refuse to let a redirect quietly downgrade it to plain HTTP.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_networkManager->get(request);
    statusBar()->showMessage(tr("Downloading LoTW report..."));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleLotwReply(reply); });
}

void MainWindow::handleLotwReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        qWarning() << "LoTW download failed:" << errorText;
        statusBar()->showMessage(tr("LoTW download failed: %1").arg(errorText), 8000);
        return;
    }

    Database::AdifImportResult result;
    QString errorMessage;
    if (!Database::importLotwAdif(data, &result, &errorMessage)) {
        qWarning() << "LoTW import failed:" << errorMessage;
        statusBar()->showMessage(tr("LoTW import failed: %1").arg(errorMessage), 8000);
        return;
    }

    if (result.imported == 0 && result.duplicates == 0 && result.invalid == 0) {
        const QString serverMessage = lotwResponseMessage(data);
        qWarning() << "LoTW returned no QSO records:"
                    << (serverMessage.isEmpty() ? QStringLiteral("(empty response)") : serverMessage);
        statusBar()->showMessage(serverMessage.isEmpty()
                                      ? tr("LoTW returned an empty response.")
                                      : tr("LoTW returned no QSO records: %1").arg(serverMessage),
                                  8000);
        return;
    }

    m_model->select();
    updateStats();
    statusBar()->showMessage(tr("LoTW import: %1 new, %2 already logged, %3 skipped")
                                  .arg(result.imported)
                                  .arg(result.duplicates)
                                  .arg(result.invalid),
                              8000);
}

void MainWindow::showTableContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *clearAllAction = menu.addAction(tr("Clear All..."));
    connect(clearAllAction, &QAction::triggered, this, &MainWindow::clearAllContacts);
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void MainWindow::clearAllContacts()
{
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Clear Database"),
        tr("This will permanently delete all contacts from the database.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    QString errorMessage;
    const int deleted = Database::clearAllContacts(&errorMessage);
    if (deleted < 0) {
        statusBar()->showMessage(tr("Clear database failed: %1").arg(errorMessage), 8000);
        return;
    }

    m_model->select();
    m_tableView->resizeColumnsToContents();
    updateStats();
    statusBar()->showMessage(tr("Cleared %1 contact(s)").arg(deleted), 5000);
}

void MainWindow::updateStats()
{
    Database::ContactStats stats;
    QString statsError;
    if (!Database::contactStats(&stats, &statsError)) {
        qWarning() << "Failed to compute contact stats:" << statsError;
    } else {
        m_statsLabel->setText(tr("%1 QSOs · %2 DXCC confirmed / %3 worked")
                                   .arg(stats.totalContacts)
                                   .arg(stats.entitiesConfirmed)
                                   .arg(stats.entitiesWorked));
    }

    QVector<Database::DxccEntityProgress> progress;
    QString progressError;
    if (!Database::dxccEntityProgress(&progress, &progressError)) {
        qWarning() << "Failed to compute DXCC entity progress:" << progressError;
        return;
    }

    m_dxccChallengeTable->setSortingEnabled(false);
    m_dxccChallengeTable->setRowCount(progress.size());

    for (int row = 0; row < progress.size(); ++row) {
        const Database::DxccEntityProgress &entity = progress.at(row);

        m_dxccChallengeTable->setItem(row, 0, new QTableWidgetItem(entity.prefix));
        m_dxccChallengeTable->setItem(row, 1, new QTableWidgetItem(entity.entityName));

        for (int col = 0; col < kDxccModeColumnCount; ++col) {
            const bool confirmed = entity.modeConfirmed[kDxccModeColumns[col].rowIndex];
            auto *item = new QTableWidgetItem(confirmed ? QStringLiteral("X") : QString());
            item->setTextAlignment(Qt::AlignCenter);
            m_dxccChallengeTable->setItem(row, kDxccInfoColumnCount + col, item);
        }
        for (int col = 0; col < Database::kDxccChallengeBandCount; ++col) {
            auto *item = new QTableWidgetItem(entity.bandConfirmed[col] ? QStringLiteral("X") : QString());
            item->setTextAlignment(Qt::AlignCenter);
            m_dxccChallengeTable->setItem(row, kDxccInfoColumnCount + kDxccModeColumnCount + col, item);
        }
    }

    m_dxccChallengeTable->setSortingEnabled(true);
    m_dxccChallengeTable->resizeColumnsToContents();
}
