// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "glsleditor.h"
#include "glsleditorconstants.h"
#include "glslhighlighter.h"
#include "glslautocompleter.h"
#include "glslcompletionassist.h"
#include "glslindenter.h"

#include <glsl/glsllexer.h>
#include <glsl/glslparser.h>
#include <glsl/glslengine.h>
#include <glsl/glslsemantic.h>
#include <glsl/glslsymbols.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreplugintr.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <cplusplus/SimpleLexer.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <texteditor/fontsettings.h>
#include <texteditor/refactoroverlay.h>
#include <texteditor/textdocument.h>
#include <texteditor/syntaxhighlighter.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/texteditorsettings.h>

#include <utils/algorithm.h>
#include <utils/changeset.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>
#include <utils/tooltip/tooltip.h>
#include <utils/uncommentselection.h>

#include <QCoreApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QTextBlock>
#include <QTimer>
#include <QTreeView>

using namespace TextEditor;
using namespace GLSL;

namespace GlslEditor::Internal {

static int versionFor(const QString &source)
{
    CPlusPlus::SimpleLexer lexer;
    lexer.setPreprocessorMode(false);
    const CPlusPlus::Tokens tokens = lexer(source);

    int version = -1;
//    QString profile;
    const int end = tokens.size();
    for (int it = 0; it + 2 < end; ++it) {
        const CPlusPlus::Token &token = tokens.at(it);
        if (token.isComment())
            continue;
        if (token.kind() == CPlusPlus::T_POUND) {
            const int line = token.lineno;
            const CPlusPlus::Token &successor = tokens.at(it + 1);
            if (line != successor.lineno)
                break;
            if (successor.kind() != CPlusPlus::T_IDENTIFIER)
                break;
            if (source.mid(successor.bytesBegin(), successor.bytes()) != "version")
                break;

            const CPlusPlus::Token &versionToken = tokens.at(it + 2);
            if (line != versionToken.lineno)
                break;
            if (versionToken.kind() != CPlusPlus::T_NUMERIC_LITERAL)
                break;
            version = source.mid(versionToken.bytesBegin(), versionToken.bytes()).toInt();

//            if (version >= 150 && it + 3 < end) {
//                const CPlusPlus::Token &profileToken = tokens.at(it + 3);
//                if (line != profileToken.lineno)
//                    break;
//                if (profileToken.kind() != CPlusPlus::T_IDENTIFIER)
//                    break;
//                profile = source.mid(profileToken.bytesBegin(), profileToken.bytes());
//            }
            break;
        }
        break;
    }
    return version;
}

enum {
    UPDATE_DOCUMENT_DEFAULT_INTERVAL = 150
};

class InitFile final
{
public:
    explicit InitFile(const QString &fileName) : m_fileName(fileName) {}

    ~InitFile() { delete m_engine; }

    GLSL::Engine *engine() const
    {
        if (!m_engine)
            initialize();
        return m_engine;
    }

    GLSL::TranslationUnitAST *ast() const
    {
        if (!m_ast)
            initialize();
        return m_ast;
    }

private:
    void initialize() const
    {
        // Parse the builtins for any language variant so we can use all keywords.
        const int variant = GLSL::Lexer::Variant_All;

        QByteArray code;
        QFile file(Core::ICore::resourcePath("glsl").pathAppended(m_fileName).toUrlishString());
        if (file.open(QFile::ReadOnly))
            code = file.readAll();

        m_engine = new GLSL::Engine();
        GLSL::Parser parser(m_engine, code.constData(), code.size(), variant);
        m_ast = parser.parse();
    }

    QString m_fileName;
    mutable GLSL::Engine *m_engine = nullptr;
    mutable GLSL::TranslationUnitAST *m_ast = nullptr;
};

static const InitFile *fragmentShaderInit(int variant)
{
    static InitFile glsl_es_100_frag{"glsl_es_100.frag"};
    static InitFile glsl_120_frag{"glsl_120.frag"};
    static InitFile glsl_330_frag{"glsl_330.frag"};

    if (variant & GLSL::Lexer::Variant_GLSL_400)
        return &glsl_330_frag;

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return  &glsl_120_frag;

    return &glsl_es_100_frag;
}

static const InitFile *vertexShaderInit(int variant)
{
    static InitFile glsl_es_100_vert{"glsl_es_100.vert"};
    static InitFile glsl_120_vert{"glsl_120.vert"};
    static InitFile glsl_330_vert{"glsl_330.vert"};

    if (variant & GLSL::Lexer::Variant_GLSL_400)
        return &glsl_330_vert;

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return &glsl_120_vert;

    return &glsl_es_100_vert;
}

static const InitFile *shaderInit(int variant)
{
    static InitFile glsl_es_100_common{"glsl_es_100_common.glsl"};
    static InitFile glsl_120_common{"glsl_120_common.glsl"};
    static InitFile glsl_330_common{"glsl_330_common.glsl"};

    if (variant & GLSL::Lexer::Variant_GLSL_400)
        return &glsl_330_common;

    if (variant & GLSL::Lexer::Variant_GLSL_120)
        return &glsl_120_common;

    return &glsl_es_100_common;
}

class CreateRanges: protected Visitor
{
    QTextDocument *textDocument;
    Document::Ptr glslDocument;

public:
    CreateRanges(QTextDocument *textDocument, Document::Ptr glslDocument)
        : textDocument(textDocument), glslDocument(glslDocument) {}

