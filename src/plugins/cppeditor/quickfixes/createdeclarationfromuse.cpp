// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "createdeclarationfromuse.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "../insertionpointlocator.h"
#include "../symbolfinder.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"
#include "cppquickfixprojectsettings.h"

#include <coreplugin/icore.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>
#include <projectexplorer/projecttree.h>

#include <QInputDialog>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

#include <memory>
#include <variant>
#include <vector>

using namespace CPlusPlus;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

using TypeOrExpr = std::variant<const CPlusPlus::ExpressionAST *, CPlusPlus::FullySpecifiedType>;

// FIXME: Needs to consider the scope at the insertion site.
static QString declFromExpr(
    const TypeOrExpr &typeOrExpr,
    const CallAST *call,
    const NameAST *varName,
    const Snapshot &snapshot,
    const LookupContext &context,
    const CppRefactoringFilePtr &file,
    bool makeConst)
{
    const auto getTypeFromUser = [varName, call]() -> QString {
        if (call)
            return {};
        const QString typeFromUser = QInputDialog::getText(
            Core::ICore::dialogParent(),
            Tr::tr("Provide the type"),
            Tr::tr("Data type:"),
            QLineEdit::Normal);
        if (!typeFromUser.isEmpty())
            return typeFromUser + ' ' + nameString(varName);
        return {};
    };
    const auto getTypeOfExpr = [&](const ExpressionAST *expr) -> FullySpecifiedType {
        return typeOfExpr(expr, file, snapshot, context);
    };

    const Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    const FullySpecifiedType type = std::holds_alternative<FullySpecifiedType>(typeOrExpr)
                                        ? std::get<FullySpecifiedType>(typeOrExpr)
                                        : getTypeOfExpr(std::get<const ExpressionAST *>(typeOrExpr));
    if (!call)
        return type.isValid() ? oo.prettyType(type, varName->name) : getTypeFromUser();

    Function func(file->cppDocument()->translationUnit(), 0, varName->name);
    func.setConst(makeConst);
    std::vector<std::unique_ptr<Argument>> argsMgr;
    for (ExpressionListAST *it = call->expression_list; it; it = it->next) {
        argsMgr.push_back(std::make_unique<Argument>(nullptr, 0, nullptr));
        Argument * const arg = argsMgr.back().get();
        arg->setType(getTypeOfExpr(it->value));
        func.addMember(arg);
    }
    return oo.prettyType(type) + ' ' + oo.prettyType(func.type(), varName->name);
}

class InsertDeclOperation: public CppQuickFixOperation
{
public:
    InsertDeclOperation(const CppQuickFixInterface &interface,
                        const FilePath &targetFilePath, const Class *targetSymbol,
                        InsertionPointLocator::AccessSpec xsSpec, const QString &decl, int priority)
        : CppQuickFixOperation(interface, priority)
        , m_targetFilePath(targetFilePath)
        , m_targetSymbol(targetSymbol)
        , m_xsSpec(xsSpec)
        , m_decl(decl)
    {
        setDescription(Tr::tr("Add %1 Declaration")
                           .arg(InsertionPointLocator::accessSpecToString(xsSpec)));
    }

    void perform() override
    {
        CppRefactoringChanges refactoring(snapshot());

        InsertionPointLocator locator(refactoring);
        const InsertionLocation loc = locator.methodDeclarationInClass(
            m_targetFilePath, m_targetSymbol, m_xsSpec);
        QTC_ASSERT(loc.isValid(), return);

        CppRefactoringFilePtr targetFile = refactoring.cppFile(m_targetFilePath);
        int targetPosition = targetFile->position(loc.line(), loc.column());

        ChangeSet target;
        target.insert(targetPosition, loc.prefix() + m_decl);
        targetFile->setOpenEditor(true, targetPosition);
        targetFile->apply(target);
    }

