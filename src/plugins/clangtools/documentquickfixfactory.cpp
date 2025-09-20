// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "documentquickfixfactory.h"

#include "clangfixitsrefactoringchanges.h"
#include "clangtoolsdiagnostic.h"
#include "documentclangtoolrunner.h"

#include <texteditor/refactoringchanges.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>

using namespace Utils;

namespace ClangTools {
namespace Internal {

class ClangToolQuickFixOperation : public TextEditor::QuickFixOperation
{
public:
    explicit ClangToolQuickFixOperation(const Diagnostic &diagnostic)
        : m_diagnostic(diagnostic)
    {}

    QString description() const override { return m_diagnostic.description; }
    void perform() override;

private:
    const Diagnostic m_diagnostic;
};

using Range = TextEditor::RefactoringFile::Range;
using DiagnosticRange = QPair<Link, Link>;

static Range toRange(const QTextDocument *doc, DiagnosticRange locations)
{
    Range range;
    range.start = Text::positionInText(doc, locations.first.targetLine, locations.first.targetColumn);
    range.end = Text::positionInText(doc, locations.second.targetLine, locations.second.targetColumn);
    return range;
}

void ClangToolQuickFixOperation::perform()
{
    TextEditor::PlainRefactoringFileFactory changes;
    QMap<Utils::FilePath, TextEditor::RefactoringFilePtr> refactoringFiles;

    for (const ExplainingStep &step : m_diagnostic.explainingSteps) {
        if (!step.isFixIt)
            continue;
        TextEditor::RefactoringFilePtr &refactoringFile =
                refactoringFiles[step.location.targetFilePath];
        if (refactoringFile.isNull())
            refactoringFile = changes.file(step.location.targetFilePath);
        Utils::ChangeSet changeSet = refactoringFile->changeSet();
        Range range = toRange(refactoringFile->document(), {step.ranges.first(), step.ranges.last()});
        changeSet.replace(range, step.message);
        refactoringFile->setChangeSet(changeSet);
    }

    for (const TextEditor::RefactoringFilePtr &refactoringFile : std::as_const(refactoringFiles))
        refactoringFile->apply();
}

DocumentQuickFixFactory::DocumentQuickFixFactory(DocumentQuickFixFactory::RunnerCollector runnerCollector)
    : m_runnerCollector(runnerCollector)
{}

void DocumentQuickFixFactory::doMatch(const CppEditor::Internal::CppQuickFixInterface &interface,
                                      QuickFixOperations &result)
{
    QTC_ASSERT(m_runnerCollector, return );
    if (DocumentClangToolRunner *runner = m_runnerCollector(interface.filePath())) {
        const QTextBlock &block = interface.textDocument()->findBlock(interface.position());
        if (!block.isValid())
            return;

        const int lineNumber = block.blockNumber() + 1;

        for (Diagnostic diagnostic : runner->diagnosticsAtLine(lineNumber)) {
            if (diagnostic.hasFixits)
                result << new ClangToolQuickFixOperation(diagnostic);
        }
    }
}

} // namespace Internal
} // namespace ClangTools
