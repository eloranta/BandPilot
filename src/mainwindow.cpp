#include "mainwindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <iterator>

namespace {

struct BandInfo
{
    const char *name;
    const char *range;
    const char *note;
};

const BandInfo kBands[] = {
    {"160 m", "1.800 - 2.000 MHz", "Best after dark; watch local noise."},
    {"80 m", "3.500 - 4.000 MHz", "Regional night-time coverage."},
    {"40 m", "7.000 - 7.300 MHz", "Reliable day and night workhorse."},
    {"30 m", "10.100 - 10.150 MHz", "CW and digital activity."},
    {"20 m", "14.000 - 14.350 MHz", "Primary daytime DX band."},
    {"17 m", "18.068 - 18.168 MHz", "Often opens when 20 m is crowded."},
    {"15 m", "21.000 - 21.450 MHz", "Good daylight DX when conditions support it."},
    {"12 m", "24.890 - 24.990 MHz", "Solar-cycle dependent DX."},
    {"10 m", "28.000 - 29.700 MHz", "Long-distance openings can be strong and brief."},
    {"6 m", "50.000 - 54.000 MHz", "Monitor for sporadic-E and weak-signal openings."}
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    populateBands();
    updateBandDetails();
}

void MainWindow::buildUi()
{
    setWindowTitle("BandPilot");
    resize(900, 560);

    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    auto *topLayout = new QHBoxLayout;

    auto *stationGroup = new QGroupBox("Station", central);
    auto *stationForm = new QFormLayout(stationGroup);

    m_bandCombo = new QComboBox(stationGroup);
    m_modeCombo = new QComboBox(stationGroup);
    m_modeCombo->addItems({"CW", "SSB", "FT8", "FT4", "RTTY", "FM"});

    m_powerSpin = new QSpinBox(stationGroup);
    m_powerSpin->setRange(1, 1500);
    m_powerSpin->setValue(100);
    m_powerSpin->setSuffix(" W");

    m_frequencyLabel = new QLabel(stationGroup);
    m_statusLabel = new QLabel(stationGroup);
    m_statusLabel->setWordWrap(true);

    stationForm->addRow("Band", m_bandCombo);
    stationForm->addRow("Mode", m_modeCombo);
    stationForm->addRow("Power", m_powerSpin);
    stationForm->addRow("Range", m_frequencyLabel);
    stationForm->addRow("Hint", m_statusLabel);

    auto *planGroup = new QGroupBox("Operating Plan", central);
    auto *planLayout = new QVBoxLayout(planGroup);
    m_planList = new QListWidget(planGroup);
    m_planList->addItems({
        "Check antenna match",
        "Listen before transmitting",
        "Call CQ or answer a strong station",
        "Log the contact and signal report"
    });
    planLayout->addWidget(m_planList);

    topLayout->addWidget(stationGroup, 1);
    topLayout->addWidget(planGroup, 1);

    auto *logGroup = new QGroupBox("Session Log", central);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(logGroup);
    m_logEdit->setReadOnly(true);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    m_addButton = new QPushButton("Add Entry", logGroup);
    m_clearButton = new QPushButton("Clear", logGroup);
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_clearButton);

    logLayout->addWidget(m_logEdit);
    logLayout->addLayout(buttonLayout);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(logGroup, 1);
    setCentralWidget(central);

    statusBar()->showMessage("Ready");

    connect(m_bandCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateBandDetails);
    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::addLogEntry);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearLog);
}

void MainWindow::populateBands()
{
    for (const BandInfo &band : kBands) {
        m_bandCombo->addItem(band.name, QString::fromLatin1(band.range));
    }
}

void MainWindow::updateBandDetails()
{
    const int index = m_bandCombo->currentIndex();
    if (index < 0 || index >= static_cast<int>(std::size(kBands))) {
        return;
    }

    const BandInfo &band = kBands[index];
    m_frequencyLabel->setText(QString::fromLatin1(band.range));
    m_statusLabel->setText(QString::fromLatin1(band.note));
    statusBar()->showMessage(QString("Selected %1").arg(QString::fromLatin1(band.name)));
}

void MainWindow::addLogEntry()
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    const QString entry = QString("[%1] %2 %3 at %4")
                              .arg(timestamp,
                                   m_bandCombo->currentText(),
                                   m_modeCombo->currentText(),
                                   m_powerSpin->text());
    m_logEdit->append(entry);
    statusBar()->showMessage("Entry added", 3000);
}

void MainWindow::clearLog()
{
    m_logEdit->clear();
    statusBar()->showMessage("Log cleared", 3000);
}
