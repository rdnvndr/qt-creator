// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatglobalconfigwidget.h"
#include "clangformatconfigwidget.h"
#include "clangformatconstants.h"
#include "clangformatfile.h"
#include "clangformatsettings.h"
#include "clangformattr.h"
#include "clangformatutils.h"

#include <cppeditor/cppcodestylepreferencesfactory.h>
#include <cppeditor/cppcodestylesettingspage.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>
#include <texteditor/codestylepool.h>
#include <texteditor/codestyleselectorwidget.h>
#include <texteditor/texteditorsettings.h>
#include <utils/fileutils.h>
#include <utils/layoutbuilder.h>

#include <QCheckBox>
#include <QGroupBox>
#include <QSpinBox>

using namespace CppEditor;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace ClangFormat {

ClangFormatGlobalConfigWidget::ClangFormatGlobalConfigWidget(
    Project *project, ICodeStylePreferences *codeStyle, QWidget *parent)
    : CodeStyleEditorWidget(parent)
    , m_project(project)
    , m_codeStyle(codeStyle)
{
    const QString sizeThresholdToolTip = Tr::tr(
        "Files greater than this will not be indented by ClangFormat.\n"
        "The built-in code indenter will handle indentation.");

    m_projectHasClangFormat = new QLabel(this);
    m_formattingModeLabel = new QLabel(Tr::tr("Formatting mode:"));
    m_fileSizeThresholdLabel = new QLabel(Tr::tr("Ignore files greater than:"));
    m_fileSizeThresholdLabel->setToolTip(sizeThresholdToolTip);
    m_fileSizeThresholdSpinBox = new QSpinBox(this);
    m_fileSizeThresholdSpinBox->setToolTip(sizeThresholdToolTip);
    m_indentingOrFormatting = new QComboBox(this);
    m_formatWhileTyping = new QCheckBox(Tr::tr("Format while typing"));
    m_formatOnSave = new QCheckBox(Tr::tr("Format edited code on file save"));
    m_useCustomSettingsCheckBox = new QCheckBox(Tr::tr("Use custom settings"));
    m_useGlobalSettings = new QCheckBox(Tr::tr("Use global settings"));
    m_useGlobalSettings->hide();
    m_useCustomSettings = ClangFormatSettings::instance().useCustomSettings();

    m_currentProjectLabel = new Utils::InfoLabel(
        Tr::tr("Please note that the current project includes a .clang-format file, which will be "
               "used for code indenting and formatting."),
        Utils::InfoLabel::Warning);
    m_currentProjectLabel->setWordWrap(true);

    using namespace Layouting;

    QWidget *globalSettingsGroupBoxWidget = nullptr;

    // clang-format off
    Group globalSettingsGroupBox {
        bindTo(&globalSettingsGroupBoxWidget),
        title(Tr::tr("ClangFormat Settings")),
        Column {
            m_useGlobalSettings,
            Form {
                 m_formattingModeLabel, m_indentingOrFormatting, st, br,
                 m_fileSizeThresholdLabel, m_fileSizeThresholdSpinBox, st, br
            },
            m_formatWhileTyping,
            m_formatOnSave,
            m_projectHasClangFormat,
            m_useCustomSettingsCheckBox,
            m_currentProjectLabel
        }
    };

    Column {
        globalSettingsGroupBox,
        noMargin
    }.attachTo(this);
    // clang-format on

    initCheckBoxes();
    initIndentationOrFormattingCombobox();
    initCustomSettingsCheckBox();
    initUseGlobalSettingsCheckBox();
    initFileSizeThresholdSpinBox();
    initCurrentProjectLabel();

    if (project) {
        m_formatOnSave->hide();
        m_formatWhileTyping->hide();

        m_useGlobalSettings->show();
        return;
    }

    globalSettingsGroupBoxWidget->show();
}

void ClangFormatGlobalConfigWidget::initCheckBoxes()
{
    auto setEnableCheckBoxes = [this](int index) {
        bool isFormatting = index == static_cast<int>(ClangFormatSettings::Mode::Formatting);

        m_formatOnSave->setEnabled(isFormatting);
        m_formatWhileTyping->setEnabled(isFormatting);
    };
    setEnableCheckBoxes(m_indentingOrFormatting->currentIndex());
    connect(m_indentingOrFormatting, &QComboBox::currentIndexChanged, this, setEnableCheckBoxes);

    m_formatOnSave->setChecked(ClangFormatSettings::instance().formatOnSave());
    m_formatWhileTyping->setChecked(ClangFormatSettings::instance().formatWhileTyping());
}

void ClangFormatGlobalConfigWidget::initIndentationOrFormattingCombobox()
{
    m_indentingOrFormatting->insertItem(
        static_cast<int>(ClangFormatSettings::Mode::Indenting), Tr::tr("Indenting only"));
    m_indentingOrFormatting->insertItem(
        static_cast<int>(ClangFormatSettings::Mode::Formatting), Tr::tr("Full formatting"));
    m_indentingOrFormatting->insertItem(
        static_cast<int>(ClangFormatSettings::Mode::Disable), Tr::tr("Use built-in indenter"));

    m_indentingOrFormatting->setCurrentIndex(
        static_cast<int>(getProjectIndentationOrFormattingSettings(m_project)));

    connect(m_indentingOrFormatting, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_project)
            m_project->setNamedSettings(Constants::MODE_ID, index);

        emit modeChanged(static_cast<ClangFormatSettings::Mode>(index));
    });
}

