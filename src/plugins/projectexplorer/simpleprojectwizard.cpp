// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "simpleprojectwizard.h"

#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <coreplugin/basefilewizard.h>
#include <coreplugin/icore.h>

#include <cmakeprojectmanager/cmakeprojectconstants.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/customwizard/customwizard.h>
#include <projectexplorer/selectablefilesmodel.h>
#include <qmakeprojectmanager/qmakeprojectmanagerconstants.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/filewizardpage.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/wizard.h>

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>
#include <QWizardPage>

using namespace Core;
using namespace Utils;

namespace ProjectExplorer::Internal {

class SimpleProjectWizardDialog;

class FilesSelectionWizardPage : public QWizardPage
{
    Q_OBJECT

public:
    FilesSelectionWizardPage(SimpleProjectWizardDialog *simpleProjectWizard);
    bool isComplete() const override { return m_filesWidget->hasFilesSelected(); }
    void initializePage() override;
    void cleanupPage() override { m_filesWidget->cancelParsing(); }
    FilePaths selectedFiles() const { return m_filesWidget->selectedFiles(); }
    FilePaths selectedPaths() const { return m_filesWidget->selectedPaths(); }
    QString qtModules() const { return m_qtModules; }
    QString buildSystem() const { return m_buildSystem; }

private:
    SimpleProjectWizardDialog *m_simpleProjectWizardDialog;
    SelectableFilesWidget *m_filesWidget;
    QString m_qtModules;
    QString m_buildSystem;
};

FilesSelectionWizardPage::FilesSelectionWizardPage(SimpleProjectWizardDialog *simpleProjectWizard)
    : m_simpleProjectWizardDialog(simpleProjectWizard),
      m_filesWidget(new SelectableFilesWidget(this))
{
    auto layout = new QVBoxLayout(this);
    {
        auto hlayout = new QHBoxLayout;
        hlayout->addWidget(new QLabel("Qt modules", this));
        auto lineEdit = new QLineEdit("core gui widgets", this);
        connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit]{
            m_qtModules = lineEdit->text();
        });
        m_qtModules = lineEdit->text();
        hlayout->addWidget(lineEdit);
        layout->addLayout(hlayout);
    }

    {
        auto hlayout = new QHBoxLayout;
        hlayout->addWidget(new QLabel("Build system", this));
        auto comboBox = new QComboBox(this);
        connect(comboBox, &QComboBox::currentTextChanged, this, [this](const QString &bs){
            m_buildSystem = bs;
        });
        comboBox->addItems(QStringList() << "qmake" << "cmake");
        comboBox->setEditable(false);
        comboBox->setCurrentText("qmake");
        hlayout->addWidget(comboBox);
        layout->addLayout(hlayout);
    }

    layout->addWidget(m_filesWidget);
    m_filesWidget->setBaseDirEditable(false);
    m_filesWidget->enableFilterHistoryCompletion
            (ProjectExplorer::Constants::ADD_FILES_DIALOG_FILTER_HISTORY_KEY);
    connect(m_filesWidget, &SelectableFilesWidget::selectedFilesChanged,
            this, &FilesSelectionWizardPage::completeChanged);

    setProperty(Utils::SHORT_TITLE_PROPERTY, Tr::tr("Files"));
}

class SimpleProjectWizardDialog : public BaseFileWizard
{
    Q_OBJECT

public:
    explicit SimpleProjectWizardDialog(const BaseFileWizardFactory *factory)
        : BaseFileWizard(factory, QVariantMap())
    {
        setWindowTitle(Tr::tr("Import Existing Project"));

        m_firstPage = new FileWizardPage;
        m_firstPage->setTitle(Tr::tr("Project Name and Location"));
        m_firstPage->setFileNameLabel(Tr::tr("Project name:"));
        m_firstPage->setPathLabel(Tr::tr("Location:"));
        addPage(m_firstPage);

        m_secondPage = new FilesSelectionWizardPage(this);
        m_secondPage->setTitle(Tr::tr("File Selection"));
        addPage(m_secondPage);
    }

