// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "glslhighlighter.h"
#include "glsleditor.h"
#include <glsl/glsllexer.h>
#include <glsl/glslparser.h>

#include <texteditor/textdocumentlayout.h>
#include <texteditor/textdocument.h>

#include <QDebug>

using namespace TextEditor;

const TextStyle GLSLReservedKeyword = C_REMOVED_LINE;

namespace GlslEditor::Internal {

class GlslHighlighter final : public TextEditor::SyntaxHighlighter
{
public:
    GlslHighlighter()
    {
        setDefaultTextFormatCategories();
    }

private:
    void highlightBlock(const QString &text) final;
    void highlightLine(const QString &text, int position, int length, const QTextCharFormat &format);
    bool isPPKeyword(QStringView text) const;
};

void GlslHighlighter::highlightBlock(const QString &text)
{
    const int previousState = previousBlockState();
    int state = 0, initialBraceDepth = 0;
    if (previousState != -1) {
        state = previousState & 0xff;
        initialBraceDepth = previousState >> 8;
    }

    int braceDepth = initialBraceDepth;

    const QByteArray data = text.toLatin1();
    GLSL::Lexer lex(/*engine=*/ nullptr, data.constData(), data.size());
    lex.setState(state);
    lex.setScanKeywords(false);
    lex.setScanComments(true);
    lex.setVariant(languageVariant(mimeType()));

    int initialState = state;

    QList<GLSL::Token> tokens;
    GLSL::Token tk;
    do {
        lex.yylex(&tk);
        tokens.append(tk);
    } while (tk.isNot(GLSL::Parser::EOF_SYMBOL));

    state = lex.state(); // refresh the state

    int foldingIndent = initialBraceDepth;
    TextBlockUserData::setFoldingIndent(currentBlock(), 0);
    TextBlockUserData::setFoldingStartIncluded(currentBlock(), false);
    TextBlockUserData::setFoldingEndIncluded(currentBlock(), false);

    if (tokens.isEmpty()) {
        setCurrentBlockState(previousState);
        TextBlockUserData::clearParentheses(currentBlock());
        if (!text.isEmpty()) // the empty line can still contain whitespace
            setFormat(0, text.length(), formatForCategory(C_VISUAL_WHITESPACE));
        TextBlockUserData::setFoldingIndent(currentBlock(), foldingIndent);
        return;
    }

    const int firstNonSpace = tokens.first().begin();

    Parentheses parentheses;
    parentheses.reserve(20); // assume wizard level ;-)

    bool highlightAsPreprocessor = false;

    for (int i = 0; i < tokens.size(); ++i) {
        const GLSL::Token &tk = tokens.at(i);

        int previousTokenEnd = 0;
        if (i != 0) {
            // mark the whitespaces
            previousTokenEnd = tokens.at(i - 1).begin() +
                               tokens.at(i - 1).length;
        }

        if (previousTokenEnd != tk.begin()) {
            setFormat(previousTokenEnd, tk.begin() - previousTokenEnd,
                      formatForCategory(C_VISUAL_WHITESPACE));
        }

        if (tk.is(GLSL::Parser::T_LEFT_PAREN) || tk.is(GLSL::Parser::T_LEFT_BRACE) || tk.is(GLSL::Parser::T_LEFT_BRACKET)) {
            const QChar c = text.at(tk.begin());
            parentheses.append(Parenthesis(Parenthesis::Opened, c, tk.begin()));
            if (tk.is(GLSL::Parser::T_LEFT_BRACE)) {
                ++braceDepth;

                // if a folding block opens at the beginning of a line, treat the entire line
                // as if it were inside the folding block
                if (tk.begin() == firstNonSpace) {
                    ++foldingIndent;
                    TextBlockUserData::setFoldingStartIncluded(currentBlock(), true);
                }
            }
        } else if (tk.is(GLSL::Parser::T_RIGHT_PAREN) || tk.is(GLSL::Parser::T_RIGHT_BRACE) || tk.is(GLSL::Parser::T_RIGHT_BRACKET)) {
            const QChar c = text.at(tk.begin());
            parentheses.append(Parenthesis(Parenthesis::Closed, c, tk.begin()));
            if (tk.is(GLSL::Parser::T_RIGHT_BRACE)) {
                --braceDepth;
                if (braceDepth < foldingIndent) {
                    // unless we are at the end of the block, we reduce the folding indent
                    if (i == tokens.size()-1 || tokens.at(i+1).is(GLSL::Parser::T_SEMICOLON))
                        TextBlockUserData::setFoldingEndIncluded(currentBlock(), true);
                    else
                        foldingIndent = qMin(braceDepth, foldingIndent);
                }
            }
        }

        bool highlightCurrentWordAsPreprocessor = highlightAsPreprocessor;

        if (highlightAsPreprocessor)
            highlightAsPreprocessor = false;

        if (false /* && i == 0 && tk.is(GLSL::Parser::T_POUND)*/) {
            highlightLine(text, tk.begin(), tk.length, formatForCategory(C_PREPROCESSOR));
            highlightAsPreprocessor = true;

        } else if (highlightCurrentWordAsPreprocessor
                   && isPPKeyword(QStringView(text).mid(tk.begin(), tk.length))) {
            setFormat(tk.begin(), tk.length, formatForCategory(C_PREPROCESSOR));

        } else if (tk.is(GLSL::Parser::T_NUMBER)) {
            setFormat(tk.begin(), tk.length, formatForCategory(C_NUMBER));

        } else if (tk.is(GLSL::Parser::T_COMMENT)) {
            highlightLine(text, tk.begin(), tk.length, formatForCategory(C_COMMENT));

            // we need to insert a close comment parenthesis, if
            //  - the line starts in a C Comment (initalState != 0)
            //  - the first token of the line is a T_COMMENT (i == 0 && tk.is(T_COMMENT))
            //  - is not a continuation line (tokens.size() > 1 || ! state)
            if (initialState && i == 0 && (tokens.size() > 1 || ! state)) {
                --braceDepth;
                // unless we are at the end of the block, we reduce the folding indent
                if (i == tokens.size()-1)
                    TextBlockUserData::setFoldingEndIncluded(currentBlock(), true);
                else
                    foldingIndent = qMin(braceDepth, foldingIndent);
                const int tokenEnd = tk.begin() + tk.length - 1;
                parentheses.append(Parenthesis(Parenthesis::Closed, QLatin1Char('-'), tokenEnd));

                // clear the initial state.
                initialState = 0;
            }

        } else if (tk.is(GLSL::Parser::T_IDENTIFIER)) {
            int kind = lex.findKeyword(data.constData() + tk.position, tk.length);
            if (kind == GLSL::Parser::T_RESERVED)
                setFormat(tk.position, tk.length, formatForCategory(GLSLReservedKeyword));
            else if (kind != GLSL::Parser::T_IDENTIFIER)
                setFormat(tk.position, tk.length, formatForCategory(C_KEYWORD));
        }
    }

    // mark the trailing white spaces
    {
        const GLSL::Token tk = tokens.last();
        const int lastTokenEnd = tk.begin() + tk.length;
        if (text.length() > lastTokenEnd)
            highlightLine(text, lastTokenEnd, text.length() - lastTokenEnd, QTextCharFormat());
    }

    if (! initialState && state && ! tokens.isEmpty()) {
        parentheses.append(Parenthesis(Parenthesis::Opened, QLatin1Char('+'),
                                       tokens.last().begin()));
        ++braceDepth;
    }

    TextBlockUserData::setParentheses(currentBlock(), parentheses);

    // if the block is ifdefed out, we only store the parentheses, but
    // do not adjust the brace depth.
    if (TextBlockUserData::ifdefedOut(currentBlock())) {
        braceDepth = initialBraceDepth;
        foldingIndent = initialBraceDepth;
    }

    TextBlockUserData::setFoldingIndent(currentBlock(), foldingIndent);
    TextBlockUserData::setBraceDepth(currentBlock(), braceDepth);
    setCurrentBlockState(lex.state());
}

void GlslHighlighter::highlightLine(const QString &text, int position, int length,
                                const QTextCharFormat &format)
{
    const QTextCharFormat visualSpaceFormat = formatForCategory(C_VISUAL_WHITESPACE);

    const int end = position + length;
    int index = position;

    while (index != end) {
        const bool isSpace = text.at(index).isSpace();
        const int start = index;

        do { ++index; }
        while (index != end && text.at(index).isSpace() == isSpace);

        const int tokenLength = index - start;
        if (isSpace)
            setFormat(start, tokenLength, visualSpaceFormat);
        else if (format.isValid())
            setFormat(start, tokenLength, format);
    }
}

bool GlslHighlighter::isPPKeyword(QStringView text) const
{
    switch (text.length())
    {
    case 2:
        if (text.at(0) == QLatin1Char('i') && text.at(1) == QLatin1Char('f'))
            return true;
        break;

    case 4:
        if (text.at(0) == QLatin1Char('e') && text == QLatin1String("elif"))
            return true;
        else if (text.at(0) == QLatin1Char('e') && text == QLatin1String("else"))
            return true;
        break;

    case 5:
        if (text.at(0) == QLatin1Char('i') && text == QLatin1String("ifdef"))
            return true;
        else if (text.at(0) == QLatin1Char('u') && text == QLatin1String("undef"))
            return true;
        else if (text.at(0) == QLatin1Char('e') && text == QLatin1String("endif"))
            return true;
        else if (text.at(0) == QLatin1Char('e') && text == QLatin1String("error"))
            return true;
        break;

    case 6:
        if (text.at(0) == QLatin1Char('i') && text == QLatin1String("ifndef"))
            return true;
        if (text.at(0) == QLatin1Char('i') && text == QLatin1String("import"))
            return true;
        else if (text.at(0) == QLatin1Char('d') && text == QLatin1String("define"))
            return true;
        else if (text.at(0) == QLatin1Char('p') && text == QLatin1String("pragma"))
            return true;
        break;

    case 7:
        if (text.at(0) == QLatin1Char('i') && text == QLatin1String("include"))
            return true;
        else if (text.at(0) == QLatin1Char('w') && text == QLatin1String("warning"))
            return true;
        break;

    case 12:
        if (text.at(0) == QLatin1Char('i') && text == QLatin1String("include_next"))
            return true;
        break;

    default:
        break;
    }

    return false;
}


TextEditor::SyntaxHighlighter *createGlslHighlighter()
{
    return new GlslHighlighter;
}

} // GlslEditor::Internal
