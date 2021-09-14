#include "CPU.h"
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QMessageBox>
#include <QDebug>

bool CPU::loadFile(const QString &filename)
{
    qDebug() << "Loading CPU" << filename;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(nullptr, "Failed top open operators file", file.errorString());
        return false;
    }
    QSet<uint8_t> usedOpcodes;

    QHash<QString, Operator> ops;
    int bitCount = -1;

    while (!file.atEnd()) {
        const QString &line = QString::fromUtf8(file.readLine()).simplified();
        if (line.startsWith('#')) {
            continue;
        }
        if (line.isEmpty()) {
            continue;
        }
        bool ok;
        if (line.startsWith("bits:")) {
            bitCount = line.split(':').last().toInt(&ok);
            if (!ok) {
                QMessageBox::warning(nullptr, "Invalid operators file", "Invalid bits specification:\n" + line);
                return false;
            }
            if (bitCount != 8 && bitCount != 16) {
                QMessageBox::warning(nullptr, "Invalid operators file", "Only 8 and 16 bit opcodes are supported:\n" + line);
                return false;
            }
            continue;
        }
        const QStringList parts = line.split(';');
        if (parts.count() != 4) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Invalid line:\n" + line);
            return false;
        }
        const QString name = parts[0].trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Missing operator name:\n" + line);
            return false;
        }
        if (ops.contains(name)) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Duplicate operator:\n" + line);
            return false;
        }
        Operator op;
        QString opcode = parts[1].trimmed();

        if (opcode.startsWith("0b")) {
            opcode = opcode.mid(2);
            op.opcode = opcode.toInt(&ok, 2);
        } else if (opcode.startsWith("0x")) {
            op.opcode = opcode.toInt(&ok, 0);
        } else {
            op.opcode = opcode.toInt(&ok);
        }
        if (!ok) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Invalid opcode on line:\n" + line);
            return false;
        }
        if (usedOpcodes.contains(op.opcode)) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Duplicate opcode on line:\n" + line);
            return false;
        }
        usedOpcodes.insert(op.opcode);

        op.numArguments = parts[2].trimmed().toInt(&ok);
        if (!ok || op.numArguments < 0) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Invalid number of operators on line:\n" + line);
            return false;
        }
        if (op.numArguments > 1) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Only supports 0 or 1 operators for now:\n" + line);
            return false;
        }
        op.help = parts[3].trimmed();
        if (op.numArguments == 0 && op.help.contains("%1")) {
            QMessageBox::warning(nullptr, "Invalid operators file", "Description can't contain %1 for ops without arguments:\n" + line);
            return false;
        }

        ops[name] = op;
        qDebug() << name << op.opcode << op.help;
    }

    if (bitCount != 8 && bitCount != 16) {
        QMessageBox::warning(nullptr, "Invalid operators file", "No bit width specified in CPU file");
        return false;
    }

    if (ops.isEmpty()) {
        QMessageBox::warning(nullptr, "Invalid operators file", "No operators in CPU file");
        return false;
    }

    m_bits = bitCount;
    m_operators = std::move(ops);

    return true;
}

