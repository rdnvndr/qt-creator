// Copyright (C) 2016 Nicolas Arnaud-Cormos
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "macromanager.h"

#include "actionmacrohandler.h"
#include "findmacrohandler.h"
#include "imacrohandler.h"
#include "macro.h"
#include "macroevent.h"
#include "macrosconstants.h"
#include "macrostr.h"
#include "texteditormacrohandler.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <texteditor/texteditorconstants.h>

#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>

namespace Macros::Internal {

/*!
    \namespace Macros
    \brief The Macros namespace contains support for macros in \QC.
*/

/*!

    \class Macro::MacroManager
    \brief The MacroManager class implements a manager for macros.

    The MacroManager manages all macros, loads them on startup, keeps track of the
    current macro, and creates new macros.

    There are two important functions in this class that can be used outside the Macros plugin:
    \list
    \li registerEventHandler: add a new event handler
    \li registerAction: add a macro event when this action is triggered
    \endlist

    This class is a singleton and can be accessed using the instance function.
*/

/*!
    \fn void registerAction(QAction *action, const QString &id)

    Appends \a action to the list of actions registered in a macro. \a id is
    the action id passed to the ActionManager.
*/

class MacroManagerPrivate
{
public:
    MacroManagerPrivate(MacroManager *qq);

    MacroManager *q;
    QMap<QString, Macro *> macros;
    QMap<QString, QAction *> actions;
    Macro *currentMacro = nullptr;
    bool isRecording = false;

    QList<IMacroHandler*> handlers;

    ActionMacroHandler *actionHandler;
    TextEditorMacroHandler *textEditorHandler;
    FindMacroHandler *findHandler;

    void initialize();
    void addMacro(Macro *macro);
    void removeMacro(const QString &name);
    void changeMacroDescription(Macro *macro, const QString &description);

    bool executeMacro(Macro *macro);
    void showSaveDialog();
};

MacroManagerPrivate::MacroManagerPrivate(MacroManager *qq):
    q(qq)
{
    // Load existing macros
    initialize();

    actionHandler = new ActionMacroHandler;
    textEditorHandler = new TextEditorMacroHandler;
    findHandler = new FindMacroHandler;
}

void MacroManagerPrivate::initialize()
{
    macros.clear();
    const QDir dir(MacroManager::macrosDirectory());
    QStringList filter;
    filter << QLatin1String("*.") + QLatin1String(Constants::M_EXTENSION);
    const QStringList files = dir.entryList(filter, QDir::Files);

    for (const QString &name : files) {
        QString fileName = dir.absolutePath() + QLatin1Char('/') + name;
        auto macro = new Macro;
        if (macro->loadHeader(fileName))
            addMacro(macro);
        else
            delete macro;
    }
}

static Utils::Id makeId(const QString &name)
{
    return Utils::Id(Macros::Constants::PREFIX_MACRO).withSuffix(name);
}

void MacroManagerPrivate::addMacro(Macro *macro)
{
    // Add sortcut
    Core::Context context(TextEditor::Constants::C_TEXTEDITOR);
    auto action = new QAction(macro->description(), q);
    Core::Command *command = Core::ActionManager::registerAction(
                action, makeId(macro->displayName()), context);
    command->setAttribute(Core::Command::CA_UpdateText);
    QObject::connect(action, &QAction::triggered, q, [this, macro]() {
        q->executeMacro(macro->displayName());
    });

    // Add macro to the map
    macros[macro->displayName()] = macro;
    actions[macro->displayName()] = action;
}

void MacroManagerPrivate::removeMacro(const QString &name)
{
    if (!macros.contains(name))
        return;
    // Remove shortcut
    QAction *action = actions.take(name);
    Core::ActionManager::unregisterAction(action, makeId(name));
    delete action;

    // Remove macro from the map
    Macro *macro = macros.take(name);
    if (macro == currentMacro)
        currentMacro = nullptr;
    delete macro;
}

void MacroManagerPrivate::changeMacroDescription(Macro *macro, const QString &description)
{
    if (!macro->load())
        return;
    macro->setDescription(description);
    macro->save(macro->fileName());

    QAction *action = actions[macro->displayName()];
    QTC_ASSERT(action, return);
    action->setText(description);
}

bool MacroManagerPrivate::executeMacro(Macro *macro)
{
    bool error = !macro->load();
    const QList<MacroEvent> macroEvents = macro->events();
    for (const MacroEvent &macroEvent : macroEvents) {
        if (error)
            break;
        for (IMacroHandler *handler : std::as_const(handlers)) {
            if (handler->canExecuteEvent(macroEvent)) {
                if (!handler->executeEvent(macroEvent))
                    error = true;
                break;
            }
        }
    }

    if (error) {
        QMessageBox::warning(
            Core::ICore::dialogParent(),
            Tr::tr("Playing Macro"),
            Tr::tr("An error occurred while replaying the macro, execution stopped."));
    }

    // Set the focus back to the editor
    // TODO: is it really needed??
    if (Core::IEditor *current = Core::EditorManager::currentEditor())
        current->widget()->setFocus(Qt::OtherFocusReason);

    return !error;
}

class SaveDialog : public QDialog
{
public:
    SaveDialog()
        : QDialog(Core::ICore::dialogParent())
    {
        resize(219, 91);
        setWindowTitle(Tr::tr("Save Macro"));

        m_name = new QLineEdit;
        m_name->setValidator(new QRegularExpressionValidator(QRegularExpression("\\w+"), this));

        m_description = new QLineEdit;

        auto buttonBox = new QDialogButtonBox;
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Save);
        auto saveButton = buttonBox->button(QDialogButtonBox::Save);
        saveButton->setEnabled(false);
        connect(m_name, &QLineEdit::textChanged, [saveButton, this]() {
            saveButton->setEnabled(m_name->hasAcceptableInput());
        });