void ClangFormatGlobalConfigWidget::initUseGlobalSettingsCheckBox()
{
    if (!m_project)
        return;

    const auto enableProjectSettings = [this] {
        const bool isDisabled = m_project && m_useGlobalSettings->isChecked();
        m_indentingOrFormatting->setDisabled(isDisabled);
        m_formattingModeLabel->setDisabled(isDisabled);
        const auto currentMode = static_cast<ClangFormatSettings::Mode>(
            m_indentingOrFormatting->currentIndex());
        m_projectHasClangFormat->setDisabled(
            isDisabled || (currentMode == ClangFormatSettings::Mode::Disable));
        m_useCustomSettingsCheckBox->setChecked(getProjectCustomSettings(m_project));
        m_useCustomSettingsCheckBox->setDisabled(
            isDisabled || (currentMode == ClangFormatSettings::Mode::Disable));

        emit m_codeStyle->currentPreferencesChanged(m_codeStyle->currentPreferences());
    };

    m_useGlobalSettings->setChecked(getProjectUseGlobalSettings(m_project));
    enableProjectSettings();

    connect(
        m_useGlobalSettings, &QCheckBox::toggled, this, [this, enableProjectSettings](bool checked) {
            m_project->setNamedSettings(Constants::USE_GLOBAL_SETTINGS, checked);
            enableProjectSettings();
        });
}

void ClangFormatGlobalConfigWidget::initFileSizeThresholdSpinBox()
{
    m_fileSizeThresholdSpinBox->setMinimum(1);
    m_fileSizeThresholdSpinBox->setMaximum(std::numeric_limits<int>::max());
    m_fileSizeThresholdSpinBox->setSuffix(" KB");
    m_fileSizeThresholdSpinBox->setValue(ClangFormatSettings::instance().fileSizeThreshold());
    if (m_project) {
        m_fileSizeThresholdSpinBox->hide();
        m_fileSizeThresholdLabel->hide();
    }

    connect(m_indentingOrFormatting, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_fileSizeThresholdLabel->setEnabled(
            index != static_cast<int>(ClangFormatSettings::Mode::Disable));
        m_fileSizeThresholdSpinBox->setEnabled(
            index != static_cast<int>(ClangFormatSettings::Mode::Disable));
    });
}