    void operator()(AST *ast) { accept(ast); }

protected:
    using GLSL::Visitor::visit;

    void endVisit(CompoundStatementAST *ast) override
    {
        if (ast->symbol) {
            QTextCursor tc(textDocument);
            tc.setPosition(ast->start);
            tc.setPosition(ast->end, QTextCursor::KeepAnchor);
            glslDocument->addRange(tc, ast->symbol);
        }
    }
};

//
//  GlslEditorWidget
//

class GlslEditorWidget : public TextEditorWidget
{
public:
    GlslEditorWidget();

    int editorRevision() const;
    bool isOutdated() const;

    QSet<QString> identifiers() const;

    std::unique_ptr<AssistInterface> createAssistInterface(AssistKind assistKind,
                                                           AssistReason reason) const override;

private:
    void updateDocumentNow();
    void setSelectedElements();
    void onTooltipRequested(const QPoint &point, int pos);
    QString wordUnderCursor() const;

    QTimer m_updateDocumentTimer;
    QComboBox *m_outlineCombo = nullptr;
    Document::Ptr m_glslDocument;
};

GlslEditorWidget::GlslEditorWidget()
{
    setAutoCompleter(new GlslCompleter);

    m_updateDocumentTimer.setInterval(UPDATE_DOCUMENT_DEFAULT_INTERVAL);
    m_updateDocumentTimer.setSingleShot(true);
    connect(&m_updateDocumentTimer, &QTimer::timeout,
            this, &GlslEditorWidget::updateDocumentNow);

    connect(this, &PlainTextEdit::textChanged, [this] { m_updateDocumentTimer.start(); });

    m_outlineCombo = new QComboBox;
    m_outlineCombo->setMinimumContentsLength(22);

    // ### m_outlineCombo->setModel(m_outlineModel);

    auto treeView = new QTreeView;
    treeView->header()->hide();
    treeView->setItemsExpandable(false);
    treeView->setRootIsDecorated(false);
    m_outlineCombo->setView(treeView);
    treeView->expandAll();

    //m_outlineCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // Make the combo box prefer to expand
    QSizePolicy policy = m_outlineCombo->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_outlineCombo->setSizePolicy(policy);

    insertExtraToolBarWidget(TextEditorWidget::Left, m_outlineCombo);

    connect(this, &TextEditorWidget::tooltipRequested, this, &GlslEditorWidget::onTooltipRequested);
}

int GlslEditorWidget::editorRevision() const
{
    //return document()->revision();
    return 0;
}

bool GlslEditorWidget::isOutdated() const
{
//    if (m_semanticInfo.revision() != editorRevision())
//        return true;

    return false;
}

QString GlslEditorWidget::wordUnderCursor() const
{
    QTextCursor tc = textCursor();
    const QChar ch = document()->characterAt(tc.position() - 1);
    // make sure that we're not at the start of the next word.
    if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
        tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::StartOfWord);
    tc.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    const QString word = tc.selectedText();
    return word;
}

void GlslEditorWidget::updateDocumentNow()
{
    m_updateDocumentTimer.stop();

    int variant = languageVariant(textDocument()->mimeType());
    const QString contents = toPlainText(); // get the code from the editor
    int version = versionFor(contents);
    if (version >= 330)
        variant |= GLSL::Lexer::Variant_GLSL_400;

    const QByteArray preprocessedCode = contents.toLatin1(); // ### use the QtCreator C++ preprocessor.

    Document::Ptr doc(new Document());
    doc->_engine = new Engine();
    Parser parser(doc->_engine, preprocessedCode.constData(), preprocessedCode.size(), variant);

    TranslationUnitAST *ast = parser.parse();
    if (ast || extraSelections(CodeWarningsSelection).isEmpty()) {
        Semantic sem;
        Scope *globalScope = new Namespace();
        doc->_globalScope = globalScope;
        const InitFile *file = shaderInit(variant);
        sem.translationUnit(file->ast(), globalScope, file->engine());
        if (variant & Lexer::Variant_VertexShader) {
            file = vertexShaderInit(variant);
            sem.translationUnit(file->ast(), globalScope, file->engine());
        }
        if (variant & Lexer::Variant_FragmentShader) {
            file = fragmentShaderInit(variant);
            sem.translationUnit(file->ast(), globalScope, file->engine());
        }
        sem.translationUnit(ast, globalScope, doc->_engine);

        CreateRanges createRanges(document(), doc);
        createRanges(ast);

        const TextEditor::FontSettings &fontSettings = TextEditor::TextEditorSettings::fontSettings();

        QTextCharFormat warningFormat = fontSettings.toTextCharFormat(TextEditor::C_WARNING);
        QTextCharFormat errorFormat = fontSettings.toTextCharFormat(TextEditor::C_ERROR);

        QList<QTextEdit::ExtraSelection> sels;
        QSet<int> errors;

        const QList<DiagnosticMessage> messages = doc->_engine->diagnosticMessages();
        for (const DiagnosticMessage &m : messages) {
            if (! m.line())
                continue;
            if (!Utils::insert(errors, m.line()))
                continue;

            QTextCursor cursor(document()->findBlockByNumber(m.line() - 1));
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format = m.isError() ? errorFormat : warningFormat;
            sel.format.setToolTip(m.message());
            sels.append(sel);
        }

        setExtraSelections(CodeWarningsSelection, sels);
        m_glslDocument = doc;
    }
}

