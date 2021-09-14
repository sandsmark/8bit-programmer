#pragma once

#include "CPU.h"

#include <QWidget>
#include <QHash>
#include <QMap>
#include <QDebug>
#include <QFocusEvent>
#include <QKeyEvent>

#include <QComboBox>
#include <QStylePainter>

class DeviceList : public QComboBox
{
protected:
    void paintEvent(QPaintEvent *event) override {
        QStyleOptionComboBox opt;
        initStyleOption(&opt);


        QStylePainter painter(this);
        painter.drawComplexControl(QStyle::CC_ComboBox, opt);

        const QRect textRect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        opt.currentText = painter.fontMetrics().elidedText(opt.currentText, Qt::ElideMiddle, textRect.width());
        painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }
};

class BaudEdit : public QComboBox
{
    Q_OBJECT


protected:
    void focusOutEvent(QFocusEvent *event) override {
        if (event->reason() != Qt::ActiveWindowFocusReason) {
            emit textActivated(currentText());
        }


        QComboBox::focusOutEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            clearFocus();
            return;
        }
        QComboBox::keyPressEvent(event);
    }
};

class CodeTextEdit;
class QPlainTextEdit;
class QComboBox;
class QHBoxLayout;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QLabel;
class Modem;

class Editor : public QWidget
{
    Q_OBJECT

public:
    Editor(QWidget *parent = nullptr);
    ~Editor();

public slots:
    void onDevicesUpdated(const QStringList &devices);

signals:
    void sendData(const QByteArray &data);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAsmChanged();
    void onUploadClicked();
    void onUploadFinished();
    bool save();
    void onScrolled();
    void onCursorMoved();
    void setSettingsVisible(bool visible);
    void onOutputChanged(const QString &outputName);
    void onBaudChanged(QString baudString);
    void onFrequencyChanged();
    void saveAs();
    void onLoadFileClicked();
    void onNewFileClicked();
    void setVolume(const int percent);
    void onWaveformSelected(int waveform);
    void updateDevices();
    void onLoadCPUClicked();

private:
    static bool isSerialPort(const QString &name);
    bool loadFile(const QString &path);
    void reloadCPU();

    int currentLineNumber();
    void scrollOutputTo(const int line);
    void highlightOutput(const int firstLine, const int lastLine);

    static QString generateTempFilename();

    QString parseToBinary(const QString &line, int *num, bool firstPass);

    CodeTextEdit *m_asmEdit = nullptr;
    QPlainTextEdit *m_binOutput = nullptr;
    QMap<uint32_t, uint8_t> m_memory; // qmap is sorted
    QPlainTextEdit *m_memContents = nullptr;
    DeviceList *m_outputSelect = nullptr;
    QPushButton *m_loadCPUButton = nullptr;
    QLabel *m_cpuInfoLabel = nullptr;

    QPushButton *m_uploadButton = nullptr;
    QPlainTextEdit *m_serialOutput = nullptr;
    QPushButton *m_refreshButton = nullptr;

    QProgressBar *m_progressBar = nullptr;

    QHash<QString, uint32_t> m_labels;
    QSet<QString> m_usedLabels; // so sue me
    QVector<int> m_outputLineNumbers;

    QString m_currentFile;

    CPU m_cpu;

    QComboBox *m_baudSelect;
    QComboBox *m_waveformSelect;
    QSlider *m_volumeSlider;
    QHBoxLayout *m_settingsLayout;

    QSpinBox *m_spaceFreq;
    QSpinBox *m_markFreq;

    Modem *m_modem;
};
