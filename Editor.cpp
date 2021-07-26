#include "Editor.h"
#include "Modem.h"
#include "CodeTextEdit.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QHash>
#include <cstdint>
#include <QDebug>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QSaveFile>
#include <QTimer>
#include <QFileDialog>
#include <QTextBlock>
#include <QSpinBox>
#include <QSlider>
#include <QWindow>
#include <QProgressBar>
#include <QShortcut>
#include <QKeySequence>
#include <QCoreApplication>

#include <QtMath>

static const char *s_settingsKeyType = "Type";
static const char *s_settingsValExt = "Extended";
static const char *s_settingsValOrig = "Original";
static const char *s_settingsKeyVolume = "Volume";
static const char *s_settingsKeyModemBaudRate = "modemBaudRate";
static const char *s_settingsKeySerialBaudRate = "serialBaudRate";
static const char *s_settingsKeyLastOutput = "lastOutputDevice";
static const char *s_settingsKeyLastFile = "lastOpenedFile";
static const char *s_settingsKeySpaceFreq = "spaceFreq";
static const char *s_settingsKeyMarkFreq = "markFreq";
static const char *s_settingsKeyWaveform = "waveform";

bool Editor::isSerialPort(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    return !QSerialPortInfo(name).isNull();
}

void Editor::onDevicesUpdated(const QStringList &devices)
{
    const QString selectedDevice = m_outputSelect->currentText();
    m_outputSelect->clear();

    for (const QSerialPortInfo &portInfo : QSerialPortInfo::availablePorts()) {
        m_outputSelect->addItem(portInfo.portName());
    }

    m_outputSelect->addItems(devices);

    int newIndex = m_outputSelect->findText(selectedDevice);
    if (newIndex != -1) {
        m_outputSelect->setCurrentIndex(newIndex);
    }
}

void Editor::updateDevices()
{
    if (m_modem->audioAvailable()) {
        m_modem->updateAudioDevices();
        return;
    }

    const QString selectedDevice = m_outputSelect->currentText();
    m_outputSelect->clear();

    for (const QSerialPortInfo &portInfo : QSerialPortInfo::availablePorts()) {
        m_outputSelect->addItem(portInfo.portName());
    }

    int newIndex = m_outputSelect->findText(selectedDevice);
    if (newIndex != -1) {
        m_outputSelect->setCurrentIndex(newIndex);
    }
    // TODO: update serial ports
}

