#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"

#include <cmath>
#include <limits>
#include <QSerialPortInfo>
#include <QTimer>
#include <QDebug>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // --- 1. 測量計時器 ---
    measureTimer = new QTimer(this);
    measureTimer->setSingleShot(true);
    connect(measureTimer, &QTimer::timeout,
            this, &MainWindow::handleMeasureTimeout);

    // --- 2. 繪圖計時器 (30ms 一次 ≒ 33 FPS) ---
    // 把 Replot / Label 更新從 Serial 接收端分離，避免高速資料時 UI 卡頓
    plotTimer = new QTimer(this);
    connect(plotTimer, &QTimer::timeout, this, &MainWindow::updatePlot);
    plotTimer->start(30);

    // --- 3. 倒數計時 (每秒一次) ---
    countdownTimer = new QTimer(this);
    connect(countdownTimer, &QTimer::timeout, this, &MainWindow::onCountdownTick);

    setupBaudRateCombo();
    populatePortList();

    // Port 列表自動更新
    auto *portTimer = new QTimer(this);
    connect(portTimer, &QTimer::timeout,
            this, &MainWindow::updateSerialPortList);
    portTimer->start(1000);

    connect(&serial, &QSerialPort::readyRead,
            this, &MainWindow::handleSerialReadyRead);
    connect(&serial, &QSerialPort::errorOccurred,
            this, &MainWindow::handleSerialError);

    connect(ui->buttonConnect, &QPushButton::clicked,
            this, &MainWindow::onConnectButtonClicked);

    // Terminal 設定
    ui->terminalView->setReadOnly(true);
    ui->terminalView->document()->setMaximumBlockCount(1000);

    // ====== QCustomPlot 設定 ======
    auto *layout = new QVBoxLayout(ui->plotWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    // 電壓圖
    plotVoltage = new QCustomPlot(ui->plotWidget);
    layout->addWidget(plotVoltage);

    gV = plotVoltage->addGraph();
    QPen vPen(QColor("#e53935"));
    vPen.setWidthF(1.6);
    gV->setPen(vPen);
    gV->setName("Voltage");

    plotVoltage->legend->setVisible(true);
    plotVoltage->legend->setBrush(QBrush(QColor(255, 255, 255, 200)));
    plotVoltage->xAxis->setLabel("Sample");
    plotVoltage->yAxis->setLabel("Voltage (V)");
    plotVoltage->xAxis->grid()->setSubGridVisible(true);
    plotVoltage->yAxis->grid()->setSubGridVisible(true);
    plotVoltage->setNoAntialiasingOnDrag(true);

    // 加速度圖
    plotAccel = new QCustomPlot(ui->plotWidget);
    layout->addWidget(plotAccel);

    gAx = plotAccel->addGraph();
    gAy = plotAccel->addGraph();
    gAz = plotAccel->addGraph();

    QPen pX(QColor("#43a047")); pX.setWidthF(1.4); gAx->setPen(pX);
    QPen pY(QColor("#1e88e5")); pY.setWidthF(1.4); gAy->setPen(pY);
    QPen pZ(QColor("#8e24aa")); pZ.setWidthF(1.4); gAz->setPen(pZ);

    gAx->setName("Accel X");
    gAy->setName("Accel Y");
    gAz->setName("Accel Z");

    plotAccel->legend->setVisible(true);
    plotAccel->legend->setBrush(QBrush(QColor(255, 255, 255, 200)));
    plotAccel->xAxis->setLabel("Sample");
    plotAccel->yAxis->setLabel("Acceleration (m/s²)");
    plotAccel->xAxis->grid()->setSubGridVisible(true);
    plotAccel->yAxis->grid()->setSubGridVisible(true);
    plotAccel->setNoAntialiasingOnDrag(true);

    // 字體與顏色設定
    QFont bigBold = ui->labelCurrentVoltage->font();
    bigBold.setPointSize(20);
    bigBold.setBold(true);

    ui->labelCurrentVoltage->setFont(bigBold);
    ui->labelCurrentAccelMag->setFont(bigBold);

    // Max 標籤改用稍小一點的字，騰出空間擺第二行 (Min / Avg / RMS)
    QFont mediumBold = bigBold;
    mediumBold.setPointSize(16);
    ui->labelMaxVoltage->setFont(mediumBold);
    ui->labelMaxAccelMag->setFont(mediumBold);

    ui->labelCurrentVoltage->setStyleSheet("color: #FF66BD;");
    ui->labelCurrentAccelMag->setStyleSheet("color: #FF66BD;");
    ui->labelMaxVoltage->setStyleSheet("color: #e53935;");
    ui->labelMaxAccelMag->setStyleSheet("color: #e53935;");

    // 啟用 RichText 才能讓 Max 標籤顯示兩行 (Max + 統計)
    ui->labelMaxVoltage->setTextFormat(Qt::RichText);
    ui->labelMaxAccelMag->setTextFormat(Qt::RichText);

    // 動態加上 Save CSV 按鈕、倒數 Label
    buildExtraUi();
}

