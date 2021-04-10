#pragma once

#include <QWidget>
#include <QHash>
#include <QMap>
#include <QSyntaxHighlighter>


class QPlainTextEdit;
class QComboBox;
class Modem;

class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    SyntaxHighlighter(QTextDocument *document, const QStringList &ops) : QSyntaxHighlighter(document), m_ops(ops) {}

protected:
    void highlightBlock(const QString &text) override;

    const QStringList m_ops;
};

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

signals:
    void sendData(const QByteArray &data);

private slots:
    void onTypeChanged();
    void onAsmChanged();
    void onUploadClicked();
    bool save();
    void onScrolled();
    void onCursorMoved();

private:
    bool loadFile(const QString &path);
    void saveAs();

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

    QPlainTextEdit *m_asmEdit = nullptr;
    QPlainTextEdit *m_binOutput = nullptr;
    const QHash<QString, Operator> m_ops;
    QMap<uint32_t, uint8_t> m_memory; // qmap is sorted
    QPlainTextEdit *m_memContents = nullptr;
    QComboBox *m_serialPort = nullptr;

    QComboBox *m_typeDropdown = nullptr;

    QPlainTextEdit *m_serialOutput = nullptr;

    QHash<QString, uint32_t> m_labels;
    QVector<int> m_outputLineNumbers;

    QString m_currentFile;

    Type m_type = Type::BenEater;
};
