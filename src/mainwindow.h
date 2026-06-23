#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QSqlTableModel;
class QTableView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    bool initializeDatabase();
    bool seedDatabase();
    void setupModel();
    void setupUi();

    QSqlTableModel *m_model = nullptr;
    QTableView *m_tableView = nullptr;
};

#endif // MAINWINDOW_H