Editor::Editor(QWidget *parent)
    : QWidget(parent),
    m_ops {
        {"nop", { 0b0000, 0, "Do nothing" }},
        {"lda", { 0b0001, 1, "Load contents of memory address %1 into register A" }},
        {"add", { 0b0010, 1, "Add memory content at %1 to content of register A" }},
        {"sub", { 0b0011, 1, "Subtract memory content at %1 from content of register A" }},
        {"sta", { 0b0100, 0, "Store contents of a" }},
        {"ldi", { 0b0101, 1, "Store %1 to instruction register" }},
        {"jmp", { 0b0110, 1, "Jump to %1" }},
        {"jc",  { 0b0111, 1, "Jump to %1 if result of last calculation overflowed" }},
        {"jz",  { 0b1000, 1, "Jump to %1 if result of last calculation was zero" }},

        // TODO: these are only valid for the extended, but we don't check
        {"addi", { 0b0010, 1, "Add value %1 to content of register A (Extended only)" }},
        {"subi", { 0b0010, 1, "Subtract value %1 from content of register A (Extended only)" }},

        {"out", { 0b1110, 0, "Show content of register A on display" }},
        {"hlt", { 0b1111, 0, "Halt execution" }},
    }
{
    // TODO: make user definable, need another tab

    QVBoxLayout *mainLayout = new QVBoxLayout;

    QHBoxLayout *topLayout = new QHBoxLayout;

    QPushButton *newFileButton = new QPushButton(QIcon::fromTheme("document-new"), "New file");
    QPushButton *openFileButton = new QPushButton(QIcon::fromTheme("document-open"), "Open file...");
    QPushButton *saveFileButton = new QPushButton(QIcon::fromTheme("document-save-as"), "Save file as...");
    topLayout->addWidget(newFileButton);
    topLayout->addWidget(openFileButton);
    topLayout->addWidget(saveFileButton);

    topLayout->addStretch();

    topLayout->addWidget(new QLabel("CPU Type:"));

    m_typeDropdown = new QComboBox;
    m_typeDropdown->addItems({"Original Ben Eater (4 bit memory)", "Extended (8 bit memory)"});
    topLayout->addWidget(m_typeDropdown);

    QHBoxLayout *editorLayout = new QHBoxLayout;
    editorLayout->setMargin(0);

    setLayout(mainLayout);

    // Editor
    m_asmEdit = new CodeTextEdit(this);
    new SyntaxHighlighter(m_asmEdit->document(), m_ops.keys());
    editorLayout->addWidget(m_asmEdit);

    m_binOutput = new QPlainTextEdit;
    m_binOutput->setReadOnly(true);
    new SyntaxHighlighter(m_binOutput->document(), {});
    editorLayout->addWidget(m_binOutput);

    // Uploader
    m_uploadButton = new QPushButton("Upload (F5)");
    m_uploadButton->setShortcut(Qt::Key_F5);
    m_uploadButton->setEnabled(false); // Disabled unless there's a serial port available
    m_uploadButton->setCheckable(true);

    QHBoxLayout *uploadLayout = new QHBoxLayout;

    m_outputSelect = new DeviceList;
    m_outputSelect->setEnabled(false);

    m_modem = new Modem(this);
    {
        if (m_modem->audioAvailable()) {
            connect(m_outputSelect, &QComboBox::currentTextChanged, m_modem, &Modem::setAudioDevice);
            m_outputSelect->addItems(m_modem->audioOutputDevices());
            connect(this, &Editor::sendData, m_modem, &Modem::sendHex);
        } else {
            m_modem->deleteLater();
        }
    }

    QPushButton *settingsButton = new QPushButton(tr("Settings"));
    settingsButton->setCheckable(true);

    for (const QSerialPortInfo &portInfo : QSerialPortInfo::availablePorts()) {
        m_outputSelect->addItem(portInfo.portName());
    }

    if (m_outputSelect->count() > 0) {
        m_uploadButton->setEnabled(true);
        m_outputSelect->setEnabled(true);
    }
    m_outputSelect->setMinimumWidth(200);

    m_refreshButton = new QPushButton(tr("Refresh"));

    uploadLayout->addWidget(new QLabel("Memory contents:"));
    uploadLayout->addStretch();

    uploadLayout->addWidget(m_uploadButton);
    uploadLayout->addStretch();

    uploadLayout->addWidget(new QLabel("Output device:"));
    uploadLayout->addWidget(m_outputSelect);
    uploadLayout->addWidget(m_refreshButton);
    uploadLayout->addStretch();
    uploadLayout->addWidget(settingsButton);

    m_settingsLayout = new QHBoxLayout;

    m_markFreq = new QSpinBox;
    m_markFreq->setMaximum(30000);
    m_markFreq->setMinimum(1);
    m_markFreq->setSuffix(" Hz");
    m_markFreq->setSingleStep(5);
    m_spaceFreq = new QSpinBox;
    m_spaceFreq->setMaximum(30000);
    m_spaceFreq->setMinimum(1);
    m_spaceFreq->setSingleStep(5);
    m_spaceFreq->setSuffix(" Hz");
    m_settingsLayout->addWidget(new QLabel(tr("Space (0):")));
    m_settingsLayout->addWidget(m_spaceFreq);
    m_settingsLayout->addWidget(new QLabel(tr("Mark (1):")));
    m_settingsLayout->addWidget(m_markFreq);
    m_settingsLayout->addStretch();

    m_volumeSlider = new QSlider;
    m_volumeSlider->setMinimum(1);
    m_volumeSlider->setMaximum(100);
    m_volumeSlider->setOrientation(Qt::Horizontal);
    m_volumeSlider->setMinimumWidth(100);
    m_settingsLayout->addWidget(new QLabel(tr("Volume:")));
    m_settingsLayout->addWidget(m_volumeSlider);
    m_settingsLayout->addStretch();

    m_waveformSelect = new QComboBox;
    m_waveformSelect->addItems({
        tr("Square"),
        tr("Sawtooth"),
        tr("Triangle"),
        tr("Sine"),
        });
    m_settingsLayout->addWidget(new QLabel("Waveform:"));
    m_settingsLayout->addWidget(m_waveformSelect);
    m_settingsLayout->addStretch();

    m_baudSelect = new BaudEdit();

    m_baudSelect->setEditable(true);
    m_baudSelect->setValidator(new QIntValidator(1, 256000, this));
    m_baudSelect->addItems({
        "110",
        "300",
        "1200",
        "9600",
        "19200",
        "57600",
        "115200",
    });
    m_settingsLayout->addWidget(new QLabel(tr("Baud:")));
    m_settingsLayout->addWidget(m_baudSelect);

    m_progressBar = new QProgressBar;
    m_progressBar->setMaximum(100);
    m_progressBar->setVisible(false);

    QHBoxLayout *uploadBottomLayout = new QHBoxLayout;

    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(editorLayout, 2);
    mainLayout->addLayout(m_settingsLayout);
    mainLayout->addLayout(uploadLayout);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addLayout(uploadBottomLayout);

    m_memContents = new QPlainTextEdit;
    m_memContents->setReadOnly(true);
    uploadBottomLayout->addWidget(m_memContents);

    m_serialOutput = new QPlainTextEdit;
    m_serialOutput->setReadOnly(true);
    m_serialOutput->setPlaceholderText("Serial response");
    uploadBottomLayout->addWidget(m_serialOutput);

    QSettings settings;

    const QString lastType = settings.value(s_settingsKeyType).toString();
    if (lastType == s_settingsValExt) {
        m_type = Type::ExtendedMemory;
        m_typeDropdown->setCurrentIndex(1);
        m_asmEdit->setBytesPerLine(2);
    } else {
        m_type = Type::BenEater;
        m_asmEdit->setBytesPerLine(1);
    }

    loadFile(settings.value(s_settingsKeyLastFile).toString());
    const QString lastOutputDevice = settings.value(s_settingsKeyLastOutput).toString();
    if (isSerialPort(lastOutputDevice) || m_modem->audioOutputDevices().contains(lastOutputDevice)) {
        m_outputSelect->setCurrentText(lastOutputDevice);
    }
    if (isSerialPort(m_outputSelect->currentText())) {
        m_baudSelect->setCurrentText(QString::number(settings.value(s_settingsKeySerialBaudRate, 115200).toInt()));
    } else if (m_modem->audioAvailable()) {
        m_baudSelect->setCurrentText(QString::number(settings.value(s_settingsKeyModemBaudRate, 300).toInt()));
        m_modem->setBaud(m_baudSelect->currentText().toInt());
    }
    int markFreq = settings.value(s_settingsKeyMarkFreq, 0).toInt();
    if (markFreq <= 0) {
        markFreq = 2225;
    }
    qDebug() << "loaded mark" << markFreq;
    m_markFreq->setValue(markFreq);
    int spaceFreq = settings.value(s_settingsKeySpaceFreq, 0).toInt();
    if (spaceFreq <= 0) {
       spaceFreq = 2025;
    }
    qDebug() << "Loaded space" << spaceFreq;
    m_spaceFreq->setValue(spaceFreq);
    m_modem->setFrequencies(spaceFreq, markFreq);

    m_modem->setWaveform(settings.value(s_settingsKeyWaveform, AudioBuffer::Sine).toInt());
    m_waveformSelect->setCurrentIndex(m_modem->currentWaveform());

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &Editor::save);
    timer->setInterval(500);
    connect(m_asmEdit, &QPlainTextEdit::textChanged, timer, [timer]() { timer->start(); });

    connect(m_uploadButton, &QPushButton::clicked, this, &Editor::onUploadClicked);
    connect(settingsButton, &QPushButton::clicked, this, &Editor::setSettingsVisible);
    connect(newFileButton, &QPushButton::clicked, this, &Editor::onNewFileClicked);
    connect(openFileButton, &QPushButton::clicked, this, &Editor::onLoadFileClicked);
    connect(saveFileButton, &QPushButton::clicked, this, &Editor::saveAs);
    connect(m_asmEdit->verticalScrollBar(), &QScrollBar::valueChanged, this, &Editor::onScrolled);
    connect(m_asmEdit, &QPlainTextEdit::textChanged, this, &Editor::onAsmChanged);
    connect(m_asmEdit, &QPlainTextEdit::cursorPositionChanged, this, &Editor::onCursorMoved);
    connect(m_typeDropdown, &QComboBox::currentTextChanged, this, &Editor::onTypeChanged); // meh, use currenttext because the other is overloaded
    connect(m_baudSelect, &QComboBox::textActivated, this, &Editor::onBaudChanged); // meh, use currenttext because the other is overloaded
    connect(m_spaceFreq, &QSpinBox::textChanged, this, &Editor::onFrequencyChanged); // valueChanged is fucked because wtf qt
    connect(m_markFreq, &QSpinBox::textChanged, this, &Editor::onFrequencyChanged); // valueChanged is fucked because wtf qt
    connect(m_volumeSlider, &QSlider::valueChanged, this, &Editor::setVolume);
    connect(m_refreshButton, &QPushButton::clicked, m_modem, &Modem::updateAudioDevices);
    connect(m_modem, &Modem::devicesUpdated, this, &Editor::onDevicesUpdated, Qt::QueuedConnection);
    connect(m_modem, &Modem::stopped, this, &Editor::onUploadFinished, Qt::QueuedConnection);
    connect(m_modem, &Modem::progress, m_progressBar, &QProgressBar::setValue);
    connect(m_outputSelect, &QComboBox::textActivated, this, &Editor::onOutputChanged);
    connect(m_waveformSelect, qOverload<int>(&QComboBox::currentIndexChanged), this, &Editor::onWaveformSelected);

    m_volumeSlider->setValue(settings.value(s_settingsKeyVolume, 75).toInt());

    connect(new QShortcut(QKeySequence::Quit, this), &QShortcut::activated, qApp, &QCoreApplication::quit);

    setSettingsVisible(false);

    restoreGeometry(settings.value("geometry").toByteArray());
}

