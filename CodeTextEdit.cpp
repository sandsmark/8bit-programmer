#include "CodeTextEdit.h"

#include <QPainter>
#include <QTextBlock>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QDebug>

CodeTextEdit::CodeTextEdit(QWidget *parent) : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    lineNumberArea = new LineNumberArea(this);
    lineNumberArea->setFont(font());

    connect(this, &CodeTextEdit::blockCountChanged, this, &CodeTextEdit::updateLineNumberAreaWidth);
    connect(this, &CodeTextEdit::updateRequest, this, &CodeTextEdit::updateLineNumberArea);
    connect(this, &CodeTextEdit::cursorPositionChanged, this, &CodeTextEdit::highlightCurrentLine);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeTextEdit::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount() * bytesPerLine);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}

void CodeTextEdit::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeTextEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) {
        lineNumberArea->scroll(0, dy);
    } else {
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
    }

    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void CodeTextEdit::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeTextEdit::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(Qt::yellow).lighter(160);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void CodeTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::lightGray);
    QTextBlock block = document()->begin();//firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    int bytes = 0;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(bytes);
            painter.setPen(Qt::black);
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        const QString lineContent = block.text().trimmed();
        if (!lineContent.isEmpty() && !lineContent.startsWith(';') && !lineContent.startsWith(".db")) {
            bytes += bytesPerLine;
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
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

    QTextCharFormat dbFormat;
    dbFormat.setForeground(Qt::darkBlue);

    QTextCharFormat varNameFormat = varFormat;
    varNameFormat.setFontWeight(QFont::Bold);

    QTextCharFormat labelFormat;
    labelFormat.setFontWeight(QFont::Bold);

    QTextCharFormat warningFormat;
    warningFormat.setForeground(Qt::darkRed);
    warningFormat.setFontWeight(QFont::Bold);

    QRegularExpression expression(";.*$");
    QRegularExpressionMatchIterator i = expression.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat);
    }

    const QString dataRegex("(0x[0-9A-Fa-f]+|[0-9]+)");

    {
        expression.setPattern("^(\\.db) " + dataRegex + " " + dataRegex + "( [a-zA-Z][a-zA-Z0-9]*)?");
        i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(1), match.capturedLength(1), dbFormat);
            setFormat(match.capturedStart(2), match.capturedLength(2), addressFormat);
            setFormat(match.capturedStart(3), match.capturedLength(3), varFormat);
            if (match.capturedTexts().count() > 3) {
                setFormat(match.capturedStart(4), match.capturedLength(4), varNameFormat);
            }
        }
    }

    if (!m_ops.isEmpty()) {
        expression.setPattern("^\\s*(" + m_ops.join('|') + ")\\b");
        QRegularExpressionMatchIterator i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(), match.capturedLength(), opcodeFormat);
        }

        expression.setPattern("^[a-zA-Z ]+" + dataRegex);
        i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(1), match.capturedLength(1), varFormat);
        }

        expression.setPattern(" ([a-zA-Z][a-zA-Z0-9]*)$");
        i = expression.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(1), match.capturedLength(1), varNameFormat);
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
    expression = QRegularExpression(";.*(WARNING).*");
    i = expression.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        setFormat(match.capturedStart(1), match.capturedLength(1), warningFormat);
    }

    expression = QRegularExpression("^\\s*([a-zA-Z][a-zA-Z0-9]*:)\\s*(;.*)?$");
    i = expression.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        setFormat(match.capturedStart(1), match.capturedLength(1), labelFormat);
    }
}
