#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QMainWindow>

class QNetworkAccessManager;
class QSqlTableModel;
class QTableView;
class UdpReceiver;
struct UdpLoggedContact;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool initializeDatabase();
    bool addLoggedContact(const UdpLoggedContact &contact);
    bool deleteSelectedContacts();
    void importFromLotw();
    void importLotwData(const QByteArray &data);
    void setupModel();
    void setupUi();

    QSqlTableModel *m_model = nullptr;
    QTableView *m_tableView = nullptr;
    UdpReceiver *m_udpReceiver = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
};

#endif // MAINWINDOW_H
