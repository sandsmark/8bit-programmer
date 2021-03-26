#pragma once

#include <QWidget>
#include <QHash>
#include <QMap>


class QPlainTextEdit;
class QComboBox;

class Editor : public QWidget
{
    Q_OBJECT

public:
    Editor(QWidget *parent = nullptr);
    ~Editor();

private slots:
    void onAsmChanged();
    void onUploadClicked();
    bool save();

private:
    bool loadFile(const QString &path);
    void saveAs();

    static QString generateTempFilename();

    QString parseToBinary(const QString &line, int *num);
    struct Operator {
        uint8_t opcode;
        int numArguments = 0; // todo use
        QString help;
    };

    QPlainTextEdit *m_asmEdit = nullptr;
    QPlainTextEdit *m_binOutput = nullptr;
    const QHash<QString, Operator> m_ops;
    QMap<uint32_t, uint8_t> m_memory; // qmap is sorted
    QPlainTextEdit *m_memContents = nullptr;
    QComboBox *m_serialPort = nullptr;

    QHash<QString, uint32_t> m_labels;

    QString m_currentFile;
};