Editor::~Editor()
{
    save();
}

void Editor::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    QWidget::closeEvent(event);
}

void Editor::onTypeChanged()
{
    QSettings settings;
    switch(m_typeDropdown->currentIndex()) {
    case 0:
        m_type = Type::BenEater;
        settings.setValue(s_settingsKeyType, s_settingsValOrig);
        m_asmEdit->setBytesPerLine(1);
        break;
    case 1:
        settings.setValue(s_settingsKeyType, s_settingsValExt);
        m_type = Type::ExtendedMemory;
        m_asmEdit->setBytesPerLine(2);
        break;
    }
    onAsmChanged();
}

void Editor::onAsmChanged()
{
    m_labels.clear();
    m_usedLabels.clear();
    int num = 0;
    // TODO: better way to resolve names
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        parseToBinary(line, &num, true);
    }

    m_binOutput->clear();
    m_memory.clear();
    m_outputLineNumbers.clear();
    num = 0;
    int outputLineNum = 0;
//    m_outputLineNumbers.append(0);
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        const QString output = parseToBinary(line, &num, false);

        // TODO: no point in trying to sync up empty lines when there isn't 1-1 mapping between lines
        if (output.isEmpty()) {
            m_outputLineNumbers.append(outputLineNum);
            continue;
        }

        m_outputLineNumbers.append(outputLineNum);
        outputLineNum += output.count('\n') + 1;
        m_binOutput->insertPlainText(output + "\n");
    }

    m_memContents->clear();

    QMapIterator<uint32_t, uint8_t> memIterator(m_memory);
    while (memIterator.hasNext()) {
        memIterator.next();
        m_memContents->insertPlainText(QString::asprintf("%.2x %.2x\n", memIterator.key(), memIterator.value()));
    }
}

