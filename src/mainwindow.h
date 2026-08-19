#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QTableView;
class QSqlTableModel;
QT_END_NAMESPACE

// Top-level application window: menu bar + a QTableView bound to the
// "contacts" table + a status bar used to report database state. Wires the
// UI layer to the business/data layer (Database + QSqlTableModel).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void setupUi();
    void setupMenuBar();
    void loadDatabase();

    QTableView *m_tableView = nullptr;
    QSqlTableModel *m_model = nullptr;
};