        using namespace Layouting;

        Form {
            Tr::tr("Name:"), m_name, br,
            Tr::tr("Description:"), m_description, br,
            buttonBox
        }.attachTo(this);

        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QString name() const { return m_name->text(); }

    QString description() const { return m_description->text(); }

private:
    QLineEdit *m_name;
    QLineEdit *m_description;
};

void MacroManagerPrivate::showSaveDialog()
{
    SaveDialog dialog;
    if (dialog.exec()) {
        if (dialog.name().isEmpty())
            return;

        // Save in the resource path
        const QString fileName = MacroManager::macrosDirectory() + QLatin1Char('/') + dialog.name()
                + QLatin1Char('.') + QLatin1String(Constants::M_EXTENSION);
        currentMacro->setDescription(dialog.description());
        currentMacro->save(fileName);
        addMacro(currentMacro);
    }
}


// ---------- MacroManager ------------
MacroManager *m_instance = nullptr;

MacroManager::MacroManager() :
    d(new MacroManagerPrivate(this))
{
    m_instance = this;
    registerMacroHandler(d->actionHandler);
    registerMacroHandler(d->findHandler);
    registerMacroHandler(d->textEditorHandler);
}

MacroManager::~MacroManager()
{
    // Cleanup macro
    const QStringList macroList = d->macros.keys();
    for (const QString &name : macroList)
        d->removeMacro(name);

    // Cleanup handlers
    qDeleteAll(d->handlers);

    delete d;
}

void MacroManager::startMacro()
{
    d->isRecording = true;
    // Delete anonymous macro
    if (d->currentMacro && d->currentMacro->displayName().isEmpty())
        delete d->currentMacro;
    d->currentMacro = new Macro;

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(false);
    for (IMacroHandler *handler : std::as_const(d->handlers))
        handler->startRecording(d->currentMacro);

    const QString endShortcut = Core::ActionManager::command(Constants::END_MACRO)
                                    ->keySequence()
                                    .toString(QKeySequence::NativeText);
    const QString executeShortcut = Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)
                                        ->keySequence()
                                        .toString(QKeySequence::NativeText);
    const QString help
        = Tr::tr("Macro mode. Type \"%1\" to stop recording and \"%2\" to play the macro.")
              .arg(endShortcut, executeShortcut);
    Core::EditorManager::showEditorStatusBar(Constants::M_STATUS_BUFFER, help,
                                             Tr::tr("Stop Recording Macro"),
                                             this, [this] { endMacro(); });
}

void MacroManager::endMacro()
{
    Core::EditorManager::hideEditorStatusBar(QLatin1String(Constants::M_STATUS_BUFFER));

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);
    for (IMacroHandler *handler : std::as_const(d->handlers))
        handler->endRecordingMacro(d->currentMacro);

    d->isRecording = false;
}

void MacroManager::executeLastMacro()
{
    if (!d->currentMacro)
        return;

    // make sure the macro doesn't accidentally invoke a macro action
    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(false);

    d->executeMacro(d->currentMacro);

    Core::ActionManager::command(Constants::START_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::END_MACRO)->action()->setEnabled(false);
    Core::ActionManager::command(Constants::EXECUTE_LAST_MACRO)->action()->setEnabled(true);
    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);
}

bool MacroManager::executeMacro(const QString &name)
{
    // Don't execute macro while recording
    if (d->isRecording || !d->macros.contains(name))
        return false;

    Macro *macro = d->macros.value(name);
    if (!d->executeMacro(macro))
        return false;

    // Delete anonymous macro
    if (d->currentMacro && d->currentMacro->displayName().isEmpty())
        delete d->currentMacro;
    d->currentMacro = macro;

    Core::ActionManager::command(Constants::SAVE_LAST_MACRO)->action()->setEnabled(true);

    return true;
}

void MacroManager::deleteMacro(const QString &name)
{
    Macro *macro = d->macros.value(name);
    if (macro) {
        QString fileName = macro->fileName();
        d->removeMacro(name);
        QFile::remove(fileName);
    }
}

const QMap<QString,Macro*> &MacroManager::macros()
{
    return m_instance->d->macros;
}

void MacroManager::registerMacroHandler(IMacroHandler *handler)
{
    m_instance->d->handlers.prepend(handler);
}

MacroManager *MacroManager::instance()
{
    return m_instance;
}

void MacroManager::changeMacro(const QString &name, const QString &description)
{
    if (!d->macros.contains(name))
        return;
    Macro *macro = d->macros.value(name);

    // Change description
    if (macro->description() != description)
        d->changeMacroDescription(macro, description);
}

void MacroManager::saveLastMacro()
{
    if (!d->currentMacro->events().isEmpty())
        d->showSaveDialog();
}

QString MacroManager::macrosDirectory()
{
    const QString path = Core::ICore::userResourcePath("macros").toUrlishString();
    if (QFileInfo::exists(path) || QDir().mkpath(path))
        return path;
    return QString();
}

} // Macros::Internal