void Editor::onUploadFinished()
{
    qDebug() << "Upload finished";
    for (int i=0; i<m_settingsLayout->count(); i++) {
        QWidget *widget = m_settingsLayout->itemAt(i)->widget();
        if (!widget) {
            continue;
        }
        widget->setEnabled(true);
    }

    m_progressBar->setVisible(false);
    m_progressBar->setValue(0);
    qDebug() << "Hidden progress bar";
    m_uploadButton->setChecked(false);
    m_outputSelect->setEnabled(true);
    m_refreshButton->setEnabled(true);
}

void Editor::onUploadClicked()
{
    // TODO: more configurable, show output received back/get feedback, async so the user can cancel

    QSettings settings;

    const QByteArray data = m_memContents->toPlainText().toLatin1();

    qDebug() << "Upload clicked";
    if (!isSerialPort(m_outputSelect->currentText())) {
        if (!m_uploadButton->isChecked()) {
            m_modem->stop();
            return;
        }
        if (!m_modem->audioOutputDevices().contains(m_outputSelect->currentText())) {
            qWarning() << "Can't upload to invalid device";
            return;
        }

        settings.setValue(s_settingsKeyLastOutput, m_outputSelect->currentText());
        for (int i=0; i<m_settingsLayout->count(); i++) {
            QWidget *widget = m_settingsLayout->itemAt(i)->widget();
            if (!widget) {
                continue;
            }
            widget->setEnabled(false);
        }

        m_outputSelect->setEnabled(false);
        m_refreshButton->setEnabled(false);
        m_progressBar->setVisible(true);

        m_modem->setBaud(m_baudSelect->currentText().toInt());
        m_modem->setWaveform(AudioBuffer::Waveform(m_waveformSelect->currentIndex()));
        m_modem->setVolume(m_volumeSlider->value() / 100.f);
        m_modem->setFrequencies(m_spaceFreq->value(), m_markFreq->value());

        emit sendData(data);

        return;
    }

    QSerialPort serialPort(m_outputSelect->currentText());
    serialPort.setBaudRate(m_baudSelect->currentText().toInt());

    if (!serialPort.open(QIODevice::ReadWrite)) {
        QMessageBox::warning(this, "Failed top open serial port", serialPort.errorString());
        return;
    }

    settings.setValue(s_settingsKeyLastOutput, m_outputSelect->currentText());


    serialPort.write("\n");
    serialPort.write(data);
    serialPort.write("\nR\n"); // R == run/reset/whatever

    if (!serialPort.waitForBytesWritten(1000)) {
        QMessageBox::warning(this, "Timeout", "Timed out trying to write to serial port");
        return;
    }
    qWarning() << serialPort.readAll();
}

