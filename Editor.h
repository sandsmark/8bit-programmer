#pragma once

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
class Modem;

class Editor : public QWidget
{
    Q_OBJECT

    enum class Type {
        BenEater,
        ExtendedMemory
    };

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
    void onTypeChanged();
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

private:
    static bool isSerialPort(const QString &name);
    bool loadFile(const QString &path);

    int currentLineNumber();
    void scrollOutputTo(const int line);
    void highlightOutput(const int firstLine, const int lastLine);

    static QString generateTempFilename();

    QString parseToBinary(const QString &line, int *num);
    struct Operator {
        uint8_t opcode;
        int numArguments = 0; // todo use
        QString help;
    };

    CodeTextEdit *m_asmEdit = nullptr;
    QPlainTextEdit *m_binOutput = nullptr;
    const QHash<QString, Operator> m_ops;
    QMap<uint32_t, uint8_t> m_memory; // qmap is sorted
    QPlainTextEdit *m_memContents = nullptr;
    DeviceList *m_outputSelect = nullptr;

    QComboBox *m_typeDropdown = nullptr;

    QPushButton *m_uploadButton = nullptr;
    QPlainTextEdit *m_serialOutput = nullptr;
    QPushButton *m_refreshButton = nullptr;

    QProgressBar *m_progressBar = nullptr;

    QHash<QString, uint32_t> m_labels;
    QVector<int> m_outputLineNumbers;

    QString m_currentFile;

    Type m_type = Type::BenEater;

    QComboBox *m_baudSelect;
    QComboBox *m_waveformSelect;
    QSlider *m_volumeSlider;
    QHBoxLayout *m_settingsLayout;

    QSpinBox *m_spaceFreq;
    QSpinBox *m_markFreq;

    Modem *m_modem;
};