    static QString generateDeclaration(const Function *function)
    {
        Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        oo.showFunctionSignatures = true;
        oo.showReturnTypes = true;
        oo.showArgumentNames = true;
        oo.showEnclosingTemplate = true;

        QString decl;
        decl += oo.prettyType(function->type(), function->unqualifiedName());
        decl += QLatin1String(";\n");

        return decl;
    }

private:
    FilePath m_targetFilePath;
    const Class *m_targetSymbol;
    InsertionPointLocator::AccessSpec m_xsSpec;
    QString m_decl;
};

class DeclOperationFactory
{
public:
    DeclOperationFactory(const CppQuickFixInterface &interface, const FilePath &filePath,
                         const Class *matchingClass, const QString &decl)
        : m_interface(interface)
        , m_filePath(filePath)
        , m_matchingClass(matchingClass)
        , m_decl(decl)
    {}

    QuickFixOperation *operator()(InsertionPointLocator::AccessSpec xsSpec, int priority)
    {
        return new InsertDeclOperation(m_interface, m_filePath, m_matchingClass, xsSpec, m_decl, priority);
    }

private:
    const CppQuickFixInterface &m_interface;
    const FilePath &m_filePath;
    const Class *m_matchingClass;
    const QString &m_decl;
};

class InsertMemberFromInitializationOp : public CppQuickFixOperation
{
public:
    InsertMemberFromInitializationOp(
        const CppQuickFixInterface &interface,
        const Class *theClass,
        const NameAST *memberName,
        const TypeOrExpr &typeOrExpr,
        const CallAST *call,
        InsertionPointLocator::AccessSpec accessSpec,
        bool makeStatic,
        bool makeConst)
        : CppQuickFixOperation(interface),
        m_class(theClass), m_memberName(memberName), m_typeOrExpr(typeOrExpr), m_call(call),
        m_accessSpec(accessSpec), m_makeStatic(makeStatic), m_makeConst(makeConst)
    {
        if (call)
            setDescription(Tr::tr("Add Member Function \"%1\"").arg(nameString(memberName)));
        else
            setDescription(Tr::tr("Add Class Member \"%1\"").arg(nameString(memberName)));
    }

private:
    void perform() override
    {
        QString decl = declFromExpr(m_typeOrExpr, m_call, m_memberName, snapshot(), context(),
                                    currentFile(), m_makeConst);
        if (decl.isEmpty())
            return;
        if (m_makeStatic)
            decl.prepend("static ");

        const CppRefactoringChanges refactoring(snapshot());
        const InsertionPointLocator locator(refactoring);
        const FilePath filePath = FilePath::fromUtf8(m_class->fileName());
        const InsertionLocation loc = locator.methodDeclarationInClass(
            filePath, m_class, m_accessSpec);
        QTC_ASSERT(loc.isValid(), return);

        CppRefactoringFilePtr targetFile = refactoring.cppFile(filePath);
        targetFile->apply(ChangeSet::makeInsert(
            targetFile->position(loc.line(), loc.column()), loc.prefix() + decl + ";\n"));
    }

    const Class * const m_class;
    const NameAST * const m_memberName;
    const TypeOrExpr m_typeOrExpr;
    const CallAST * m_call;
    const InsertionPointLocator::AccessSpec m_accessSpec;
    const bool m_makeStatic;
    const bool m_makeConst;
};

class AddLocalDeclarationOp: public CppQuickFixOperation
{
public:
    AddLocalDeclarationOp(const CppQuickFixInterface &interface,
                          int priority,
                          const BinaryExpressionAST *binaryAST,
                          const SimpleNameAST *simpleNameAST)
        : CppQuickFixOperation(interface, priority)
        , binaryAST(binaryAST)
        , simpleNameAST(simpleNameAST)
    {
        setDescription(Tr::tr("Add Local Declaration"));
    }

