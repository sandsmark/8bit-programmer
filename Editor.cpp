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
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QSaveFile>
#include <QTimer>
#include <QFileDialog>
#include <QTextBlock>

#include <QtMath>

static const char *s_settingsKeyType = "Type";
static const char *s_settingsValOrig = "Original";
static const char *s_settingsValExt = "Extended";

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
    mainLayout->addLayout(topLayout);

    topLayout->addStretch();

    topLayout->addWidget(new QLabel("CPU Type:"));

    m_typeDropdown = new QComboBox;
    m_typeDropdown->addItems({"Original Ben Eater (4 bit memory)", "Extended (8 bit memory"});
    topLayout->addWidget(m_typeDropdown);

    QHBoxLayout *editorLayout = new QHBoxLayout;
    editorLayout->setMargin(0);

    setLayout(mainLayout);
    mainLayout->addLayout(editorLayout, 2);

    // Editor
    m_asmEdit = new QPlainTextEdit;
    new SyntaxHighlighter(m_asmEdit->document(), m_ops.keys());
    editorLayout->addWidget(m_asmEdit);

    m_binOutput = new QPlainTextEdit;
    m_binOutput->setReadOnly(true);
    new SyntaxHighlighter(m_binOutput->document(), {});
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

    QHBoxLayout *uploadBottomLayout = new QHBoxLayout;
    mainLayout->addLayout(uploadBottomLayout);

    m_memContents = new QPlainTextEdit;
    m_memContents->setReadOnly(true);
    uploadBottomLayout->addWidget(m_memContents);

    m_serialOutput = new QPlainTextEdit;
    m_serialOutput->setReadOnly(true);
    m_serialOutput->setPlaceholderText("Serial response");
    uploadBottomLayout->addWidget(m_serialOutput);

    connect(uploadButton, &QPushButton::clicked, this, &Editor::onUploadClicked);
    connect(m_asmEdit->verticalScrollBar(), &QScrollBar::valueChanged, this, &Editor::onScrolled);
    connect(m_asmEdit, &QPlainTextEdit::textChanged, this, &Editor::onAsmChanged);
    connect(m_asmEdit, &QPlainTextEdit::cursorPositionChanged, this, &Editor::onCursorMoved);
    connect(m_typeDropdown, &QComboBox::currentTextChanged, this, &Editor::onTypeChanged); // meh, use currenttext because the other is overloaded

    QSettings settings;
    loadFile(settings.value("lastOpenedFile").toString());

    const QString lastType = settings.value(s_settingsKeyType).toString();
    if (lastType == s_settingsValExt) {
        m_type = Type::ExtendedMemory;
        m_typeDropdown->setCurrentIndex(1);
    } else {
        m_type = Type::BenEater;
    }

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &Editor::save);
    timer->setInterval(500);
    connect(m_asmEdit, &QPlainTextEdit::textChanged, timer, [timer]() { timer->start(); });
}

Editor::~Editor()
{
    save();
}

void Editor::onTypeChanged()
{
    QSettings settings;
    switch(m_typeDropdown->currentIndex()) {
    case 0:
        m_type = Type::BenEater;
        settings.setValue("Type", "Original");
        break;
    case 1:
        settings.setValue("Type", "Extended");
        m_type = Type::ExtendedMemory;
        break;
    }
    onAsmChanged();
}

void Editor::onAsmChanged()
{
    m_labels.clear();
    int num = 0;
    // TODO: better way to resolve names
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        parseToBinary(line, &num);
    }

    m_binOutput->clear();
    m_memory.clear();
    m_outputLineNumbers.clear();
    num = 0;
    int outputLineNum = 0;
//    m_outputLineNumbers.append(0);
    for (const QString &line : m_asmEdit->toPlainText().split('\n')) {
        const QString output = parseToBinary(line, &num);

        // TODO: no point in trying to sync up empty lines when there isn't 1-1 mapping between lines
        if (m_type != Type::BenEater && output.isEmpty()) {
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
    onAsmChanged();

    m_currentFile = path;
    QSettings settings;
    settings.setValue("lastOpenedFile", path);
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
    settings.setValue("lastOpenedFile", m_currentFile);

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

    uint16_t binary = 0;
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
        if (m_type == Type::BenEater) {
            binary = m_ops[op].opcode << 4;
            (*num)++;
        } else {
            binary = m_ops[op].opcode << 8;
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
            return "; Invalid value '" + tokens[1] + "'";
        }

        if (value > 0xF && (m_type == Type::BenEater && op != ".db")) {
            return "; Value out of range: " + QString::number(value);
        }

        if (op == ".db") {
            helpText = helpText.arg(address).arg(value);
        } else {
            helpText = helpText.arg(value);
        }

        if (m_type == Type::BenEater) {
            binary |= value & 0xF;
        } else {
            binary |= value & 0xFF;
        }
    }

    QString ret;
    if (m_type == Type::ExtendedMemory) {
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
            return "; Memory address " + QString::number(address) + " (" + otherAddressBin + ") overwrites another value (" + otherValue + ")";
        }
        m_memory[address] = binary;

        ret += QString::asprintf("%s: %s", addressString.constData(), binaryString.constData());

        if (!helpText.isEmpty()) {
            ret += "\t; " + helpText;
        }

        if (m_type == Type::BenEater || op == ".db") {
            break;
        }

        address++;
        (*num)++;
        binary >>= 8;
        ret += "\n";
    }
    return ret;
//    return QString::asprintf("0x%.2x = 0x%.2x \t; %s = %s", address, binary, addressString.constData(), binaryString.constData());
}


void SyntaxHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat errorFormat;
    errorFormat.setForeground(Qt::darkRed);
    errorFormat.setFontWeight(QFont::Bold);
    setFormat(0, text.length(), errorFormat);

    QTextCharFormat commentFormat;
    commentFormat.setForeground(Qt::darkGray);
    QTextCharFormat opcodeFormat;
    opcodeFormat.setForeground(Qt::darkGreen);
    QTextCharFormat varFormat;
    varFormat.setForeground(Qt::darkYellow);

    QTextCharFormat addressFormat;
    addressFormat.setForeground(Qt::darkMagenta);

    QColor binColor(Qt::darkCyan);
    QTextCharFormat binFormat1;
    binFormat1.setForeground(binColor);
    QTextCharFormat binFormat2;
    binFormat2.setForeground(binColor.darker(150));

    QRegularExpression expression(";.*$");
    QRegularExpressionMatchIterator i = expression.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat);
    }

    if (!m_ops.isEmpty()) {
        expression.setPattern("^\\s*(" + m_ops.join('|') + ")\\b");
        QRegularExpressionMatchIterator i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(), match.capturedLength(), opcodeFormat);
        }

        expression.setPattern("^[a-zA-Z ]+(0x[0-9A-Fa-f]+|[0-9]+)");
        i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(1), match.capturedLength(1), varFormat);
        }
    } else {
        expression.setPattern("([01 ]+):\\s+([01]+)\\s+([01]+)");
        i = expression.globalMatch(text);

        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(1), match.capturedLength(1), addressFormat);
            setFormat(match.capturedStart(2), match.capturedLength(2), binFormat1);
            setFormat(match.capturedStart(3), match.capturedLength(3), binFormat2);
        }

    }
}