void GlslEditorWidget::onTooltipRequested(const QPoint &point, int pos)
{
    QTC_ASSERT(m_glslDocument && m_glslDocument->engine(), return);
    const int lineno = document()->findBlock(pos).blockNumber() + 1;
    const QStringList messages
            = Utils::transform<QStringList>(
                Utils::filtered(m_glslDocument->engine()->diagnosticMessages(),
                                [lineno](const DiagnosticMessage &msg) {
                    return msg.line() == lineno;
                }),
                [](const DiagnosticMessage &msg) {
        return msg.message();
    });

    if (!messages.isEmpty())
        Utils::ToolTip::show(point, messages.join("<hr/>"), this);
    else
        Utils::ToolTip::hide();
}

int languageVariant(const QString &type)
{
    int variant = 0;
    bool isVertex = false;
    bool isFragment = false;
    bool isDesktop = false;
    if (type.isEmpty()) {
        // ### Before file has been opened, so don't know the mime type.
        isVertex = true;
        isFragment = true;
    } else if (type == QLatin1String("text/x-glsl") ||
               type == QLatin1String(Utils::Constants::GLSL_MIMETYPE)) {
        isVertex = true;
        isFragment = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_VERT_MIMETYPE)) {
        isVertex = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_FRAG_MIMETYPE)) {
        isFragment = true;
        isDesktop = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_ES_VERT_MIMETYPE)) {
        isVertex = true;
    } else if (type == QLatin1String(Utils::Constants::GLSL_ES_FRAG_MIMETYPE)) {
        isFragment = true;
    }
    if (isDesktop)
        variant |= Lexer::Variant_GLSL_120;
    else
        variant |= Lexer::Variant_GLSL_ES_100;
    if (isVertex)
        variant |= Lexer::Variant_VertexShader;
    if (isFragment)
        variant |= Lexer::Variant_FragmentShader;
    return variant;
}

std::unique_ptr<AssistInterface> GlslEditorWidget::createAssistInterface(
    AssistKind kind, AssistReason reason) const
{
    if (kind != Completion)
        return TextEditorWidget::createAssistInterface(kind, reason);

    return std::make_unique<GlslCompletionAssistInterface>(textCursor(),
                                                           textDocument()->filePath(),
                                                           reason,
                                                           textDocument()->mimeType(),
                                                           m_glslDocument);
}

//  GlslEditorFactory

class GlslEditorFactory final : public TextEditor::TextEditorFactory
{
public:
    GlslEditorFactory()
    {
        setId(Constants::C_GLSLEDITOR_ID);
        setDisplayName(::Core::Tr::tr(Constants::C_GLSLEDITOR_DISPLAY_NAME));
        addMimeType(Utils::Constants::GLSL_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_VERT_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_FRAG_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_ES_VERT_MIMETYPE);
        addMimeType(Utils::Constants::GLSL_ES_FRAG_MIMETYPE);

        setDocumentCreator([]() { return new TextDocument(Constants::C_GLSLEDITOR_ID); });
        setEditorWidgetCreator([]() { return new GlslEditorWidget; });
        setIndenterCreator(&createGlslIndenter);
        setSyntaxHighlighterCreator(&createGlslHighlighter);
        setCommentDefinition(Utils::CommentDefinition::CppStyle);
        setCompletionAssistProvider(createGlslCompletionAssistProvider());
        setParenthesesMatchingEnabled(true);
        setCodeFoldingSupported(true);

        setOptionalActionMask(OptionalActions::Format
                                | OptionalActions::UnCommentSelection
                                | OptionalActions::UnCollapseAll);
    }
};

void setupGlslEditorFactory()
{
    static GlslEditorFactory theGlslEditorFactory;
}

} // GlslEditor::Internal
