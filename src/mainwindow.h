#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QTableView;
class QTableWidget;
class QTabWidget;
class QSqlRelationalTableModel;
class QNetworkAccessManager;
class QNetworkReply;
class QLabel;
QT_END_NAMESPACE

// Top-level application window: menu bar + a tab widget (the "contacts"
// QTableView, and a DXCC Challenge summary grid) + a status bar used to
// report database state. Wires the UI layer to the business/data layer
// (Database + QSqlRelationalTableModel).
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
    void showTableContextMenu(const QPoint &pos);
    void clearAllContacts();
    void updateStats();

    QTabWidget *m_tabWidget = nullptr;
    QTableView *m_tableView = nullptr;
    QTableWidget *m_dxccChallengeTable = nullptr;
    QSqlRelationalTableModel *m_model = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QLabel *m_statsLabel = nullptr;
};