    void perform() override
    {
        QString declaration = getDeclaration();

        if (!declaration.isEmpty()) {
            currentFile()->apply(ChangeSet::makeReplace(
                currentFile()->startOf(binaryAST),
                currentFile()->endOf(simpleNameAST),
                declaration));
        }
    }

private:
    QString getDeclaration()
    {
        Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        const auto settings = CppQuickFixProjectsSettings::getQuickFixSettings(
            ProjectTree::currentProject());

        if (currentFile()->cppDocument()->languageFeatures().cxx11Enabled && settings->useAuto)
            return "auto " + oo.prettyName(simpleNameAST->name);
        return declFromExpr(binaryAST->right_expression, nullptr, simpleNameAST, snapshot(),
                            context(), currentFile(), false);
    }

    const BinaryExpressionAST *binaryAST;
    const SimpleNameAST *simpleNameAST;
};

//! Adds a declarations to a definition
class InsertDeclFromDef: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();
        CppRefactoringFilePtr file = interface.currentFile();

        FunctionDefinitionAST *funDef = nullptr;
        int idx = 0;
        for (; idx < path.size(); ++idx) {
            AST *node = path.at(idx);
            if (idx > 1) {
                if (DeclaratorIdAST *declId = node->asDeclaratorId()) {
                    if (file->isCursorOn(declId)) {
                        if (FunctionDefinitionAST *candidate = path.at(idx - 2)->asFunctionDefinition()) {
                            funDef = candidate;
                            break;
                        }
                    }
                }
            }

            if (node->asClassSpecifier())
                return;
        }

        if (!funDef || !funDef->symbol)
            return;

        Function *fun = funDef->symbol;
        if (Class *matchingClass = isMemberFunction(interface.context(), fun)) {
            const QualifiedNameId *qName = fun->name()->asQualifiedNameId();
            for (Symbol *symbol = matchingClass->find(qName->identifier());
                 symbol; symbol = symbol->next()) {
                Symbol *s = symbol;
                if (fun->enclosingScope()->asTemplate()) {
                    if (const Template *templ = s->type()->asTemplateType()) {
                        if (Symbol *decl = templ->declaration()) {
                            if (decl->type()->asFunctionType())
                                s = decl;
                        }
                    }
                }
                if (!s->name()
                    || !qName->identifier()->match(s->identifier())
                    || !s->type()->asFunctionType())
                    continue;

                if (s->type().match(fun->type())) {
                    // Declaration exists.
                    return;
                }
            }
            const FilePath fileName = matchingClass->filePath();
            const QString decl = InsertDeclOperation::generateDeclaration(fun);

            // Add several possible insertion locations for declaration
            DeclOperationFactory operation(interface, fileName, matchingClass, decl);

            result << operation(InsertionPointLocator::Public, 5)
                   << operation(InsertionPointLocator::PublicSlot, 4)
                   << operation(InsertionPointLocator::Protected, 3)
                   << operation(InsertionPointLocator::ProtectedSlot, 2)
                   << operation(InsertionPointLocator::Private, 1)
                   << operation(InsertionPointLocator::PrivateSlot, 0);
        }
    }
};