bool Editor::loadFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open" << path << file.errorString();
        return false;
    }
    const QByteArray content = file.readAll();
    if (content.isEmpty()) {
        qDebug() << "No content in" << path;
        return false;
    }

    QSignalBlocker blocker(m_asmEdit); // don't trigger the save timer
    m_asmEdit->setPlainText(QString::fromUtf8(content));
    m_asmEdit->updateLineNumberAreaWidth();
    onAsmChanged();

    m_currentFile = path;
    QSettings settings;
    settings.setValue(s_settingsKeyLastFile, path);
    qDebug() << "Loaded" << path;
    return true;
}

void Editor::saveAs()
{
    QString newPath = QFileDialog::getSaveFileName(this, "Select file to save to", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "Ben Assembly File (*.basm)");
    if (newPath.isEmpty()) {
        return;
    }
    m_currentFile = newPath;
    save();
}

void Editor::onLoadFileClicked()
{
    QString newPath = QFileDialog::getOpenFileName(this, "Select file", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "Ben Assembly File (*.basm)");
    if (newPath.isEmpty()) {
        return;
    }
    loadFile(newPath);
}

void Editor::onNewFileClicked()
{
    if (!m_currentFile.isEmpty()) {
        save();
    }
    m_currentFile.clear();
    m_asmEdit->clear();
    save();
}

void Editor::setVolume(const int percent)
{
    QSettings settings;
    settings.setValue(s_settingsKeyVolume, percent);

    m_modem->setVolume(percent / 100.f);
}

