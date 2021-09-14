#pragma once

#include <QHash>
#include <QString>

struct CPU
{
    struct Operator
    {
        uint8_t opcode;
        int numArguments = 0; // todo use
        QString help;
    };

    bool loadFile(const QString &filename);

    uint8_t bits() const { return m_bits; }
    const QHash<QString, Operator> &operators() const { return m_operators; }

    bool isValid() const {
        return (m_bits == 8 || m_bits == 16) && !m_operators.isEmpty();
    }

private:
    int m_bits = 8;
    QHash<QString, Operator> m_operators;
};

