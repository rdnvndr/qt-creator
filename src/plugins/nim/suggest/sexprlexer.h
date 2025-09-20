// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Nim {

struct SExprLexer {
    enum Result {
        Finished,
        TokenAvailable,
        Error
    };

    enum TokenType : std::size_t {
        STRING,
        NUMBER,
        IDENTIFIER,
        OPEN_BRACE,
        CLOSE_BRACE,
    };

    struct Token {
        TokenType type;
        std::size_t start;
        std::size_t end;
    };

    SExprLexer(const char *data, std::size_t length)
        : m_data(data)
        , m_dataLength(length)
        , m_pos(0)
    {}

    static size_t tokenLength(Token &token)
    {
        return token.end - token.start + 1;
    }

    std::string tokenValue(Token &token) const
    {
        return std::string(m_data + token.start, tokenLength(token));
    }

    Result next(Token &token)
    {
        while (m_pos < m_dataLength) {
            if (m_data[m_pos] == '(') {
                token = Token{OPEN_BRACE, m_pos, m_pos + 1};
                m_pos++;
                return TokenAvailable;
            } else if (m_data[m_pos] == ')') {
                token = Token{CLOSE_BRACE, m_pos, m_pos + 1};
                m_pos++;
                return TokenAvailable;
            } else if (m_data[m_pos] == '"') {
                Token token_tmp {STRING, m_pos, m_pos};
                char previous = '"';
                m_pos++;
                while (true) {
                    if (m_pos >= m_dataLength)
                        return Error;
                    if (m_data[m_pos] == '"' && previous != '\\')
                        break;
                    previous = m_data[m_pos];
                    m_pos++;
                }
                token_tmp.end = m_pos;
                token = std::move(token_tmp);
                m_pos++;
                return TokenAvailable;
            } else if (std::isdigit(m_data[m_pos])) {
                bool decimal_separator_found = false;
                token = {NUMBER, m_pos, m_pos};
                m_pos++;
                while (true) {
                    if (m_pos >= m_dataLength)
                        break;
                    if (m_data[m_pos] == ',' || m_data[m_pos] == '.') {
                        if (decimal_separator_found)
                            return Error;
                        decimal_separator_found = true;
                    } else if (!std::isdigit(m_data[m_pos]))
                        break;
                    m_pos++;
                }
                token.end = m_pos - 1;
                return TokenAvailable;
            } else if (!std::isspace(m_data[m_pos])) {
                token = {IDENTIFIER, m_pos, m_pos};
                m_pos++;
                while (true) {
                    if (m_pos >= m_dataLength)
                        break;
                    if (std::isspace(m_data[m_pos]) || m_data[m_pos] == '(' || m_data[m_pos] == ')')
                        break;
                    m_pos++;
                }
                token.end = m_pos - 1;
                return TokenAvailable;
            } else {
                m_pos++;
            }
        }
        return Finished;
    }

private:
    const char *m_data = nullptr;
    std::size_t m_dataLength = 0;
    std::size_t m_pos = 0;
};

} // namespace Nim