void Editor::onWaveformSelected(int waveform)
{
    if (waveform <= AudioBuffer::Invalid || waveform >= AudioBuffer::WaveformCount) {
        qWarning() << "Invalid waveform" << waveform << "defaulting to triangle";
        waveform = AudioBuffer::Triangle;
    }
    m_modem->setWaveform(AudioBuffer::Waveform(waveform));
    QSettings settings;
    settings.setValue(s_settingsKeyWaveform, waveform);
}

int Editor::currentLineNumber()
{
    // holy fuck qt

    QTextCursor cursor = m_asmEdit->textCursor();
    cursor.movePosition(QTextCursor::StartOfLine);

    int lines = 0;
    while (cursor.positionInBlock() > 0) {
        cursor.movePosition(QTextCursor::Up);
        lines++;
    }
    QTextBlock block = cursor.block().previous();

    while(block.isValid()) {
        lines += block.lineCount();
        block = block.previous();
    }

    return lines;
}
void Editor::onScrolled()
{

//    qDebug() << m_asmEdit->verticalScrollBar()->value() << m_asmEdit->verticalScrollBar()->maximum();
//    float relativeValue = float(m_asmEdit->verticalScrollBar()->value()) / m_asmEdit->verticalScrollBar()->maximum();
    int asmLine = m_asmEdit->verticalScrollBar()->value();
    scrollOutputTo(asmLine);
}

void Editor::onCursorMoved()
{
    // does jack shit
//    m_binOutput->textCursor().clearSelection();

    m_binOutput->moveCursor(QTextCursor::Start, QTextCursor::KeepAnchor);
    m_binOutput->moveCursor(QTextCursor::Start, QTextCursor::MoveAnchor);

    const int asmLine = currentLineNumber();

    if (asmLine >= m_outputLineNumbers.size()) {
        qWarning() << "Line out of range" << asmLine;
        m_binOutput->verticalScrollBar()->setValue(m_binOutput->verticalScrollBar()->maximum());
        return;
    }
    const int startLine = m_outputLineNumbers[asmLine];
    if (startLine == -1) {
        return;
    }

    int endLine = -1;
    if (asmLine + 1 < m_outputLineNumbers.size()) {
        endLine = m_outputLineNumbers[asmLine + 1];
    }


    highlightOutput(startLine, endLine);
    m_binOutput->ensureCursorVisible();
    //    m_binOutput->verticalScrollBar()->setValue(startLine);
}

void Editor::setSettingsVisible(bool visible)
{
    for (int i=0; i<m_settingsLayout->count(); i++) {
        QWidget *widget = m_settingsLayout->itemAt(i)->widget();
        if (!widget) {
            continue;
        }
        widget->setVisible(visible);
    }
    if (visible && m_modem->audioOutputDevices().contains(m_outputSelect->currentText())) {
        m_spaceFreq->setEnabled(true);
        m_markFreq->setEnabled(true);
    } else {
        m_spaceFreq->setEnabled(false);
        m_markFreq->setEnabled(false);
    }
}

void Editor::onOutputChanged(const QString &outputName)
{
    qDebug() << "Output set to" << outputName;
    QSettings settings;
    settings.setValue(s_settingsKeyLastOutput, outputName);

    if (isSerialPort(m_outputSelect->currentText())) {
        m_baudSelect->setCurrentText(QString::number(settings.value(s_settingsKeySerialBaudRate, 115200).toInt()));
    } else if (m_modem->audioAvailable()) {
        m_baudSelect->setCurrentText(QString::number(settings.value(s_settingsKeyModemBaudRate, 300).toInt()));
    }

}

void Editor::onBaudChanged(QString baudString)
{
    qDebug() << "baud" << baudString << m_baudSelect->hasFocus();
    int unused;
    if (m_baudSelect->validator()->validate(baudString, unused) != QValidator::Acceptable) {
        m_baudSelect->setCurrentText("300");
    }

    bool ok;
    int baud = baudString.toInt(&ok);
    if (!ok || baud <= 0) {
        m_baudSelect->setCurrentIndex(0);
        return;
    }

    QSettings settings;

    if (isSerialPort(m_outputSelect->currentText())) {
        settings.setValue(s_settingsKeySerialBaudRate, baud);
    } else if (m_modem->audioOutputDevices().contains(m_outputSelect->currentText())) {
        settings.setValue(s_settingsKeyModemBaudRate, baud);
        m_modem->setBaud(baud);
    }
}

