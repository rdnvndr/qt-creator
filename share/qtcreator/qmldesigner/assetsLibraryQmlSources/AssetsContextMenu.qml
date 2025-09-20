// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls
import StudioControls as StudioControls
import StudioTheme as StudioTheme
import AssetsLibraryBackend

StudioControls.Menu {
    id: root

    required property Item assetsView

    property var assetsModel: AssetsLibraryBackend.assetsModel
    property var rootView: AssetsLibraryBackend.rootView

    property bool __isDirectory: false
    property var __fileIndex: null
    property string __dirPath: ""
    property string __dirName: ""
    property var __onFolderCreated: null
    property var __dirIndex: null
    property string __allExpandedState: ""
    property var __selectedAssetPathsList: null
    property bool __showInGraphicalShellEnabled: false

    signal openNewFolderDialog(string dirPath)

    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

    function openContextMenuForRoot(rootModelIndex, dirPath, dirName, onFolderCreated)
    {
        root.__showInGraphicalShellEnabled = true

        rootView.updateContextMenuActionsEnableState()

        root.__onFolderCreated = onFolderCreated
        root.__fileIndex = ""
        root.__dirPath = dirPath
        root.__dirName = dirName
        root.__dirIndex = rootModelIndex
        root.__isDirectory = false
        root.popup()
    }

    function openContextMenuForDir(dirModelIndex, dirPath, dirName, allExpandedState, onFolderCreated)
    {
        root.__showInGraphicalShellEnabled = true

        rootView.updateContextMenuActionsEnableState()

        root.__onFolderCreated = onFolderCreated
        root.__dirPath = dirPath
        root.__dirName = dirName
        root.__fileIndex = ""
        root.__dirIndex = dirModelIndex
        root.__isDirectory = true
        root.__allExpandedState = allExpandedState
        root.popup()
    }

    function openContextMenuForFile(fileIndex, dirModelIndex, selectedAssetPathsList, onFolderCreated)
    {
        // check that all assets are in the same folder
        let asset0 = selectedAssetPathsList[0]
        let asset0Folder = asset0.substring(0, asset0.lastIndexOf('/'))

        root.__showInGraphicalShellEnabled = selectedAssetPathsList.every(v => {
            let assetFolder = v.substring(0, v.lastIndexOf('/'))
            return assetFolder === asset0Folder
        })

        if (selectedAssetPathsList.length > 1) {
            deleteFileItem.text = qsTr("Delete Files")
            addTexturesItem.text = qsTr("Add Textures")
        } else {
            deleteFileItem.text = qsTr("Delete File")
            addTexturesItem.text = qsTr("Add Texture")
        }

        rootView.updateContextMenuActionsEnableState()

        root.__onFolderCreated = onFolderCreated
        root.__selectedAssetPathsList = selectedAssetPathsList
        root.__fileIndex = fileIndex
        root.__dirIndex = dirModelIndex
        root.__dirPath = root.assetsModel.filePath(dirModelIndex)
        root.__isDirectory = false
        root.popup()
    }

    function openContextMenuForEmpty(dirPath)
    {
        __showInGraphicalShellEnabled = true

        root.__dirPath = dirPath
        root.__fileIndex = ""
        root.__dirIndex = ""
        root.__isDirectory = false
        root.popup()
    }

    StudioControls.MenuItem {
        text: qsTr("Expand All")
        enabled: root.__allExpandedState !== "all_expanded"
        visible: root.__isDirectory
        height: visible ? implicitHeight : 0
        onTriggered: root.assetsView.expandAll()
    }

    StudioControls.MenuItem {
        text: qsTr("Collapse All")
        enabled: root.__allExpandedState !== "all_collapsed"
        visible: root.__isDirectory
        height: visible ? implicitHeight : 0
        onTriggered: root.assetsView.collapseAll()
    }

    StudioControls.MenuSeparator {
        visible: root.__isDirectory
        height: visible ? StudioTheme.Values.border : 0
    }

    StudioControls.MenuItem {
        id: editInEffectComposerItem
        text: qsTr("Edit in Effect Composer")
        visible: root.__fileIndex && root.__selectedAssetPathsList.length === 1
                 && root.assetsModel.allFilePathsAreComposedEffects(root.__selectedAssetPathsList)
                 && root.rootView.canCreateEffects
        height: editInEffectComposerItem.visible ? editInEffectComposerItem.implicitHeight : 0
        onTriggered: root.rootView.openEffectComposer(root.__selectedAssetPathsList[0])
    }

    StudioControls.MenuItem {
        id: editComponent
        text: qsTr("Edit Component")
        visible: root.__fileIndex && root.__selectedAssetPathsList.length === 1
                 && root.rootView.assetIsImported3d(root.__selectedAssetPathsList[0])
        height: editComponent.visible ? editComponent.implicitHeight : 0
        onTriggered: root.rootView.editAssetComponent(root.__selectedAssetPathsList[0])
    }

    StudioControls.MenuItem {
        id: updateComponent
        text: qsTr("Reimport 3D Asset")
        visible: root.__fileIndex && root.__selectedAssetPathsList.length === 1
                 && root.rootView.assetIsImported3d(root.__selectedAssetPathsList[0])
        height: editComponent.visible ? editComponent.implicitHeight : 0
        onTriggered: root.rootView.updateAssetComponent(root.__selectedAssetPathsList[0])
    }

    StudioControls.MenuItem {
        id: addTexturesItem
        text: qsTr("Add Texture")
        enabled: rootView.hasMaterialLibrary
        visible: root.__fileIndex && root.assetsModel.allFilePathsAreTextures(root.__selectedAssetPathsList)
        height: addTexturesItem.visible ? addTexturesItem.implicitHeight : 0
        onTriggered: root.rootView.addTextures(root.__selectedAssetPathsList)
    }

    StudioControls.MenuItem {
        id: addLightProbes
        text: qsTr("Add Light Probe")
        enabled: rootView.hasMaterialLibrary && rootView.hasSceneEnv
        visible: root.__fileIndex && root.__selectedAssetPathsList.length === 1
                 && root.assetsModel.allFilePathsAreTextures(root.__selectedAssetPathsList)
        height: addLightProbes.visible ? addLightProbes.implicitHeight : 0
        onTriggered: rootView.addLightProbe(root.__selectedAssetPathsList[0])
    }

    StudioControls.MenuItem {
        id: deleteFileItem
        text: qsTr("Delete File")
        visible: root.__fileIndex
        height: deleteFileItem.visible ? deleteFileItem.implicitHeight : 0
        onTriggered: {
            let deleted = root.assetsModel.requestDeleteFiles(root.__selectedAssetPathsList)
            if (!deleted)
                confirmDeleteFiles.open()
        }

        ConfirmDeleteFilesDialog {
            id: confirmDeleteFiles
            parent: root.assetsView
            files: root.__selectedAssetPathsList

            onAccepted: root.assetsView.selectedAssets = {}
        }
    }

    StudioControls.MenuSeparator {
        visible: root.__fileIndex
        height: visible ? StudioTheme.Values.border : 0
    }

    StudioControls.MenuItem {
        text: qsTr("Rename Folder")
        visible: root.__isDirectory
        height: visible ? implicitHeight : 0
        onTriggered: renameFolderDialog.open()

        RenameFolderDialog {
            id: renameFolderDialog
            parent: root.assetsView
            dirPath: root.__dirPath
            dirName: root.__dirName

            onAccepted: root.__onFolderCreated(renameFolderDialog.renamedDirPath)
        }
    }

    StudioControls.MenuItem {
        text: qsTr("New Folder")
        height: visible ? implicitHeight : 0

        onTriggered: root.openNewFolderDialog(root.__dirPath)
    }

    StudioControls.MenuItem {
        text: qsTr("Delete Folder")
        visible: root.__isDirectory
        height: visible ? implicitHeight : 0

        ConfirmDeleteFolderDialog {
            id: confirmDeleteFolderDialog
            parent: root.assetsView
            dirName: root.__dirName
            dirIndex: root.__dirIndex
        }

        onTriggered: {
            if (!root.assetsModel.hasChildren(root.__dirIndex)) {
                // NOTE: the folder may still not be empty -- it doesn't have files visible to the
                // user, but that doesn't mean that there are no other files (e.g. files of unknown
                // types) on disk in this directory.
                root.assetsModel.deleteFolderRecursively(root.__dirIndex)
            } else {
                confirmDeleteFolderDialog.open()
            }
        }
    }

    StudioControls.MenuItem {
        text: qsTr("New Effect")
        visible: root.rootView.canCreateEffects
        height: visible ? implicitHeight : 0

        NewEffectDialog {
            id: newEffectDialog
            parent: root.assetsView.parent
            dirPath: root.__dirPath
        }

        onTriggered: newEffectDialog.open()
    }

    StudioControls.MenuItem {
        text: root.rootView.showInGraphicalShellMsg()

        enabled: root.__showInGraphicalShellEnabled

        onTriggered: {
            if (!root.__fileIndex || root.__selectedAssetPathsList.length > 1)
                root.rootView.showInGraphicalShell(root.__dirPath)
            else
                root.rootView.showInGraphicalShell(root.__selectedAssetPathsList[0])
        }
    }

    StudioControls.MenuItem {
        text: qsTr("Add to Content Library")
        visible: root.__fileIndex && root.assetsModel.allFilePathsAreTextures(root.__selectedAssetPathsList)
        height: visible ? implicitHeight : 0
        onTriggered: root.rootView.addAssetsToContentLibrary(root.__selectedAssetPathsList)
    }
}
