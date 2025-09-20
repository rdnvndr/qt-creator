// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpptoolssettings.h"

#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppcodestylepreferencesfactory.h"
#include "cppcodestylesettings.h"

#include <coreplugin/icore.h>

#include <extensionsystem/pluginmanager.h>

#include <texteditor/completionsettingspage.h>
#include <texteditor/codestylepool.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>

#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>

static const char idKey[] = "CppGlobal";

using namespace Core;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor {
namespace Internal {

class CppToolsSettingsPrivate
{
public:
    CppCodeStylePreferences *m_globalCodeStyle = nullptr;
};

} // Internal

Internal::CppToolsSettingsPrivate *d = nullptr;

CppToolsSettings::CppToolsSettings()
{
    d = new Internal::CppToolsSettingsPrivate;

    qRegisterMetaType<CppCodeStyleSettings>("CppEditor::CppCodeStyleSettings");

    // code style factory
    ICodeStylePreferencesFactory *factory = createCppCodeStylePreferencesFactory();
    TextEditorSettings::registerCodeStyleFactory(factory);

    // code style pool
    auto pool = new CodeStylePool(factory, this);
    TextEditorSettings::registerCodeStylePool(Constants::CPP_SETTINGS_ID, pool);

    // global code style settings
    d->m_globalCodeStyle = new CppCodeStylePreferences(this);
    d->m_globalCodeStyle->setDelegatingPool(pool);
    d->m_globalCodeStyle->setDisplayName(Tr::tr("Global", "Settings"));
    d->m_globalCodeStyle->setId(idKey);
    pool->addCodeStyle(d->m_globalCodeStyle);
    TextEditorSettings::registerCodeStyle(Constants::CPP_SETTINGS_ID, d->m_globalCodeStyle);

    /*
    For every language we have exactly 1 pool. The pool contains:
    1) All built-in code styles (Qt/GNU)
    2) All custom code styles (which will be added dynamically)
    3) A global code style

    If the code style gets a pool (setCodeStylePool()) it means it can behave
    like a proxy to one of the code styles from that pool
    (ICodeStylePreferences::setCurrentDelegate()).
    That's why the global code style gets a pool (it can point to any code style
    from the pool), while built-in and custom code styles don't get a pool
    (they can't point to any other code style).

    The instance of the language pool is shared. The same instance of the pool
    is used for all project code style settings and for global one.
    Project code style can point to one of built-in or custom code styles
    or to the global one as well. That's why the global code style is added
    to the pool. The proxy chain can look like:
    ProjectCodeStyle -> GlobalCodeStyle -> BuildInCodeStyle (e.g. Qt).

    With the global pool there is an exception - it gets a pool
    in which it exists itself. The case in which a code style point to itself
    is disallowed and is handled in ICodeStylePreferences::setCurrentDelegate().
    */

    // built-in settings
    // Qt style
    auto qtCodeStyle = new CppCodeStylePreferences;
    qtCodeStyle->setId("qt");
    qtCodeStyle->setDisplayName(Tr::tr("Qt"));
    qtCodeStyle->setReadOnly(true);
    TabSettings qtTabSettings;
    qtTabSettings.m_tabPolicy = TabSettings::SpacesOnlyTabPolicy;
    qtTabSettings.m_tabSize = 4;
    qtTabSettings.m_indentSize = 4;
    qtTabSettings.m_continuationAlignBehavior = TabSettings::ContinuationAlignWithIndent;
    qtTabSettings.m_autoDetect = false;
    qtCodeStyle->setTabSettings(qtTabSettings);
    pool->addCodeStyle(qtCodeStyle);

    // GNU style
    auto gnuCodeStyle = new CppCodeStylePreferences;
    gnuCodeStyle->setId("gnu");
    gnuCodeStyle->setDisplayName(Tr::tr("GNU"));
    gnuCodeStyle->setReadOnly(true);
    TabSettings gnuTabSettings;
    gnuTabSettings.m_tabPolicy = TabSettings::TabsOnlyTabPolicy;
    gnuTabSettings.m_tabSize = 2;
    gnuTabSettings.m_indentSize = 2;
    gnuTabSettings.m_autoDetect = false;
    gnuTabSettings.m_continuationAlignBehavior = TabSettings::ContinuationAlignWithIndent;
    gnuCodeStyle->setTabSettings(gnuTabSettings);
    CppCodeStyleSettings gnuCodeStyleSettings;
    gnuCodeStyleSettings.indentNamespaceBody = true;
    gnuCodeStyleSettings.indentBlockBraces = true;
    gnuCodeStyleSettings.indentSwitchLabels = true;
    gnuCodeStyleSettings.indentBlocksRelativeToSwitchLabels = true;
    gnuCodeStyle->setCodeStyleSettings(gnuCodeStyleSettings);
    pool->addCodeStyle(gnuCodeStyle);

    // default delegate for global preferences
    d->m_globalCodeStyle->setCurrentDelegate(qtCodeStyle);

    pool->loadCustomCodeStyles();

    // load global settings (after built-in settings are added to the pool)
    d->m_globalCodeStyle->fromSettings(Constants::CPP_SETTINGS_ID);

    // mimetypes to be handled
    using namespace Utils::Constants;
    TextEditorSettings::registerMimeTypeForLanguageId(C_SOURCE_MIMETYPE, Constants::CPP_SETTINGS_ID);
    TextEditorSettings::registerMimeTypeForLanguageId(C_HEADER_MIMETYPE, Constants::CPP_SETTINGS_ID);
    TextEditorSettings::registerMimeTypeForLanguageId(CPP_SOURCE_MIMETYPE, Constants::CPP_SETTINGS_ID);
    TextEditorSettings::registerMimeTypeForLanguageId(CPP_HEADER_MIMETYPE, Constants::CPP_SETTINGS_ID);
}

CppToolsSettings::~CppToolsSettings()
{
    TextEditorSettings::unregisterCodeStyle(Constants::CPP_SETTINGS_ID);
    TextEditorSettings::unregisterCodeStylePool(Constants::CPP_SETTINGS_ID);
    TextEditorSettings::unregisterCodeStyleFactory(Constants::CPP_SETTINGS_ID);

    delete d;
}

CppCodeStylePreferences *CppToolsSettings::cppCodeStyle()
{
    return d->m_globalCodeStyle;
}

} // namespace CppEditor