void Editor::onFrequencyChanged()
{
    const int space = m_spaceFreq->value();
    if (space <= 0) {
        return;
    }
    const int mark = m_markFreq->value();
    if (mark <= 0) {
        return;
    }
    m_modem->setFrequencies(space, mark);

    QSettings settings;
    settings.setValue(s_settingsKeySpaceFreq, space);
    settings.setValue(s_settingsKeyMarkFreq, mark);

}

void Editor::scrollOutputTo(const int line)
{
    if (line >= m_outputLineNumbers.size()) {
        qWarning() << "Line out of range" << line;
        m_binOutput->verticalScrollBar()->setValue(m_binOutput->verticalScrollBar()->maximum());
        return;
    }
    m_binOutput->verticalScrollBar()->setValue(m_outputLineNumbers[line]);
}

void Editor::highlightOutput(const int firstLine, const int lastLine)
{
    QTextCursor cursor = m_binOutput->textCursor();
    cursor.clearSelection();

    m_binOutput->moveCursor(QTextCursor::Start);

    int line = 0;
    while (!cursor.atEnd()) {
        line += cursor.block().lineCount();
        if (line > firstLine) {
            break;
        }
        m_binOutput->moveCursor(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
    }

    if (lastLine == firstLine) {
        return;
    }

    while (!cursor.atEnd()) {
        m_binOutput->moveCursor(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        line += cursor.block().lineCount();
        if (line != -1 && line >= lastLine) {
            break;
        }
    }

    if (lastLine == -1) {
        m_binOutput->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
        // Skip the last newline at the end
        m_binOutput->moveCursor(QTextCursor::Left, QTextCursor::KeepAnchor);
    }
}

bool Editor::save()
{
    if (m_currentFile.isEmpty()) {
        m_currentFile = generateTempFilename();
    }
    QSaveFile file(m_currentFile);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open" << m_currentFile << file.errorString();
        m_currentFile = generateTempFilename();
        file.setFileName(m_currentFile);

        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to open generated" << m_currentFile << file.errorString();
            return false;
        }
    }
    QSettings settings;
    settings.setValue(s_settingsKeyLastFile, m_currentFile);

    file.write(m_asmEdit->toPlainText().toUtf8());
    file.commit();

    return true;
}


QString Editor::generateTempFilename()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(dir.absolutePath());
    return dir.filePath(QDateTime::currentDateTime().toString(Qt::ISODate)) + ".basm";
}

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c %c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
    ((byte) & 0x80 ? '1' : '0'), \
    ((byte) & 0x40 ? '1' : '0'), \
    ((byte) & 0x20 ? '1' : '0'), \
    ((byte) & 0x10 ? '1' : '0'), \
    ((byte) & 0x08 ? '1' : '0'), \
    ((byte) & 0x04 ? '1' : '0'), \
    ((byte) & 0x02 ? '1' : '0'), \
    ((byte) & 0x01 ? '1' : '0')
#define NIBBLE_TO_BINARY_PATTERN "%c%c%c%c"
#define NIBBLE_TO_BINARY(byte)  \
    ((byte) & 0x08 ? '1' : '0'), \
    ((byte) & 0x04 ? '1' : '0'), \
    ((byte) & 0x02 ? '1' : '0'), \
    ((byte) & 0x01 ? '1' : '0')