    Utils::FilePath projectDir() const { return m_firstPage->filePath(); }
    void setProjectDir(const Utils::FilePath &path) { m_firstPage->setFilePath(path); }
    FilePaths selectedFiles() const { return m_secondPage->selectedFiles(); }
    FilePaths selectedPaths() const { return m_secondPage->selectedPaths(); }
    QString qtModules() const { return m_secondPage->qtModules(); }
    QString buildSystem() const { return m_secondPage->buildSystem(); }
    QString projectName() const { return m_firstPage->fileName(); }

    FileWizardPage *m_firstPage;
    FilesSelectionWizardPage *m_secondPage;
};

void FilesSelectionWizardPage::initializePage()
{
    m_filesWidget->resetModel(m_simpleProjectWizardDialog->projectDir(), FilePaths());
}

SimpleProjectWizard::SimpleProjectWizard()
{
    setSupportedProjectTypes({QmakeProjectManager::Constants::QMAKEPROJECT_ID,
                              CMakeProjectManager::Constants::CMAKE_PROJECT_ID});
    setIcon(ProjectExplorer::Icons::WIZARD_IMPORT_AS_PROJECT.icon());
    setDisplayName(Tr::tr("Import as qmake or CMake Project (Limited Functionality)"));
    setId("Z.DummyProFile");
    setDescription(
        Tr::tr(
            "Imports existing projects that do not use qmake, CMake, Qbs, Meson, or Autotools.<p>"
            "This creates a project file that allows you to use %1 as a code editor "
            "and as a launcher for debugging and analyzing tools. "
            "If you want to build the project, you might need to edit the generated project file.")
            .arg(QGuiApplication::applicationDisplayName()));
    setCategory(ProjectExplorer::Constants::IMPORT_WIZARD_CATEGORY);
    setDisplayCategory(Tr::tr(ProjectExplorer::Constants::IMPORT_WIZARD_CATEGORY_DISPLAY));
    setFlags(IWizardFactory::PlatformIndependent);
}

BaseFileWizard *SimpleProjectWizard::create(const WizardDialogParameters &parameters) const
{
    auto wizard = new SimpleProjectWizardDialog(this);
    wizard->setProjectDir(parameters.defaultPath());

    for (QWizardPage *p : wizard->extensionPages())
        wizard->addPage(p);

    return wizard;
}

GeneratedFiles generateQmakeFiles(const SimpleProjectWizardDialog *wizard,
                                  QString *errorMessage)
{
    Q_UNUSED(errorMessage)
    const QString projectPath = wizard->projectDir().toUrlishString();
    const QDir dir(projectPath);
    const QString projectName = wizard->projectName();
    const FilePath proFileName = Utils::FilePath::fromString(QFileInfo(dir, projectName + ".pro").absoluteFilePath());
    const QStringList paths = Utils::transform(wizard->selectedPaths(), &FilePath::toUrlishString);

    MimeType headerType = Utils::mimeTypeForName(Utils::Constants::C_HEADER_MIMETYPE);

    QStringList nameFilters = headerType.globPatterns();

    QString proIncludes = "INCLUDEPATH = \\\n";
    for (const QString &path : paths) {
        QFileInfo fileInfo(path);
        QDir thisDir(fileInfo.absoluteFilePath());
        if (!thisDir.entryList(nameFilters, QDir::Files).isEmpty()) {
            QString relative = dir.relativeFilePath(path);
            if (!relative.isEmpty())
                proIncludes.append("    $$PWD/" + relative + " \\\n");
        }
    }

    QString proSources = "SOURCES = \\\n";
    QString proHeaders = "HEADERS = \\\n";

    for (const FilePath &fileName : wizard->selectedFiles()) {
        QString source = dir.relativeFilePath(fileName.toUrlishString());
        MimeType mimeType = Utils::mimeTypeForFile(fileName);
        if (mimeType.matchesName(Utils::Constants::C_HEADER_MIMETYPE)
            || mimeType.matchesName(Utils::Constants::CPP_HEADER_MIMETYPE))
            proHeaders += "   $$PWD/" + source + " \\\n";
        else
            proSources += "   $$PWD/" + source + " \\\n";
    }

    proHeaders.chop(3);
    proSources.chop(3);
    proIncludes.chop(3);

    GeneratedFile generatedProFile(proFileName);
    generatedProFile.setAttributes(Core::GeneratedFile::OpenProjectAttribute);
    generatedProFile.setContents(
        "# Created by and for " + QGuiApplication::applicationDisplayName()
        + " This file was created for editing the project sources only.\n"
          "# You may attempt to use it for building too, by modifying this file here.\n\n"
          "#TARGET = "
        + projectName
        + "\n\n"
          "QT = "
        + wizard->qtModules() + "\n\n" + proHeaders + "\n\n" + proSources + "\n\n" + proIncludes
        + "\n\n"
          "#DEFINES = \n\n");

    return GeneratedFiles{generatedProFile};
}

