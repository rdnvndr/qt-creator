// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpp/formclasswizard.h"
#include "designerconstants.h"
#include "designertr.h"
#include "formeditorfactory.h"
#include "formeditor.h"
#include "formtemplatewizardpage.h"
#include "qtdesignerformclasscodegenerator.h"
#include "settingspage.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/coreplugintr.h>
#include <coreplugin/designmode.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>

#include <cppeditor/cppeditorconstants.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/jsonwizard/jsonwizardfactory.h>

#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QMenu>
#include <QTranslator>

using namespace Core;
using namespace Designer::Constants;
using namespace Utils;

namespace Designer::Internal {

// Find out current existing editor file
static FilePath currentFile()
{
    if (const IDocument *document = EditorManager::currentDocument()) {
        const FilePath filePath = document->filePath();
        if (!filePath.isEmpty() && filePath.isFile())
            return filePath;
    }
    return {};
}

// Switch between form ('ui') and source file ('cpp'):
// Find corresponding 'other' file, simply assuming it is in the same directory.
static FilePath otherFile()
{
    // Determine mime type of current file.
    const FilePath current = currentFile();
    if (current.isEmpty())
        return {};
    const Utils::MimeType currentMimeType = Utils::mimeTypeForFile(current);
    // Determine potential suffixes of candidate files
    // 'ui' -> 'cpp', 'cpp/h' -> 'ui'.
    using namespace Utils::Constants;
    QStringList candidateSuffixes;
    if (currentMimeType.matchesName(FORM_MIMETYPE)) {
        candidateSuffixes += Utils::mimeTypeForName(CPP_SOURCE_MIMETYPE).suffixes();
    } else if (currentMimeType.matchesName(CPP_SOURCE_MIMETYPE)
               || currentMimeType.matchesName(CPP_HEADER_MIMETYPE)) {
        candidateSuffixes += Utils::mimeTypeForName(FORM_MIMETYPE).suffixes();
    } else {
        return {};
    }
    // Try to find existing file with desired suffix
    const FilePath currentBaseName = current.parentDir().pathAppended(current.baseName() + '.');
    for (const QString &candidateSuffix : std::as_const(candidateSuffixes)) {
        const FilePath filePath = currentBaseName.stringAppended(candidateSuffix);
        if (filePath.isFile())
            return filePath.absoluteFilePath();
    }
    return {};
}

static void parseArguments(const QStringList &arguments)
{
    const auto doWithNext = [arguments](auto it, const std::function<void(QString)> &fun) {
        ++it;
        if (it != arguments.cend())
            fun(*it);
    };
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        if (*it == "-designer-qt-pluginpath")
            doWithNext(it, [](const QString &path) { setQtPluginPath(path); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        // -designer-pluginpath is only supported when building with Qt >= 6.7.0, which added the
        // required API
        else if (*it == "-designer-pluginpath")
            doWithNext(it, [](const QString &path) { addPluginPath(path); });
#endif
    }
}

class FormEditorPluginPrivate
{
public:
    QAction actionSwitchSource{Tr::tr("Switch Source/Form"), nullptr};

    FormEditorFactory formEditorFactory;
    SettingsPageProvider settingsPageProvider;
    QtDesignerFormClassCodeGenerator formClassCodeGenerator;
    FormPageFactory formPageFactory;
};

class DesignerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Designer.json")

    ~DesignerPlugin() final
    {
        deleteInstance();
        delete d;
    }

    Result<> initialize(const QStringList &arguments) final
    {
        d = new FormEditorPluginPrivate;

        IWizardFactory::registerFactoryCreator([]() -> IWizardFactory * {
            IWizardFactory *wizard = new FormClassWizard;
            wizard->setCategory(Core::Constants::WIZARD_CATEGORY_QT);
            wizard->setDisplayCategory(::Core::Tr::tr(Core::Constants::WIZARD_TR_CATEGORY_QT));
            wizard->setDisplayName(Tr::tr("Qt Widgets Designer Form Class"));
            wizard->setIcon({}, "ui/h");
            wizard->setId("C.FormClass");
            wizard->setDescription(Tr::tr("Creates a Qt Widgets Designer form along with a matching "
                                          "class (C++ header and source file) "
                                          "for implementation purposes. You can add the form and "
                                          "class to an existing Qt Widget Project."));

            return wizard;
        });

        // Ensure that loading designer translations is done before FormEditorW is instantiated
        const QString locale = ICore::userInterfaceLanguage();
        if (!locale.isEmpty()) {
            auto qtr = new QTranslator(this);
            const QString creatorTrPath = ICore::resourcePath("translations").toUrlishString();
            const QString qtTrPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
            const QString trFile = "designer_" + locale;
            if (qtr->load(trFile, qtTrPath) || qtr->load(trFile, creatorTrPath))
                QCoreApplication::installTranslator(qtr);
        }

#ifdef WITH_TESTS
        addTestCreator(createGoToSlotTest);
#endif

        parseArguments(arguments);
        return ResultOk;
    }

    void extensionsInitialized() final
    {
        DesignMode::setDesignModeIsRequired();
        // 4) test and make sure everything works (undo, saving, editors, opening/closing multiple files, dirtiness etc)

        ActionContainer *mtools = ActionManager::actionContainer(Core::Constants::M_TOOLS);
        ActionContainer *mformtools = ActionManager::createMenu(M_FORMEDITOR);
        mformtools->menu()->setTitle(Tr::tr("For&m Editor"));
        mtools->addMenu(mformtools);

        connect(&d->actionSwitchSource, &QAction::triggered, this, &DesignerPlugin::switchSourceForm);
        Context context(C_FORMEDITOR, Core::Constants::C_EDITORMANAGER);
        Command *cmd = ActionManager::registerAction(&d->actionSwitchSource,
                                                     "FormEditor.FormSwitchSource", context);
        cmd->setDefaultKeySequence(Tr::tr("Shift+F4"));
        mformtools->addAction(cmd, Core::Constants::G_DEFAULT_THREE);
    }

    void switchSourceForm()
    {
        const FilePath fileToOpen = otherFile();
        if (!fileToOpen.isEmpty())
            EditorManager::openEditor(fileToOpen);
    }

    FormEditorPluginPrivate *d = nullptr;
};

} // Designer::Internal

#include "designerplugin.moc"