MainWindow::~MainWindow()
{
    if (serial.isOpen())
        serial.close();
    delete ui;
}

// [新增] 在程式裡建立 Save CSV / Countdown 兩個元件
//        放在現有 Calibrate/Spin/Start/Stop 按鈕那一行的「下方」
void MainWindow::buildExtraUi()
{
    QWidget *parent = ui->buttonStartMeasure->parentWidget();

    // Save CSV (放在 Stop 按鈕正下方)
    buttonSaveCsv = new QPushButton("Save CSV", parent);
    buttonSaveCsv->setGeometry(1090, 304, 61, 25);
    buttonSaveCsv->setEnabled(false);
    buttonSaveCsv->setToolTip("匯出最近一次測量的資料 (CSV)");
    connect(buttonSaveCsv, &QPushButton::clicked,
            this, &MainWindow::onSaveCsvClicked);
    buttonSaveCsv->show();

    // 倒數計時 Label
    labelCountdown = new QLabel("", parent);
    labelCountdown->setGeometry(810, 304, 271, 25);
    QFont f = labelCountdown->font();
    f.setPointSize(11);
    f.setBold(true);
    labelCountdown->setFont(f);
    labelCountdown->setStyleSheet("color: #444;");
    labelCountdown->setAlignment(Qt::AlignCenter);
    labelCountdown->show();
}

void MainWindow::setupBaudRateCombo()
{
    ui->comboBoxBaud->clear();
    ui->comboBoxBaud->addItem("9600",   QSerialPort::Baud9600);
    ui->comboBoxBaud->addItem("19200",  19200);
    ui->comboBoxBaud->addItem("38400",  38400);
    ui->comboBoxBaud->addItem("57600",  57600);
    ui->comboBoxBaud->addItem("115200", QSerialPort::Baud115200);
    ui->comboBoxBaud->setCurrentText("115200");
}

QString MainWindow::currentPortName() const
{
    int idx = ui->comboBoxPorts->currentIndex();
    if (idx < 0)
        return QString();
    return ui->comboBoxPorts->itemData(idx).toString();
}

void MainWindow::populatePortList()
{
    const QString previousPort = ui->comboBoxPorts->currentData().toString();

    ui->comboBoxPorts->clear();

    const auto infos = QSerialPortInfo::availablePorts();

    for (const QSerialPortInfo &info : infos) {
        const QString portName     = info.portName();
        const QString description  = info.description();
        const QString manufacturer = info.manufacturer();

        QString displayText;
        if (manufacturer.contains("Arduino", Qt::CaseInsensitive) ||
            description.contains("Arduino", Qt::CaseInsensitive)) {
            displayText = QString("Arduino %1").arg(portName);
        } else if (!description.isEmpty()) {
            displayText = QString("%1 %2").arg(description, portName);
        } else {
            displayText = QString("Unknown %1").arg(portName);
        }

        ui->comboBoxPorts->addItem(displayText, portName);
    }

    int index = ui->comboBoxPorts->findData(previousPort);
    if (index != -1) {
        ui->comboBoxPorts->setCurrentIndex(index);
    } else if (ui->comboBoxPorts->count() > 0) {
        ui->comboBoxPorts->setCurrentIndex(0);
    }
}

