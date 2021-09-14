#ifndef CODETEXTEDIT_H
#define CODETEXTEDIT_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    SyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

    void setOperators(const QStringList &ops) {
        m_ops = ops;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override;

    QStringList m_ops;
};

class CodeTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeTextEdit(QWidget *parent);

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

    void setBytesPerLine(const int bpl) {
        bytesPerLine = bpl;
        updateLineNumberAreaWidth();
    }
    SyntaxHighlighter *highlighter() const { return m_highlighter; }

protected:
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void updateLineNumberAreaWidth();

private slots:
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    QWidget *lineNumberArea;
    SyntaxHighlighter *m_highlighter;
    int bytesPerLine = 4;
};

class LineNumberArea : public QWidget
{
public:
    LineNumberArea(CodeTextEdit *editor) : QWidget(editor), codeEditor(editor)
    {}

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeTextEdit *codeEditor;
};

#endif // CODETEXTEDIT_H
