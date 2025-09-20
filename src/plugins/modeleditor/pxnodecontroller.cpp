// Copyright (C) 2016 Jochen Becher
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pxnodecontroller.h"

#include "classviewcontroller.h"
#include "componentviewcontroller.h"
#include "modeleditortr.h"
#include "modelutilities.h"
#include "packageviewcontroller.h"
#include "pxnodeutilities.h"

#include "qmt/model/mpackage.h"
#include "qmt/model/mclass.h"
#include "qmt/model/mcomponent.h"
#include "qmt/model/mdiagram.h"
#include "qmt/model/mitem.h"
#include "qmt/model/mcanvasdiagram.h"
#include "qmt/controller/namecontroller.h"
#include "qmt/controller/undocontroller.h"
#include "qmt/model_controller/modelcontroller.h"
#include "qmt/tasks/diagramscenecontroller.h"

#include <projectexplorer/projectnodes.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QAction>
#include <QMenu>
#include <QQueue>

using Utils::FilePath;

namespace ModelEditor {
namespace Internal {

class PxNodeController::MenuAction :
        public QAction
{
public:
    enum Type {
        TYPE_ADD_COMPONENT,
        TYPE_ADD_CLASS,
        TYPE_ADD_PACKAGE,
        TYPE_ADD_PACKAGE_AND_DIAGRAM,
        TYPE_ADD_PACKAGE_MODEL,
        TYPE_ADD_COMPONENT_MODEL,
        TYPE_ADD_CLASS_MODEL,
        TYPE_ADD_PACKAGE_LINK,
        TYPE_ADD_DIAGRAM_LINK,
        TYPE_ADD_DOCUMENT_LINK,
    };

public:
    MenuAction(const QString &text, const QString &elementName, Type type, int index,
               QObject *parent)
        : QAction(text, parent),
          elementName(elementName),
          type(type),
          index(index)
    {
    }

    MenuAction(const QString &text, const QString &elementName, Type type, const QString &stereotype,
               const FilePath &filePath, QObject *parent)
        : QAction(text, parent),
          elementName(elementName),
          type(type),
          index(-1),
          stereotype(stereotype),
          filePath(filePath)
    {
    }

    MenuAction(const QString &text, const QString &elementName, Type type, QObject *parent)
        : QAction(text, parent),
          elementName(elementName),
          type(type),
          index(-1)
    {
    }

    QString elementName;
    int type;
    int index;
    QString className;
    QString stereotype;
    FilePath filePath;
};

class PxNodeController::PxNodeControllerPrivate
{
public:
    PxNodeUtilities *pxnodeUtilities = nullptr;
    ModelUtilities *modelUtilities = nullptr;
    PackageViewController *packageViewController = nullptr;
    ComponentViewController *componentViewController = nullptr;
    ClassViewController *classViewController = nullptr;
    qmt::DiagramSceneController *diagramSceneController = nullptr;
    FilePath anchorFolder;
};

PxNodeController::PxNodeController(QObject *parent)
    : QObject(parent),
      d(new PxNodeControllerPrivate)
{
    d->pxnodeUtilities = new PxNodeUtilities(this);
    d->modelUtilities = new ModelUtilities(this);
    d->packageViewController = new PackageViewController(this);
    d->packageViewController->setModelUtilities(d->modelUtilities);
    d->componentViewController = new ComponentViewController(this);
    d->componentViewController->setPxNodeUtilties(d->pxnodeUtilities);
    d->componentViewController->setPackageViewController(d->packageViewController);
    d->componentViewController->setModelUtilities(d->modelUtilities);
    d->classViewController = new ClassViewController(this);
}

PxNodeController::~PxNodeController()
{
    delete d;
}

ComponentViewController *PxNodeController::componentViewController() const
{
    return d->componentViewController;
}

void PxNodeController::setDiagramSceneController(
        qmt::DiagramSceneController *diagramSceneController)
{
    d->diagramSceneController = diagramSceneController;
    d->pxnodeUtilities->setDiagramSceneController(diagramSceneController);
    d->packageViewController->setModelController(diagramSceneController->modelController());
    d->componentViewController->setDiagramSceneController(diagramSceneController);
}

void PxNodeController::setAnchorFolder(const FilePath &anchorFolder)
{
    d->anchorFolder = anchorFolder;
}

void PxNodeController::addFileSystemEntry(const FilePath &filePath, int line, int column,
                                          qmt::DElement *topMostElementAtPos, const QPointF &pos,
                                          qmt::MDiagram *diagram)
{
    QMT_ASSERT(diagram, return);

    QString elementName = qmt::NameController::convertFileNameToElementName(filePath);

    if (filePath.isFile()) {
        auto menu = new QMenu;
        menu->addAction(new MenuAction(Tr::tr("Add Component %1").arg(elementName), elementName,
                                       MenuAction::TYPE_ADD_COMPONENT, menu));
        const QStringList classNames = Utils::toList(
            d->classViewController->findClassDeclarations(filePath, line, column));
        if (!classNames.empty()) {
            menu->addSeparator();
            int index = 0;
            for (const QString &className : classNames) {
                auto action = new MenuAction(Tr::tr("Add Class %1").arg(className), elementName,
                                             MenuAction::TYPE_ADD_CLASS, index, menu);
                action->className = className;
                menu->addAction(action);
                ++index;
            }
        }
        menu->addSeparator();
        QString fileName = filePath.fileName();
        menu->addAction(new MenuAction(Tr::tr("Add Package Link to %1").arg(fileName), fileName,
                                       MenuAction::TYPE_ADD_PACKAGE_LINK, "package", filePath, menu));
        menu->addAction(new MenuAction(Tr::tr("Add Diagram Link to %1").arg(fileName), fileName,
                                       MenuAction::TYPE_ADD_DIAGRAM_LINK, "diagram", filePath, menu));
        menu->addAction(new MenuAction(Tr::tr("Add Document Link to %1").arg(fileName), fileName,
                                       MenuAction::TYPE_ADD_DOCUMENT_LINK, "document", filePath, menu));
        connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
        connect(menu, &QMenu::triggered, this, [this, filePath, topMostElementAtPos, pos, diagram](
                                                   QAction *action) {
            // TODO potential risk if topMostElementAtPos or diagram is deleted in between
            onMenuActionTriggered(static_cast<MenuAction *>(action), filePath, topMostElementAtPos,
                                  pos, diagram);
        });
        menu->popup(QCursor::pos());
    } else if (filePath.isDir()) {
        auto menu = new QMenu;
        menu->addAction(new MenuAction(Tr::tr("Add Package %1").arg(elementName), elementName,
                                       MenuAction::TYPE_ADD_PACKAGE, menu));
        menu->addAction(new MenuAction(Tr::tr("Add Package and Diagram %1").arg(elementName), elementName,
                                       MenuAction::TYPE_ADD_PACKAGE_AND_DIAGRAM, menu));
        menu->addAction(new MenuAction(Tr::tr("Add Component Model"), elementName,
                                       MenuAction::TYPE_ADD_COMPONENT_MODEL, menu));
        connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
        connect(menu, &QMenu::triggered, this, [this, filePath, topMostElementAtPos, pos, diagram](
                                                   QAction *action) {
            onMenuActionTriggered(static_cast<MenuAction *>(action), filePath, topMostElementAtPos,
                                  pos, diagram);
        });
        menu->popup(QCursor::pos());
    }
}

bool PxNodeController::hasDiagramForExplorerNode(const ProjectExplorer::Node *node)
{
    return findDiagramForExplorerNode(node) != nullptr;
}

qmt::MDiagram *PxNodeController::findDiagramForExplorerNode(const ProjectExplorer::Node *node)
{
    if (!node)
        return nullptr;

    QStringList relativeElements = qmt::NameController::buildElementsPath(
        d->pxnodeUtilities->calcRelativePath(node, d->anchorFolder), false);

    QQueue<qmt::MPackage *> roots;
    roots.append(d->diagramSceneController->modelController()->rootPackage());

    while (!roots.isEmpty()) {
        qmt::MPackage *package = roots.takeFirst();

        // append all sub-packages of the same level as next root packages
        for (const qmt::Handle<qmt::MObject> &handle : package->children()) {
            if (handle.hasTarget()) {
                if (auto childPackage = dynamic_cast<qmt::MPackage *>(handle.target()))
                    roots.append(childPackage);
            }
        }

        // goto into sub-packages to find complete chain of names
        int relativeIndex = 0;
        bool found = true;
        while (found && relativeIndex < relativeElements.size()) {
            QString relativeSearchId = qmt::NameController::calcElementNameSearchId(
                        relativeElements.at(relativeIndex));
            found = false;
            for (const qmt::Handle<qmt::MObject> &handle : package->children()) {
                if (handle.hasTarget()) {
                    if (auto childPackage = dynamic_cast<qmt::MPackage *>(handle.target())) {
                        if (qmt::NameController::calcElementNameSearchId(childPackage->name()) == relativeSearchId) {
                            package = childPackage;
                            ++relativeIndex;
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        if (found) {
            QMT_ASSERT(relativeIndex >= relativeElements.size(), return nullptr);
            // complete package chain found so check for appropriate diagram within deepest package
            qmt::MDiagram *diagram = d->diagramSceneController->findDiagramBySearchId(
                        package, package->name());
            if (diagram)
                return diagram;
            // find first diagram within deepest package
            for (const qmt::Handle<qmt::MObject> &handle : package->children()) {
                if (handle.hasTarget()) {
                    if (auto diagram = dynamic_cast<qmt::MDiagram *>(handle.target()))
                        return diagram;
                }
            }
        }
    }

    // complete sub-package structure scanned but did not found the desired object
    return nullptr;
}

void PxNodeController::onMenuActionTriggered(PxNodeController::MenuAction *action,
                                             const FilePath &filePath,
                                             qmt::DElement *topMostElementAtPos,
                                             const QPointF &pos, qmt::MDiagram *diagram)
{
    qmt::MObject *newObject = nullptr;
    qmt::MDiagram *newDiagramInObject = nullptr;
    bool dropInCurrentDiagram = false;

    switch (action->type) {
    case MenuAction::TYPE_ADD_COMPONENT:
    {
        auto component = new qmt::MComponent();
        component->setFlags(qmt::MElement::ReverseEngineered);
        component->setName(action->elementName);
        newObject = component;
        break;
    }
    case MenuAction::TYPE_ADD_CLASS:
    {
        // TODO handle template classes
        auto klass = new qmt::MClass();
        klass->setFlags(qmt::MElement::ReverseEngineered);
        parseFullClassName(klass, action->className);
        newObject = klass;
        break;
    }
    case MenuAction::TYPE_ADD_PACKAGE:
    case MenuAction::TYPE_ADD_PACKAGE_AND_DIAGRAM:
    {
            auto package = new qmt::MPackage();
            package->setFlags(qmt::MElement::ReverseEngineered);
            package->setName(action->elementName);
            if (!action->stereotype.isEmpty())
                package->setStereotypes({action->stereotype});
            newObject = package;
            if (action->type == MenuAction::TYPE_ADD_PACKAGE_AND_DIAGRAM) {
                auto diagram = new qmt::MCanvasDiagram();
                diagram->setName(action->elementName);
                newDiagramInObject = diagram;
            }
        break;
    }
    case MenuAction::TYPE_ADD_COMPONENT_MODEL:
    {
        auto package = new qmt::MPackage();
        package->setFlags(qmt::MElement::ReverseEngineered);
        package->setName(action->elementName);
        if (!action->stereotype.isEmpty())
            package->setStereotypes({action->stereotype});
        d->diagramSceneController->modelController()->undoController()->beginMergeSequence(Tr::tr("Create Component Model"));
        QStringList relativeElements = qmt::NameController::buildElementsPath(
            d->pxnodeUtilities->calcRelativePath(filePath, d->anchorFolder), true);
        if (qmt::MObject *existingObject = d->pxnodeUtilities->findSameObject(relativeElements, package)) {
            delete package;
            package = dynamic_cast<qmt::MPackage *>(existingObject);
            QMT_ASSERT(package, return);
            d->diagramSceneController->addExistingModelElement(package->uid(), pos, diagram);
        } else {
            qmt::MPackage *requestedRootPackage = d->diagramSceneController->findSuitableParentPackage(topMostElementAtPos, diagram);
            qmt::MPackage *bestParentPackage = d->pxnodeUtilities->createBestMatchingPackagePath(requestedRootPackage, relativeElements);
            d->diagramSceneController->dropNewModelElement(package, bestParentPackage, pos, diagram);
        }
        d->componentViewController->createComponentModel(filePath, diagram, d->anchorFolder);
        d->componentViewController->updateIncludeDependencies(package);
        d->diagramSceneController->modelController()->undoController()->endMergeSequence();
        break;
    }
    case MenuAction::TYPE_ADD_PACKAGE_LINK:
    case MenuAction::TYPE_ADD_DIAGRAM_LINK:
    case MenuAction::TYPE_ADD_DOCUMENT_LINK:
    {
        auto item = new qmt::MItem();
        item->setName(action->elementName);
        item->setVariety(action->stereotype);
        item->setVarietyEditable(false);
        item->setLinkedFileName(action->filePath.relativePathFromDir(d->anchorFolder));
        newObject = item;
        dropInCurrentDiagram = true;
        break;
    }
    }

    if (newObject) {
        d->diagramSceneController->modelController()->undoController()->beginMergeSequence(Tr::tr("Drop Node"));
        if (dropInCurrentDiagram) {
            auto *parentPackage = dynamic_cast<qmt::MPackage *>(diagram->owner());
            if (parentPackage)
                d->diagramSceneController->dropNewModelElement(newObject, parentPackage, pos, diagram);
        } else {
            qmt::MObject *parentForDiagram = nullptr;
            QStringList relativeElements = qmt::NameController::buildElementsPath(
                d->pxnodeUtilities->calcRelativePath(filePath, d->anchorFolder),
                dynamic_cast<qmt::MPackage *>(newObject) != nullptr);
            if (qmt::MObject *existingObject = d->pxnodeUtilities->findSameObject(relativeElements, newObject)) {
                delete newObject;
                newObject = nullptr;
                d->diagramSceneController->addExistingModelElement(existingObject->uid(), pos, diagram);
                parentForDiagram = existingObject;
            } else {
                qmt::MPackage *requestedRootPackage = d->diagramSceneController->findSuitableParentPackage(topMostElementAtPos, diagram);
                qmt::MPackage *bestParentPackage = d->pxnodeUtilities->createBestMatchingPackagePath(requestedRootPackage, relativeElements);
                d->diagramSceneController->dropNewModelElement(newObject, bestParentPackage, pos, diagram);
                parentForDiagram = newObject;
            }

            // if requested and not existing then create new diagram in package
            if (newDiagramInObject) {
                auto package = dynamic_cast<qmt::MPackage *>(parentForDiagram);
                QMT_ASSERT(package, return);
                if (d->diagramSceneController->findDiagramBySearchId(package, newDiagramInObject->name()))
                    delete newDiagramInObject;
                else
                    d->diagramSceneController->modelController()->addObject(package, newDiagramInObject);
            }
        }
        d->diagramSceneController->modelController()->undoController()->endMergeSequence();
    }
}

void PxNodeController::parseFullClassName(qmt::MClass *klass, const QString &fullClassName)
{
    QString umlNamespace;
    QString className;
    QStringList templateParameters;

    if (qmt::NameController::parseClassName(fullClassName, &umlNamespace, &className, &templateParameters)) {
        klass->setName(className);
        klass->setUmlNamespace(umlNamespace);
        klass->setTemplateParameters(templateParameters);
    } else {
        klass->setName(fullClassName);
    }
}

} // namespace Internal
} // namespace ModelEditor