void MainWindow::updateSerialPortList()
{
    populatePortList();
}

void MainWindow::onConnectButtonClicked()
{
    if (serial.isOpen()) {
        serial.close();
        ui->buttonConnect->setText("Connect");
        ui->buttonConnect->setStyleSheet("");
        ui->statusbar->showMessage("Disconnected", 2000);
        return;
    }

    const QString portName = currentPortName();
    if (portName.isEmpty()) {
        ui->statusbar->showMessage("No valid port selected", 2000);
        return;
    }

    const int baud = ui->comboBoxBaud->currentData().toInt();

    serial.setPortName(portName);
    serial.setBaudRate(baud);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        ui->statusbar->showMessage("Failed to open " + portName + ": " + serial.errorString(), 5000);
        return;
    }

    ui->buttonConnect->setText("Disconnect");
    ui->buttonConnect->setStyleSheet(
        "background-color:#43a047; color:white; font-weight:bold;");
    ui->statusbar->showMessage("Connected to " + portName + " @ " + QString::number(baud), 3000);

    serial.clear();
    readBuffer.clear();
}

void MainWindow::handleSerialReadyRead()
{
    readBuffer.append(serial.readAll());

    int idx = -1;
    while ((idx = readBuffer.indexOf('\n')) != -1) {
        QByteArray line = readBuffer.left(idx + 1);
        readBuffer.remove(0, idx + 1);

        QString text = QString::fromUtf8(line).trimmed();
        if (text.isEmpty())
            continue;

        // [優化] terminal 改用批次累積，由 updatePlot 一次寫入
        pendingTerminalText.append(text);
        pendingTerminalText.append('\n');

        // 解析 v,gx,gy,gz
        QStringList parts = text.split(',', Qt::SkipEmptyParts);
        if (parts.size() < 4)
            continue;

        bool ok0=false, ok1=false, ok2=false, ok3=false;
        double v  = parts[0].toDouble(&ok0);
        double gx = parts[1].toDouble(&ok1);  // m/s^2
        double gy = parts[2].toDouble(&ok2);
        double gz = parts[3].toDouble(&ok3);

        if (!(ok0 && ok1 && ok2 && ok3))
            continue;

        // ====== ① 校正模式 ======
        if (calibrating) {
            calibSumX += gx;
            calibSumY += gy;
            calibSumZ += gz;
            calibSamplesCount++;

            if (calibSamplesCount >= calibSamplesTarget) {
                calibOffsetX = calibSumX / calibSamplesCount;
                calibOffsetY = calibSumY / calibSamplesCount;
                calibOffsetZ = calibSumZ / calibSamplesCount;

                calibrating = false;
                calibValid  = true;

                // 自動關閉「校正中」對話視窗
                if (calibWaitBox) {
                    calibWaitBox->accept();
                    calibWaitBox->deleteLater();
                    calibWaitBox = nullptr;
                }

                ui->statusbar->showMessage("Calibration Done!", 2000);
            }
            // [優化] 校正期間不畫圖、不更新 Label，直接 continue
            continue;
        }

        // ====== ② 套用校正偏移 ======
        if (calibValid) {
            gx -= calibOffsetX;
            gy -= calibOffsetY;
            gz -= calibOffsetZ;
        }

        // ====== ③ 把最新值快取起來，由 plotTimer 統一刷新 Label ======
        double accelMag = std::sqrt(gx*gx + gy*gy + gz*gz);
        latestV = v;
        latestAccelMag = accelMag;
        hasNewData = true;

        // ====== ④ 統計 + CSV 紀錄 (僅在 measuring 期間) ======
        if (measuring) {
            if (measureSampleCount == 0) {
                maxVoltage  = v;
                minVoltage  = v;
                maxAccelMag = accelMag;
                minAccelMag = accelMag;
            } else {
                if (v > maxVoltage) maxVoltage = v;
                if (v < minVoltage) minVoltage = v;
                if (accelMag > maxAccelMag) maxAccelMag = accelMag;
                if (accelMag < minAccelMag) minAccelMag = accelMag;
            }
            sumVoltage    += v;
            sumVoltageSq  += v*v;
            sumAccelMag   += accelMag;
            sumAccelMagSq += accelMag*accelMag;
            measureSampleCount++;

            // CSV 記錄
            recT.append(sampleIndex);
            recV.append(v);
            recAx.append(gx);
            recAy.append(gy);
            recAz.append(gz);
        }

        // ====== ⑤ 將資料加入圖表 (Replot 由 plotTimer 處理) ======
        appendData(v, gx, gy, gz);
    }
}

