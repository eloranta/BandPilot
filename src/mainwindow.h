#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "contactdatabase.h"
#include "lotwcredentials.h"
#include "lotwimporter.h"

#include <QByteArray>
#include <QMainWindow>

class QNetworkAccessManager;
class QSqlTableModel;
class QTableView;
class QVariant;
class UdpReceiver;
struct Contact;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool addLoggedContact(const Contact &contact);
    bool deleteSelectedContacts();
    void clearAllContacts();
    void configureLotwSettings();
    void downloadLotwReport(const QString &login, const QString &password, const QString &qsoSince, bool usePost);
    void importFromLotw();
    void importLotwData(const QByteArray &data);
    void setupModel();
    void setupUi();
    void refreshTable();
    void selectContactById(const QVariant &id);

    QSqlTableModel *m_model = nullptr;
    QTableView *m_tableView = nullptr;
    UdpReceiver *m_udpReceiver = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    ContactDatabase m_database;
    LotwCredentialStore m_lotwCredentials;
    LotwImporter m_lotwImporter;
};

#endif // MAINWINDOW_H
