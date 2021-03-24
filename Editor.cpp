#include "Editor.h"
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

        {"out", { 0b1110, 0, "Show content of register A on display" }},
        {"hlt", { 0b1111, 0, "Halt execution" }},
    }
{
    // TODO: make user definable, need another tab

    QVBoxLayout *mainLayout = new QVBoxLayout;
    QHBoxLayout *editorLayout = new QHBoxLayout;
    editorLayout->setMargin(0);

    setLayout(mainLayout);
    mainLayout->addLayout(editorLayout, 2);

    // Editor
    m_asmEdit = new QPlainTextEdit;
    editorLayout->addWidget(m_asmEdit);

    m_binOutput = new QPlainTextEdit;
    m_binOutput->setReadOnly(true);
    editorLayout->addWidget(m_binOutput);

    // Uploader
    QPushButton *uploadButton = new QPushButton("Upload (F5)");
    uploadButton->setEnabled(false); // Disabled unless there's a serial port available

    QHBoxLayout *uploadLayout = new QHBoxLayout;
    mainLayout->addLayout(uploadLayout);

    m_serialPort = new QComboBox;
    m_serialPort->setEnabled(false);
    for (const QSerialPortInfo &portInfo : QSerialPortInfo::availablePorts()) {
        qDebug() << "Port:" << portInfo.portName();
        m_serialPort->addItem(portInfo.portName());
        uploadButton->setEnabled(true);
        m_serialPort->setEnabled(true);
    }
    uploadLayout->addWidget(new QLabel("Memory contents:"));
    uploadLayout->addStretch();

    uploadLayout->addWidget(new QLabel("Serial port:"));
    uploadLayout->addWidget(m_serialPort);
    uploadLayout->addWidget(uploadButton);

    m_memContents = new QPlainTextEdit;
    mainLayout->addWidget(m_memContents);

    connect(uploadButton, &QPushButton::clicked, this, &Editor::onUploadClicked);
    connect(m_asmEdit->verticalScrollBar(), &QScrollBar::valueChanged, m_binOutput->verticalScrollBar(), &QScrollBar::setValue);
    connect(m_asmEdit, &QPlainTextEdit::textChanged, this, &Editor::onAsmChanged);
}

void Editor::onAsmChanged()
{
    m_labels.clear();
    int num = 0;
    // TODO: better way
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        parseToBinary(line, &num);
    }
    qDebug() << m_labels;

    m_binOutput->clear();
    m_memory.clear();
    num = 0;
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        m_binOutput->insertPlainText(parseToBinary(line, &num) + "\n");
    }

    m_memContents->clear();

    QMapIterator<uint32_t, uint8_t> memIterator(m_memory);
    while (memIterator.hasNext()) {
        memIterator.next();
        m_memContents->insertPlainText(QString::asprintf("%.2x %.2x\n", memIterator.key(), memIterator.value()));
    }
}

void Editor::onUploadClicked()
{
    // TODO: more configurable, show output received back/get feedback, async so the user can cancel

    if (m_serialPort->currentText().isEmpty()) {
        qWarning() << "No port selected";
        return;
    }

    QSerialPort serialPort(m_serialPort->currentText());
    if (!serialPort.open(QIODevice::ReadWrite)) {
        QMessageBox::warning(this, "Failed top open serial port", serialPort.errorString());
        return;
    }

    serialPort.setBaudRate(57600); // TODO: configurable

    serialPort.write("\n");
    serialPort.write(m_memContents->toPlainText().toLatin1());
    serialPort.write("\n");

    if (!serialPort.waitForBytesWritten(1000)) {
        QMessageBox::warning(this, "Timeout", "Timed out trying to write to serial port");
        return;
    }
    qWarning() << serialPort.readAll();
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

QString Editor::parseToBinary(const QString &line, int *num)
{
    const int eol = line.indexOf(';');
    QStringList tokens = line.mid(0, eol).split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return "";
    }
    QString op = tokens.first().toLower().simplified();
    if (op.endsWith(":")) {
        op.chop(1);
        m_labels[op] = *num;
        return " ; Label '" + op + "'";
    }

    uint8_t binary = 0;
    uint32_t address = 0;

    QString helpText;
    if (op == ".db") {
        if (tokens.count() < 3) {
            return "; Syntax: .db address value [label]";
        }
        bool ok = false;
        const QString addressString = tokens.takeAt(1);

        if (addressString.startsWith("0x")) {
            address = (addressString.toInt(&ok, 16));
        } else {
            address = (addressString.toInt(&ok));
        }
        if (!ok) {
            return "; Invalid value '" + addressString + "'";
        }
        if (address > 0xFF) {
            return "; Address out of range: " + QString::number(address);
        }
        helpText = QString("Memory content at %1 is %2");
        if (tokens.count() > 2) {
            m_labels[tokens[2]] = address;
            helpText += " (named " + tokens[2] + ")";
        }
    } else {
        if (!m_ops.contains(op)) {
            return "; Invalid operator '" + op + "'";
        }
        if (tokens.count() != m_ops[op].numArguments + 1) {
            return "; Operator '" + op + "' takes " + QString::number(m_ops[op].numArguments) + " argument(s)";
        }
        binary = m_ops[op].opcode << 4;
        address = *num;
        helpText = m_ops[op].help;

        (*num)++;
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
            return "; Invalid value '" + tokens[1] + "'";
        }
        if (value > 0xF && op != ".db") {
            return "; Value out of range: " + QString::number(value);
        }
        if (op == ".db") {
            helpText = helpText.arg(address).arg(value);
        } else {
            helpText = helpText.arg(value);
        }

        binary |= value & 0xF;
    }

    const QByteArray binaryString = QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(binary)).toLatin1();
    const QByteArray addressString = QString::asprintf(NIBBLE_TO_BINARY_PATTERN, NIBBLE_TO_BINARY(address)).toLatin1();

    if (m_memory.contains(address)) {
        // TODO: track line numbers
        return "; Memory address " + QString::number(address) + " overwrites another value (" + QString::asprintf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(m_memory[address])).toLatin1() + ")";
    }
    m_memory[address] = binary;

    return QString::asprintf("%s: %s\t; %s", addressString.constData(), binaryString.constData(), helpText.toUtf8().constData());
//    return QString::asprintf("0x%.2x = 0x%.2x \t; %s = %s", address, binary, addressString.constData(), binaryString.constData());
}