void MainWindow::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    ui->statusbar->showMessage("Serial error: " + serial.errorString(), 5000);

    if (error == QSerialPort::ResourceError) {
        serial.close();
        ui->buttonConnect->setText("Connect");
        ui->buttonConnect->setStyleSheet("");
    }
}

void MainWindow::appendData(double v, double ax, double ay, double az)
{
    const double t = sampleIndex++;

    gV->addData(t, v);
    gAx->addData(t, ax);
    gAy->addData(t, ay);
    gAz->addData(t, az);

    // 移除過舊資料，保持記憶體健康
    if (gV->dataCount() > maxPoints + 50) {
        double removeBefore = t - maxPoints - 50;
        gV->data()->removeBefore(removeBefore);
        gAx->data()->removeBefore(removeBefore);
        gAy->data()->removeBefore(removeBefore);
        gAz->data()->removeBefore(removeBefore);
    }
}

// 即時值 + Max/Min/Avg/RMS 雙行顯示
void MainWindow::refreshStatLabels()
{
    ui->labelCurrentVoltage->setText(
        QString("V: %1 V").arg(latestV, 0, 'f', 3));
    ui->labelCurrentAccelMag->setText(
        QString("|a|: %1 m/s²").arg(latestAccelMag, 0, 'f', 3));

    if (measuring && measureSampleCount > 0) {
        double avgV = sumVoltage    / measureSampleCount;
        double rmsV = std::sqrt(sumVoltageSq / measureSampleCount);
        double avgA = sumAccelMag   / measureSampleCount;
        double rmsA = std::sqrt(sumAccelMagSq / measureSampleCount);

        ui->labelMaxVoltage->setText(
            QString(
                "<div style='font-size:16pt; font-weight:bold; color:#e53935;'>"
                "Max V: %1 V</div>"
                "<div style='font-size:9pt; color:#555;'>"
                "Min %2 | Avg %3 | RMS %4</div>"
            )
            .arg(maxVoltage, 0, 'f', 3)
            .arg(minVoltage, 0, 'f', 3)
            .arg(avgV, 0, 'f', 3)
            .arg(rmsV, 0, 'f', 3)
        );

        ui->labelMaxAccelMag->setText(
            QString(
                "<div style='font-size:16pt; font-weight:bold; color:#e53935;'>"
                "Max |a|: %1 m/s²</div>"
                "<div style='font-size:9pt; color:#555;'>"
                "Min %2 | Avg %3 | RMS %4</div>"
            )
            .arg(maxAccelMag, 0, 'f', 3)
            .arg(minAccelMag, 0, 'f', 3)
            .arg(avgA, 0, 'f', 3)
            .arg(rmsA, 0, 'f', 3)
        );
    }
}

