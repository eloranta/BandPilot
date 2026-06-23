#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void updateBandDetails();
    void addLogEntry();
    void clearLog();

private:
    void buildUi();
    void populateBands();

    QComboBox *m_bandCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QSpinBox *m_powerSpin = nullptr;
    QLabel *m_frequencyLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_planList = nullptr;
    QTextEdit *m_logEdit = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_clearButton = nullptr;
};

#endif // MAINWINDOW_H
