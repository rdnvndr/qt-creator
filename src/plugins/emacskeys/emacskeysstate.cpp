// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "emacskeysstate.h"

#include <utils/plaintextedit/plaintextedit.h>

#include <QTextCursor>

using namespace Utils;

namespace EmacsKeys {
namespace Internal {

//---------------------------------------------------------------------------
// EmacsKeysState
//---------------------------------------------------------------------------

EmacsKeysState::EmacsKeysState(PlainTextEdit *edit):
    m_ignore3rdParty(false),
    m_mark(-1),
    m_lastAction(KeysAction3rdParty),
    m_editorWidget(edit)
{
    connect(edit, &PlainTextEdit::cursorPositionChanged,
            this, &EmacsKeysState::cursorPositionChanged);
    connect(edit, &PlainTextEdit::textChanged,
            this, &EmacsKeysState::textChanged);
    connect(edit, &PlainTextEdit::selectionChanged,
            this, &EmacsKeysState::selectionChanged);
}

EmacsKeysState::~EmacsKeysState() = default;

void EmacsKeysState::setLastAction(EmacsKeysAction action)
{
    if (m_mark != -1) {
        // this code can be triggered only by 3rd party actions
        beginOwnAction();
        QTextCursor cursor = m_editorWidget->textCursor();
        cursor.clearSelection();
        m_editorWidget->setTextCursor(cursor);
        m_mark = -1;
        endOwnAction(action);
    } else {
        m_lastAction = action;
    }
}

void EmacsKeysState::cursorPositionChanged() {
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

void EmacsKeysState::textChanged() {
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

void EmacsKeysState::selectionChanged()
{
    if (!m_ignore3rdParty)
        setLastAction(KeysAction3rdParty);
}

} // namespace Internal
} // namespace EmacsKeys