class AddDeclarationForUndeclaredIdentifier : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        // Are we on a name?
        const QList<AST *> &path = interface.path();
        if (path.isEmpty())
            return;
        if (!path.last()->asSimpleName())
            return;

        // Special case: Member initializer.
        if (!checkForMemberInitializer(interface, result))
            return;

        // Are we inside a function?
        const FunctionDefinitionAST *func = nullptr;
        for (auto it = path.rbegin(); !func && it != path.rend(); ++it)
            func = (*it)->asFunctionDefinition();
        if (!func)
            return;

        // Is this name declared somewhere already?
        const CursorInEditor cursorInEditor(interface.cursor(), interface.filePath(),
                                            interface.editor(), interface.editor()->textDocument());
        const auto followSymbolFallback = [&](const Link &link) {
            if (!link.hasValidTarget())
                collectOperations(interface, result);
        };
        NonInteractiveFollowSymbolMarker niMarker;
        CppModelManager::followSymbol(cursorInEditor, followSymbolFallback, false, false,
                                      FollowSymbolMode::Exact,
                                      CppModelManager::Backend::Builtin);
    }

    void collectOperations(const CppQuickFixInterface &interface,
                           QuickFixOperations &result)
    {
        const QList<AST *> &path = interface.path();
        const CppRefactoringFilePtr &file = interface.currentFile();
        for (int index = path.size() - 1; index != -1; --index) {
            if (const auto call = path.at(index)->asCall())
                return handleCall(call, interface, result);

            // We only trigger if the identifier appears on the left-hand side of an
            // assignment expression.
            const auto binExpr = path.at(index)->asBinaryExpression();
            if (!binExpr)
                continue;
            if (!binExpr->left_expression || !binExpr->right_expression
                || file->tokenAt(binExpr->binary_op_token).kind() != T_EQUAL
                || !interface.isCursorOn(binExpr->left_expression)) {
                return;
            }

            // In the case of "a.|b = c", find out the type of a, locate the class declaration
            // and add a member b there.
            if (const auto memberAccess = binExpr->left_expression->asMemberAccess()) {
                if (interface.isCursorOn(memberAccess->member_name)
                    && memberAccess->member_name == path.last()) {
                    maybeAddMember(interface, file->scopeAt(memberAccess->firstToken()),
                                   file->textOf(memberAccess->base_expression).toUtf8(),
                                   binExpr->right_expression, nullptr, result);
                }
                return;
            }

            const auto idExpr = binExpr->left_expression->asIdExpression();
            if (!idExpr || !idExpr->name)
                return;

            // In the case of "A::|b = c", add a static member b to A.
            if (const auto qualName = idExpr->name->asQualifiedName()) {
                return maybeAddStaticMember(interface, qualName, binExpr->right_expression, nullptr,
                                            result);
            }

            // For an unqualified access, offer a local declaration and, if we are
            // in a member function, a member declaration.
            if (const auto simpleName = idExpr->name->asSimpleName()) {
                if (!property("membersOnly").toBool())
                    result << new AddLocalDeclarationOp(interface, index, binExpr, simpleName);
                maybeAddMember(interface, file->scopeAt(idExpr->firstToken()), "this",
                               binExpr->right_expression, nullptr, result);
                return;
            }
        }
    }

    void handleCall(const CPlusPlus::CallAST *call, const CppQuickFixInterface &interface,
                    QuickFixOperations &result)
    {
        if (!call->base_expression)
            return;

        // In order to find out the return type, we need to check the context of the call.
        // If it is a statement expression, the type is void, if it's a binary expression,
        // we assume the type of the other side of the expression, if it's a return statement,
        // we use the return type of the surrounding function, and if it's a declaration,
        // we use the type of the variable. Other cases are not supported.
        const QList<AST *> &path = interface.path();
        const CppRefactoringFilePtr &file = interface.currentFile();
        TypeOrExpr returnTypeOrExpr;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            if ((*it)->asCompoundStatement())
                return;
            if ((*it)->asExpressionStatement()) {
                returnTypeOrExpr = FullySpecifiedType(&VoidType::instance);
                break;
            }
            if (const auto binExpr = (*it)->asBinaryExpression()) {
                returnTypeOrExpr = interface.isCursorOn(binExpr->left_expression)
                                       ? binExpr->right_expression : binExpr->left_expression;
                break;
            }
            if ((*it)->asReturnStatement()) {
                for (auto it2 = std::next(it); it2 != path.rend(); ++it2) {
                    if (const auto func = (*it2)->asFunctionDefinition()) {
                        if (!func->symbol)
                            return;
                        returnTypeOrExpr = func->symbol->returnType();
                        break;
                    }
                }
                break;
            }
            if (const auto declarator = (*it)->asDeclarator()) {
                if (!interface.isCursorOn(declarator->initializer))
                    return;
                const auto decl = (*std::next(it))->asSimpleDeclaration();
                if (!decl || !decl->symbols)
                    return;
                if (!decl->symbols->value->type().isValid())
                    return;
                returnTypeOrExpr = decl->symbols->value->type();
                break;
            }
        }

        if (std::holds_alternative<const ExpressionAST *>(returnTypeOrExpr)
            && !std::get<const ExpressionAST *>(returnTypeOrExpr)) {
            return;
        }

        // a.f()
        if (const auto memberAccess = call->base_expression->asMemberAccess()) {
            if (!interface.isCursorOn(memberAccess->member_name))
                return;
            maybeAddMember(
                interface, file->scopeAt(call->firstToken()),
                file->textOf(memberAccess->base_expression).toUtf8(), returnTypeOrExpr, call, result);
        }

        const auto idExpr = call->base_expression->asIdExpression();
        if (!idExpr || !idExpr->name)
            return;

        // A::f()
        if (const auto qualName = idExpr->name->asQualifiedName())
            return maybeAddStaticMember(interface, qualName, returnTypeOrExpr, call, result);

        // f()
        if (idExpr->name->asSimpleName()) {
            maybeAddMember(interface, file->scopeAt(idExpr->firstToken()), "this",
                           returnTypeOrExpr, call, result);
        }
    }

    // Returns whether to still do other checks.
    bool checkForMemberInitializer(const CppQuickFixInterface &interface,
                                   QuickFixOperations &result)
    {
        const QList<AST *> &path = interface.path();
        const int size = path.size();
        if (size < 4)
            return true;
        const MemInitializerAST * const memInitializer = path.at(size - 2)->asMemInitializer();
        if (!memInitializer)
            return true;
        if (!path.at(size - 3)->asCtorInitializer())
            return true;
        const FunctionDefinitionAST * ctor = path.at(size - 4)->asFunctionDefinition();
        if (!ctor)
            return false;

        // Now find the class.
        const Class *theClass = nullptr;
        if (size > 4) {
            const ClassSpecifierAST * const classSpec = path.at(size - 5)->asClassSpecifier();
            if (classSpec) // Inline constructor. We get the class directly.
                theClass = classSpec->symbol;
        }
        if (!theClass) {
            // Out-of-line constructor. We need to find the class.
            SymbolFinder finder;
            const QList<Declaration *> matches = finder.findMatchingDeclaration(
                LookupContext(interface.currentFile()->cppDocument(), interface.snapshot()),
                ctor->symbol);
            if (!matches.isEmpty())
                theClass = matches.first()->enclosingClass();
        }

        if (!theClass)
            return false;

        const SimpleNameAST * const name = path.at(size - 1)->asSimpleName();
        QTC_ASSERT(name, return false);

        // Check whether the member exists already.
        if (theClass->find(interface.currentFile()->cppDocument()->translationUnit()->identifier(
                name->identifier_token))) {
            return false;
        }

        result << new InsertMemberFromInitializationOp(
            interface, theClass, memInitializer->name->asSimpleName(), memInitializer->expression,
            nullptr, InsertionPointLocator::Private, false, false);
        return false;
    }

    void maybeAddMember(const CppQuickFixInterface &interface, CPlusPlus::Scope *scope,
                        const QByteArray &classTypeExpr, const TypeOrExpr &typeOrExpr,
                        const CPlusPlus::CallAST *call, QuickFixOperations &result)
    {
        const QList<AST *> &path = interface.path();

        TypeOfExpression typeOfExpression;
        typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot(),
                              interface.context().bindings());
        const QList<LookupItem> lhsTypes = typeOfExpression(
            classTypeExpr, scope,
            TypeOfExpression::Preprocess);
        if (lhsTypes.isEmpty())
            return;

        const Type *type = lhsTypes.first().type().type();
        if (!type)
            return;
        if (type->asPointerType()) {
            type = type->asPointerType()->elementType().type();
            if (!type)
                return;
        }
        const auto namedType = type->asNamedType();
        if (!namedType)
            return;
        const ClassOrNamespace * const classOrNamespace
            = interface.context().lookupType(namedType->name(), scope);
        if (!classOrNamespace || !classOrNamespace->rootClass())
            return;

        const Class * const theClass = classOrNamespace->rootClass();
        bool needsStatic = lhsTypes.first().type().isStatic();

        // If the base expression refers to the same class that the member function is in,
        // then we want to insert a private member, otherwise a public one.
        const FunctionDefinitionAST *func = nullptr;
        for (auto it = path.rbegin(); !func && it != path.rend(); ++it)
            func = (*it)->asFunctionDefinition();
        QTC_ASSERT(func, return);
        InsertionPointLocator::AccessSpec accessSpec = InsertionPointLocator::Public;
        for (int i = 0; i < theClass->memberCount(); ++i) {
            if (theClass->memberAt(i) == func->symbol) {
                accessSpec = InsertionPointLocator::Private;
                needsStatic = func->symbol->isStatic();
                break;
            }
        }
        if (accessSpec == InsertionPointLocator::Public) {
            QList<Declaration *> decls;
            QList<Declaration *> dummy;
            SymbolFinder().findMatchingDeclaration(interface.context(), func->symbol, &decls,
                                                   &dummy, &dummy);
            for (const Declaration * const decl : std::as_const(decls)) {
                for (int i = 0; i < theClass->memberCount(); ++i) {
                    if (theClass->memberAt(i) == decl) {
                        accessSpec = InsertionPointLocator::Private;
                        needsStatic = decl->isStatic();
                        break;
                    }
                }
                if (accessSpec == InsertionPointLocator::Private)
                    break;
            }
        }
        result << new InsertMemberFromInitializationOp(interface, theClass, path.last()->asName(),
                                                       typeOrExpr, call, accessSpec, needsStatic,
                                                       func->symbol->isConst());
    }

    void maybeAddStaticMember(
        const CppQuickFixInterface &interface, const CPlusPlus::QualifiedNameAST *qualName,
        const TypeOrExpr &typeOrExpr, const CPlusPlus::CallAST *call,
        QuickFixOperations &result)
    {
        const QList<AST *> &path = interface.path();

        if (!interface.isCursorOn(qualName->unqualified_name))
            return;
        if (qualName->unqualified_name != path.last())
            return;
        if (!qualName->nested_name_specifier_list)
            return;

        const NameAST * const topLevelName
            = qualName->nested_name_specifier_list->value->class_or_namespace_name;
        if (!topLevelName)
            return;
        ClassOrNamespace * const classOrNamespace = interface.context().lookupType(
            topLevelName->name, interface.currentFile()->scopeAt(qualName->firstToken()));
        if (!classOrNamespace)
            return;
        QList<const Name *> otherNames;
        for (auto it = qualName->nested_name_specifier_list->next; it; it = it->next) {
            if (!it->value || !it->value->class_or_namespace_name)
                return;
            otherNames << it->value->class_or_namespace_name->name;
        }

        const Class *theClass = nullptr;
        if (!otherNames.isEmpty()) {
            const Symbol * const symbol = classOrNamespace->lookupInScope(otherNames);
            if (!symbol)
                return;
            theClass = symbol->asClass();
        } else {
            theClass = classOrNamespace->rootClass();
        }
        if (theClass) {
            result << new InsertMemberFromInitializationOp(
                interface, theClass, path.last()->asName(), typeOrExpr, call,
                InsertionPointLocator::Public, true, false);
        }
    }
};

#ifdef WITH_TESTS
class InsertDeclFromDefTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class AddDeclarationForUndeclaredIdentifierTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerCreateDeclarationFromUseQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(InsertDeclFromDef);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(AddDeclarationForUndeclaredIdentifier);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <createdeclarationfromuse.moc>
#endif
