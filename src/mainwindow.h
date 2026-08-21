#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QTableView;
class QSqlRelationalTableModel;
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

// Top-level application window: menu bar + a QTableView bound to the
// "contacts" table + a status bar used to report database state. Wires the
// UI layer to the business/data layer (Database + QSqlRelationalTableModel).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void setupUi();
    void setupMenuBar();
    void loadDatabase();
    void importAdif();
    void importLotw();
    void handleLotwReply(QNetworkReply *reply);

    QTableView *m_tableView = nullptr;
    QSqlRelationalTableModel *m_model = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
};
