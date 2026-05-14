#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include "qcustomplot.h"
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectButtonClicked();
    void updateSerialPortList();

    // Serial 處理
    void handleSerialReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

    // 測量流程
    void on_buttonStartMeasure_clicked();
    void on_buttonStopMeasure_clicked();
    void handleMeasureTimeout();

    // 校正
    void on_Calibrate_clicked();

    // 定時更新圖表用的 slot
    void updatePlot();

    // [新增] CSV 匯出
    void onSaveCsvClicked();

    // [新增] 倒數計時 tick
    void onCountdownTick();

private:
    Ui::MainWindow *ui;
    QSerialPort serial;
    QByteArray readBuffer;

    // Timer
    QTimer *measureTimer;    // 控制測量總時間
    QTimer *plotTimer;       // 控制圖表 / Label 刷新頻率
    QTimer *countdownTimer;  // [新增] 每秒倒數
    int     countdownSecondsLeft = 0;

    // 圖表物件指標
    QCustomPlot *plotVoltage;
    QCustomPlot *plotAccel;
    QCPGraph *gV;
    QCPGraph *gAx;
    QCPGraph *gAy;
    QCPGraph *gAz;

    // 參數與狀態
    bool measuring = false;
    double maxVoltage = 0.0;
    double maxAccelMag = 0.0;
    double sampleIndex = 0;
    const int maxPoints = 500; // X軸視窗大小(顯示最近幾點)

    // 校正變數
    bool calibrating = false;
    bool calibValid = false;
    int calibSamplesCount = 0;
    const int calibSamplesTarget = 50; // 校正取樣數
    double calibSumX=0, calibSumY=0, calibSumZ=0;
    double calibOffsetX=0, calibOffsetY=0, calibOffsetZ=0;

    // [新增] 統計用 (Min / Avg / RMS)
    double minVoltage = 0.0;
    double sumVoltage = 0.0;
    double sumVoltageSq = 0.0;
    double minAccelMag = 0.0;
    double sumAccelMag = 0.0;
    double sumAccelMagSq = 0.0;
    int    measureSampleCount = 0;

    // [新增] 最新即時值快取，由 plotTimer 統一刷新 Label
    double latestV = 0.0;
    double latestAccelMag = 0.0;
    bool   hasNewData = false;

    // [新增] CSV 紀錄 buffer
    QVector<double> recT, recV, recAx, recAy, recAz;

    // [新增] terminal 批次輸出 buffer
    QString pendingTerminalText;

    // [新增] 程式建立的 UI 元件
    QPushButton *buttonSaveCsv = nullptr;
    QLabel      *labelCountdown = nullptr;

    // 輔助函式
    void setupBaudRateCombo();
    void populatePortList();
    QString currentPortName() const;
    void appendData(double v, double ax, double ay, double az);
    void resetStats();
    void buildExtraUi();
    void refreshStatLabels();

    QMessageBox *calibWaitBox = nullptr;
    QVector<double> recordedAccels;
};
#endif // MAINWINDOW_H