// 定時刷新：terminal 批次、Label、Replot
void MainWindow::updatePlot()
{
    // ① 批次寫 terminal
    if (!pendingTerminalText.isEmpty()) {
        ui->terminalView->moveCursor(QTextCursor::End);
        ui->terminalView->insertPlainText(pendingTerminalText);
        pendingTerminalText.clear();
    }

    // ② Label (有新資料才更新，省 CPU)
    if (hasNewData) {
        refreshStatLabels();
        hasNewData = false;
    }

    // ③ 還沒收到任何資料就不必 replot
    if (sampleIndex <= 0)
        return;

    double right = sampleIndex;
    double left  = (sampleIndex > maxPoints) ? (sampleIndex - maxPoints) : 0;

    plotVoltage->xAxis->setRange(left, right);
    plotAccel->xAxis->setRange(left, right);

    plotVoltage->yAxis->rescale(true);

    gAx->rescaleValueAxis(false, true);
    gAy->rescaleValueAxis(false, true);
    gAz->rescaleValueAxis(false, true);

    plotVoltage->replot(QCustomPlot::rpQueuedReplot);
    plotAccel->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::resetStats()
{
    maxVoltage    = 0.0;
    minVoltage    = 0.0;
    sumVoltage    = 0.0;
    sumVoltageSq  = 0.0;
    maxAccelMag   = 0.0;
    minAccelMag   = 0.0;
    sumAccelMag   = 0.0;
    sumAccelMagSq = 0.0;
    measureSampleCount = 0;

    recT.clear();
    recV.clear();
    recAx.clear();
    recAy.clear();
    recAz.clear();
}

void MainWindow::on_buttonStopMeasure_clicked()
{
    measuring = false;

    if (measureTimer->isActive())
        measureTimer->stop();
    if (countdownTimer->isActive())
        countdownTimer->stop();

    if (labelCountdown) labelCountdown->setText("");
    if (buttonSaveCsv)  buttonSaveCsv->setEnabled(measureSampleCount > 0);

    ui->statusbar->showMessage("Measurement paused", 2000);
}

void MainWindow::handleMeasureTimeout()
{
    measuring = false;
    if (countdownTimer->isActive())
        countdownTimer->stop();
    if (labelCountdown) labelCountdown->setText("");

    if (buttonSaveCsv) buttonSaveCsv->setEnabled(measureSampleCount > 0);

    const double avgV = (measureSampleCount > 0) ? sumVoltage    / measureSampleCount : 0;
    const double rmsV = (measureSampleCount > 0) ? std::sqrt(sumVoltageSq / measureSampleCount) : 0;
    const double avgA = (measureSampleCount > 0) ? sumAccelMag   / measureSampleCount : 0;
    const double rmsA = (measureSampleCount > 0) ? std::sqrt(sumAccelMagSq / measureSampleCount) : 0;

    QString message =
        QString("測量完成！樣本數 %1\n\n"
                "電壓 V：\n"
                "  Max %2   Min %3\n"
                "  Avg %4   RMS %5\n\n"
                "加速度 |a| (m/s²)：\n"
                "  Max %6   Min %7\n"
                "  Avg %8   RMS %9\n")
            .arg(measureSampleCount)
            .arg(maxVoltage, 0, 'f', 3)
            .arg(minVoltage, 0, 'f', 3)
            .arg(avgV,       0, 'f', 3)
            .arg(rmsV,       0, 'f', 3)
            .arg(maxAccelMag,0, 'f', 4)
            .arg(minAccelMag,0, 'f', 4)
            .arg(avgA,       0, 'f', 4)
            .arg(rmsA,       0, 'f', 4);

    QMessageBox::information(this, "測量完成", message);

    ui->buttonStartMeasure->setText("Finished!");
    ui->buttonStartMeasure->setStyleSheet(
        "background-color:#ff7043; color:white; font-weight:bold;");

    ui->plotWidget->setStyleSheet("background-color:#ffe0b2;");

    QTimer::singleShot(3000, this, [this]() {
        ui->buttonStartMeasure->setText("Start");
        ui->buttonStartMeasure->setStyleSheet("");
        ui->plotWidget->setStyleSheet("");
    });

    ui->statusbar->showMessage("Measurement finished", 2000);
}

void MainWindow::on_buttonStartMeasure_clicked()
{
    measuring = true;

    // 清除 Serial / terminal buffer
    readBuffer.clear();
    pendingTerminalText.clear();
    if (serial.isOpen()) {
        serial.clear();
    }

    // 重設統計與 CSV 紀錄
    resetStats();
    if (buttonSaveCsv) buttonSaveCsv->setEnabled(false);

    ui->labelMaxVoltage->setText("Max V: ---");
    ui->labelMaxAccelMag->setText("Max |a|: ---");

    // 清空圖表
    gV->data()->clear();
    gAx->data()->clear();
    gAy->data()->clear();
    gAz->data()->clear();

    sampleIndex = 0;

    plotVoltage->xAxis->setRange(0, maxPoints);
    plotAccel->xAxis->setRange(0, maxPoints);

    plotVoltage->replot();
    plotAccel->replot();

    // 啟動量測計時
    int seconds = ui->spinMeasureSeconds->value();
    if (seconds <= 0)
        seconds = 1;

    measureTimer->start(seconds * 1000);

    // 倒數顯示
    countdownSecondsLeft = seconds;
    if (labelCountdown)
        labelCountdown->setText(QString("⏱ Time left: %1 s").arg(countdownSecondsLeft));
    countdownTimer->start(1000);

    ui->statusbar->showMessage(
        QString("Measuring for %1 seconds...").arg(seconds), 2000);
}

void MainWindow::onCountdownTick()
{
    if (countdownSecondsLeft > 0)
        countdownSecondsLeft--;
    if (labelCountdown)
        labelCountdown->setText(QString("⏱ Time left: %1 s").arg(countdownSecondsLeft));
    if (countdownSecondsLeft <= 0)
        countdownTimer->stop();
}

void MainWindow::on_Calibrate_clicked()
{
    if (!serial.isOpen()) {
        ui->statusbar->showMessage("Serial not connected", 2000);
        return;
    }

    calibrating = true;
    calibValid  = false;
    calibSamplesCount = 0;
    calibSumX = calibSumY = calibSumZ = 0.0;
    measuring = false; // 校正時不列入最大值計算

    if (calibWaitBox) {
        delete calibWaitBox;
        calibWaitBox = nullptr;
    }

    calibWaitBox = new QMessageBox(this);
    calibWaitBox->setWindowTitle("校正中");
    calibWaitBox->setText("正在進行感測器歸零...\n請保持裝置靜止不要移動。");
    calibWaitBox->setStandardButtons(QMessageBox::NoButton);
    calibWaitBox->setModal(true);
    calibWaitBox->show();

    ui->statusbar->showMessage("Start calibration...", 2000);
}

// [新增] CSV 匯出
void MainWindow::onSaveCsvClicked()
{
    if (recT.isEmpty()) {
        QMessageBox::information(this, "Save CSV", "目前沒有可儲存的測量資料。");
        return;
    }

    const QString defaultName = QString("wind_turbine_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    const QString fileName = QFileDialog::getSaveFileName(
        this, "Save measurement as CSV", defaultName, "CSV files (*.csv)");

    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save CSV",
            "無法寫入檔案：\n" + file.errorString());
        return;
    }

    QTextStream out(&file);
    out << "sample,voltage_V,accel_x,accel_y,accel_z,accel_mag\n";

    const int n = recT.size();
    for (int i = 0; i < n; ++i) {
        const double mag = std::sqrt(
            recAx[i]*recAx[i] + recAy[i]*recAy[i] + recAz[i]*recAz[i]);
        out << recT[i] << ','
            << QString::number(recV[i],  'f', 6) << ','
            << QString::number(recAx[i], 'f', 6) << ','
            << QString::number(recAy[i], 'f', 6) << ','
            << QString::number(recAz[i], 'f', 6) << ','
            << QString::number(mag,      'f', 6) << '\n';
    }
    file.close();

    ui->statusbar->showMessage(
        QString("Saved %1 samples to %2").arg(n).arg(fileName), 4000);
}