void ClangFormatGlobalConfigWidget::initCurrentProjectLabel()
{
    auto setCurrentProjectLabelVisible = [this]() {
        ProjectExplorer::Project *currentProject
            = m_project ? m_project : ProjectExplorer::ProjectTree::currentProject();

        if (currentProject) {
            Utils::FilePath settingsFilePath = currentProject->projectDirectory()
                                               / Constants::SETTINGS_FILE_NAME;
            Utils::FilePath settingsAltFilePath = currentProject->projectDirectory()
                                                  / Constants::SETTINGS_FILE_ALT_NAME;

            if ((settingsFilePath.exists() || settingsAltFilePath.exists())
                && m_useCustomSettingsCheckBox->checkState() == Qt::CheckState::Unchecked) {
                m_currentProjectLabel->show();
                return;
            }
        }
        m_currentProjectLabel->hide();
    };
    setCurrentProjectLabelVisible();
    connect(m_useCustomSettingsCheckBox, &QCheckBox::toggled, this, setCurrentProjectLabelVisible);
}

bool ClangFormatGlobalConfigWidget::projectClangFormatFileExists()
{
    llvm::Expected<clang::format::FormatStyle> styleFromProjectFolder = clang::format::getStyle(
        "file", m_project->projectFilePath().path().toStdString(), "none", "", nullptr, true);

    return styleFromProjectFolder && !(*styleFromProjectFolder == clang::format::getNoStyle());
}

void ClangFormatGlobalConfigWidget::initCustomSettingsCheckBox()
{
    if (!m_project || !projectClangFormatFileExists()) {
        m_projectHasClangFormat->hide();
    } else {
        m_projectHasClangFormat->show();
        m_projectHasClangFormat->setText(
            Tr::tr("The current project has its own .clang-format file which "
                   "can be overridden by the settings below."));
    }

    auto setEnableCustomSettingsCheckBox = [this](int index) {
        bool isDisable = index == static_cast<int>(ClangFormatSettings::Mode::Disable);
        m_useCustomSettingsCheckBox->setDisabled(isDisable);
        m_projectHasClangFormat->setDisabled(isDisable);
    };

    m_useCustomSettingsCheckBox->setChecked(getProjectCustomSettings(m_project));
    m_useCustomSettingsCheckBox->setToolTip(
        "<html>"
        + Tr::tr("When this option is enabled, ClangFormat will use a "
                 "user-specified configuration from the widget below, "
                 "instead of the project .clang-format file. You can "
                 "customize the formatting options for your code by "
                 "adjusting the settings in the widget. Note that any "
                 "changes made there will only affect the current "
                 "configuration, and will not modify the project "
                 ".clang-format file."));

    setEnableCustomSettingsCheckBox(m_indentingOrFormatting->currentIndex());
    connect(
        m_indentingOrFormatting,
        &QComboBox::currentIndexChanged,
        this,
        setEnableCustomSettingsCheckBox);

    connect(m_useCustomSettingsCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_project) {
            m_project->setNamedSettings(Constants::USE_CUSTOM_SETTINGS_ID, checked);
        } else {
            ClangFormatSettings::instance().setUseCustomSettings(checked);
        }

        emit useCustomSettingsChanged(checked);
    });
}

void ClangFormatGlobalConfigWidget::apply()
{
    ClangFormatSettings &settings = ClangFormatSettings::instance();
    settings.setFormatOnSave(m_formatOnSave->isChecked());
    settings.setFormatWhileTyping(m_formatWhileTyping->isChecked());
    if (!m_project) {
        settings.setMode(
            static_cast<ClangFormatSettings::Mode>(m_indentingOrFormatting->currentIndex()));
        settings.setUseCustomSettings(m_useCustomSettingsCheckBox->isChecked());
        settings.setFileSizeThreshold(m_fileSizeThresholdSpinBox->value());
        m_useCustomSettings = m_useCustomSettingsCheckBox->isChecked();
    }
    settings.write();
}

void ClangFormatGlobalConfigWidget::finish()
{
    ClangFormatSettings::instance().setUseCustomSettings(m_useCustomSettings);
}

ClangFormatSettings::Mode ClangFormatGlobalConfigWidget::mode() const
{
    return static_cast<ClangFormatSettings::Mode>(m_indentingOrFormatting->currentIndex());
}

bool ClangFormatGlobalConfigWidget::useCustomSettings() const
{
    return m_useCustomSettingsCheckBox->isChecked();
}

} // namespace ClangFormat