GeneratedFiles generateCmakeFiles(const SimpleProjectWizardDialog *wizard,
                                  QString *errorMessage)
{
    Q_UNUSED(errorMessage)
    const QDir dir(wizard->projectDir().toUrlishString());
    const QString projectName = wizard->projectName();
    const FilePath projectFileName = Utils::FilePath::fromString(QFileInfo(dir, "CMakeLists.txt").absoluteFilePath());
    const QStringList paths = Utils::transform(wizard->selectedPaths(), &FilePath::toUrlishString);

    MimeType headerType = Utils::mimeTypeForName(Utils::Constants::C_HEADER_MIMETYPE);

    QStringList nameFilters = headerType.globPatterns();

    QString includes = "include_directories(\n";
    bool haveIncludes = false;
    for (const QString &path : paths) {
        QFileInfo fileInfo(path);
        QDir thisDir(fileInfo.absoluteFilePath());
        if (!thisDir.entryList(nameFilters, QDir::Files).isEmpty()) {
            QString relative = dir.relativeFilePath(path);
            if (!relative.isEmpty()) {
                includes.append("    " + relative + "\n");
                haveIncludes = true;
            }
        }
    }
    if (haveIncludes)
        includes += ")";
    else
        includes.clear();

    QString srcs = "set (SRCS\n";
    for (const FilePath &fileName : wizard->selectedFiles())
        srcs += "    " + dir.relativeFilePath(fileName.toUrlishString()) + "\n";
    srcs += ")\n";

    QString components = "find_package(Qt5 COMPONENTS";
    QString libs = "target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE";
    bool haveQtModules = false;
    for (QString c : wizard->qtModules().split(' ')) {
        if (c.isEmpty())
            continue;
        c[0] = c[0].toUpper();
        libs += " Qt5::" + c;
        components += " " + c;
        haveQtModules = true;
    }
    if (haveQtModules) {
        libs += ")\n";
        components += " REQUIRED)";
    } else {
        libs.clear();
        components.clear();
    }


    GeneratedFile generatedProFile(projectFileName);
    generatedProFile.setAttributes(Core::GeneratedFile::OpenProjectAttribute);
    generatedProFile.setContents(
        "# Created by and for " + QGuiApplication::applicationDisplayName()
        + " This file was created for editing the project sources only.\n"
          "# You may attempt to use it for building too, by modifying this file here.\n\n"
          "cmake_minimum_required(VERSION 3.5)\n"
          "project("
        + projectName
        + ")\n\n"
          "set(CMAKE_AUTOUIC ON)\n"
          "set(CMAKE_AUTOMOC ON)\n"
          "set(CMAKE_AUTORCC ON)\n"
          "set(CMAKE_CXX_STANDARD 11)\n"
          "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        + components + "\n\n" + includes + "\n\n" + srcs
        + "\n\n"
          "add_executable(${CMAKE_PROJECT_NAME} ${SRCS})\n\n"
        + libs);
    return GeneratedFiles{generatedProFile};
}

GeneratedFiles SimpleProjectWizard::generateFiles(const QWizard *w,
                                                  QString *errorMessage) const
{
    Q_UNUSED(errorMessage)

    auto wizard = qobject_cast<const SimpleProjectWizardDialog *>(w);
    if (wizard->buildSystem() == "qmake")
        return generateQmakeFiles(wizard, errorMessage);
    else if (wizard->buildSystem() == "cmake")
        return generateCmakeFiles(wizard, errorMessage);

    if (errorMessage)
        *errorMessage = Tr::tr("Unknown build system \"%1\"").arg(wizard->buildSystem());
    return {};
}

Result<> SimpleProjectWizard::postGenerateFiles(const QWizard *w, const GeneratedFiles &l) const
{
    Q_UNUSED(w)
    return CustomProjectWizard::postGenerateOpen(l);
}

} // namespace ProjectExplorer::Internal

#include "simpleprojectwizard.moc"
