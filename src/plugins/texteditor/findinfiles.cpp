// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "findinfiles.h"

#include "texteditortr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/findplugin.h>
#include <coreplugin/icore.h>

#include <utils/historycompleter.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QStackedWidget>

using namespace Core;
using namespace Utils;

namespace TextEditor {

static const char HistoryKey[] = "FindInFiles.Directories.History";

FindInFiles::FindInFiles()
{
    connect(EditorManager::instance(), &EditorManager::findOnFileSystemRequest,
            this, &FindInFiles::findOnFileSystem);
}

FindInFiles::~FindInFiles() = default;

bool FindInFiles::isValid() const
{
    return m_isValid;
}

QString FindInFiles::id() const
{
    return QLatin1String("Files on Disk");
}

QString FindInFiles::displayName() const
{
    return Tr::tr("Files in File System");
}

FileContainerProvider FindInFiles::fileContainerProvider() const
{
    return [nameFilters = fileNameFilters(), exclusionFilters = fileExclusionFilters(),
            filePath = searchDir()] {
        return SubDirFileContainer({filePath}, nameFilters, exclusionFilters,
                                   EditorManager::defaultTextCodec());
    };
}

QString FindInFiles::label() const
{
    QString title = currentSearchEngine()->title();

    const QChar slash = QLatin1Char('/');
    const QStringList &nonEmptyComponents = searchDir().toFileInfo().absoluteFilePath()
            .split(slash, Qt::SkipEmptyParts);
    return Tr::tr("%1 \"%2\":")
            .arg(title)
            .arg(nonEmptyComponents.isEmpty() ? QString(slash) : nonEmptyComponents.last());
}

QString FindInFiles::toolTip() const
{
    //: the last arg is filled by BaseFileFind::runNewSearch
    QString tooltip = Tr::tr("Path: %1\nFilter: %2\nExcluding: %3\n%4")
            .arg(searchDir().toUserOutput())
            .arg(fileNameFilters().join(','))
            .arg(fileExclusionFilters().join(','));

    const QString searchEngineToolTip = currentSearchEngine()->toolTip();
    if (!searchEngineToolTip.isEmpty())
        tooltip = tooltip.arg(searchEngineToolTip);

    return tooltip;
}

void FindInFiles::syncSearchEngineCombo(int selectedSearchEngineIndex)
{
    QTC_ASSERT(m_searchEngineCombo && selectedSearchEngineIndex >= 0
               && selectedSearchEngineIndex < searchEngines().size(), return);

    m_searchEngineCombo->setCurrentIndex(selectedSearchEngineIndex);
}

void FindInFiles::setValid(bool valid)
{
    if (valid == m_isValid)
        return;
    m_isValid = valid;
    emit validChanged(m_isValid);
}

void FindInFiles::searchEnginesSelectionChanged(int index)
{
    setCurrentSearchEngine(index);
    m_searchEngineWidget->setCurrentIndex(index);
}

void FindInFiles::currentEditorChanged(Core::IEditor *editor)
{
    m_currentDirectory->setEnabled(editor && editor->document() && !editor->document()->filePath().isEmpty());
}

QWidget *FindInFiles::createConfigWidget()
{
    if (!m_configWidget) {
        m_configWidget = new QWidget;
        auto gridLayout = new QGridLayout(m_configWidget);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        m_configWidget->setLayout(gridLayout);

        int row = 0;
        auto searchEngineLabel = new QLabel(Tr::tr("Search engine:"));
        gridLayout->addWidget(searchEngineLabel, row, 0, Qt::AlignRight);
        m_searchEngineCombo = new QComboBox;
        connect(m_searchEngineCombo, &QComboBox::currentIndexChanged,
                this, &FindInFiles::searchEnginesSelectionChanged);
        searchEngineLabel->setBuddy(m_searchEngineCombo);
        gridLayout->addWidget(m_searchEngineCombo, row, 1);

        m_searchEngineWidget = new QStackedWidget(m_configWidget);
        const QList<SearchEngine *> searchEngineVector = searchEngines();
        for (const SearchEngine *searchEngine : searchEngineVector) {
            m_searchEngineWidget->addWidget(searchEngine->widget());
            m_searchEngineCombo->addItem(searchEngine->title());
        }
        gridLayout->addWidget(m_searchEngineWidget, row++, 2);

        QLabel *dirLabel = new QLabel(Tr::tr("Director&y:"));
        gridLayout->addWidget(dirLabel, row, 0, Qt::AlignRight);
        m_directory = new PathChooser;
        m_directory->setExpectedKind(PathChooser::ExistingDirectory);
        m_directory->setPromptDialogTitle(Tr::tr("Directory to Search"));
        connect(m_directory.data(), &PathChooser::textChanged, this,
                [this] { setSearchDir(m_directory->filePath()); });
        connect(this, &BaseFileFind::searchDirChanged, m_directory, &PathChooser::setFilePath);
        m_directory->setHistoryCompleter(HistoryKey, /*restoreLastItemFromHistory=*/ true);
        if (!HistoryCompleter::historyExistsFor(HistoryKey)) {
            auto completer = static_cast<HistoryCompleter *>(m_directory->lineEdit()->completer());
            const QStringList legacyHistory = ICore::settings()->value(
                        "Find/FindInFiles/directories").toStringList();
            for (const QString &dir: legacyHistory)
                completer->addEntry(dir);
        }
        m_directory->addButton("Current", this, [this] {
            const IDocument *document = EditorManager::instance()->currentDocument();
            if (!document)
                return;
            m_directory->setFilePath(document->filePath().parentDir());
        });
        m_currentDirectory = m_directory->buttonAtIndex(1);
        auto editorManager = EditorManager::instance();
        connect(editorManager, &EditorManager::currentEditorChanged,
                this, &FindInFiles::currentEditorChanged);
        currentEditorChanged(editorManager->currentEditor());

        dirLabel->setBuddy(m_directory);
        gridLayout->addWidget(m_directory, row++, 1, 1, 2);

        const QList<QPair<QWidget *, QWidget *>> patternWidgets = createPatternWidgets();
        for (const QPair<QWidget *, QWidget *> &p : patternWidgets) {
            gridLayout->addWidget(p.first, row, 0, Qt::AlignRight);
            gridLayout->addWidget(p.second, row, 1, 1, 2);
            ++row;
        }
        m_configWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        // validity
        auto updateValidity = [this] {
            setValid(currentSearchEngine()->isEnabled() && m_directory->isValid());
        };
        connect(this, &BaseFileFind::currentSearchEngineChanged, this, updateValidity);
        for (const SearchEngine *searchEngine : searchEngineVector)
            connect(searchEngine, &SearchEngine::enabledChanged, this, updateValidity);
        connect(m_directory.data(), &PathChooser::validChanged, this, updateValidity);
        updateValidity();
    }
    return m_configWidget;
}

const char kDefaultInclusion[] = "*.cpp,*.h";
const char kDefaultExclusion[] = "*/.git/*,*/.cvs/*,*/.svn/*,*.autosave,*/build/*";

Store FindInFiles::save() const
{
    Store s;
    writeCommonSettings(s, kDefaultInclusion, kDefaultExclusion);
    return s;
}

void FindInFiles::restore(const Utils::Store &s)
{
    readCommonSettings(s, kDefaultInclusion, kDefaultExclusion);
}

QByteArray FindInFiles::settingsKey() const
{
    return "FindInFiles";
}

void FindInFiles::setBaseDirectory(const FilePath &directory)
{
    m_directory->setBaseDirectory(directory);
}

static FindInFiles *s_instance;

FindInFiles &findInFiles()
{
    return *s_instance;
}

void FindInFiles::findOnFileSystem(const QString &path)
{
    const QFileInfo fi(path);
    const QString folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    findInFiles().setSearchDir(FilePath::fromString(folder));
    Find::openFindDialog(&findInFiles());
}

FindInFiles *FindInFiles::instance()
{
    return s_instance;
}

void Internal::setupFindInFiles(QObject *guard)
{
    s_instance = new FindInFiles;
    s_instance->setParent(guard);
}

} // TextEditor