QString Editor::parseToBinary(const QString &line, int *num, bool firstPass)
{
    const int eol = line.indexOf(';');
    QStringList tokens = line.mid(0, eol).split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return "";
    }
    QString op = tokens.first().toLower().simplified();
    if (op.endsWith(":")) {
        op.chop(1);
        if (!firstPass) {
            if (m_usedLabels.contains(op)) {
                return " ; WARNING: label '" + op + "' already exists\n";
            }
            m_usedLabels.insert(op);
        }

        m_labels[op] = *num;
        return " ; Label '" + op + "'\n";
    }

    uint16_t binary = 0;
    uint32_t address = 0;

    QString helpText;
    if (op == ".db") {
        if (tokens.count() < 3) {
            return "; Syntax: .db address value [label]\n";
        }
        bool ok = false;
        const QString addressString = tokens.takeAt(1);

        if (addressString.startsWith("0x")) {
            address = (addressString.toInt(&ok, 16));
        } else {
            address = (addressString.toInt(&ok));
        }
        if (!ok) {
            return "; Invalid value '" + addressString + "'\n";
        }
        if (address > 0xFF) {
            return "; Address out of range: " + QString::number(address) + "\n";
        }
        helpText = QString("Memory content at %1 is %2");
        if (tokens.count() > 2) {
            m_labels[tokens[2]] = address;
            helpText += " (named " + tokens[2] + ")";
        }
    } else {
        if (!m_ops.contains(op)) {
            return "; Invalid operator '" + op + "'\n";
        }
        if (tokens.count() != m_ops[op].numArguments + 1) {
            return "; Operator '" + op + "' takes " + QString::number(m_ops[op].numArguments) + " argument(s)\n";
        }
        if (m_type == Type::BenEater) {
            binary = m_ops[op].opcode << 4;
            (*num)++;
        } else {
            binary = m_ops[op].opcode;
        }
        address = *num;
        helpText = m_ops[op].help;

    }

    if (tokens.count() > 1) {
        bool ok = false;
        uint8_t value = 0;
        if (m_labels.contains(tokens[1])) {
            value = m_labels[tokens[1]];
            ok = true;
        } else if (tokens[1].startsWith("0x")) {
            value = (tokens[1].toInt(&ok, 16));
        } else {
            value = (tokens[1].toInt(&ok));
        }

        if (!ok) {
            return "; Invalid value '" + tokens[1] + "'\n";
        }

        if (value > 0xF && (m_type == Type::BenEater && op != ".db")) {
            return "; Value out of range: " + QString::number(value) + "\n";
        }

        if (op == ".db") {
            helpText = helpText.arg(address).arg(value);
        } else {
            helpText = helpText.arg(value);
        }

        if (m_type == Type::BenEater) {
            binary |= value & 0xF;
        } else {
            if (op == ".db") {
                binary |= (value & 0xFF);
            } else {
                binary |= (value & 0xFF) << 8;
            }
        }
    }

    QString ret;
    if (m_type == Type::ExtendedMemory && op != ".db") {
        ret = "; " + line.mid(0, eol).simplified() + ": " + helpText + "\n";
        helpText.clear();
    }
    for (int i=0; i<2; i++) {
        const QByteArray binaryString = QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(binary & 0xFF)).toLatin1();
        QByteArray addressString;
        if (m_type == Type::BenEater) {
            addressString = QString::asprintf(NIBBLE_TO_BINARY_PATTERN, NIBBLE_TO_BINARY(address)).toLatin1();
        } else {
            addressString = QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(address)).toLatin1();
        }

        if (m_memory.contains(address)) {
            // TODO: track line numbers
            const QString otherValue = QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(m_memory[address]));
            const QString otherAddressBin = QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(address));
            helpText +=  " WARNING: overwrites " + otherValue + " at " + otherAddressBin + "!";
        }
        m_memory[address] = binary & 0xFF;

        ret += QString::asprintf("%s: %s", addressString.constData(), binaryString.constData());

        if (!helpText.isEmpty()) {
            ret += "\t; " + helpText;
        }

        if (op == ".db") {
            ret += "\n";
            break;
        }

        if (m_type == Type::BenEater) {
            break;
        }

        address++;
        (*num)++;
        binary >>= 8;
        //m_memory[address] = binary;
        ret += "\n";
    }
    return ret;
//    return QString::asprintf("0x%.2x = 0x%.2x \t; %s = %s", address, binary, addressString.constData(), binaryString.constData());
}
