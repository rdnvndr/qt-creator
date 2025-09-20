// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import HelperWidgets
import StudioControls as StudioControls
import StudioTheme as StudioTheme
import MaterialBrowserBackend

StudioControls.Menu {
    id: root

    property int textureInternalId: -1
    property int textureIndex: -1

    property var materialBrowserTexturesModel: MaterialBrowserBackend.materialBrowserTexturesModel

    function popupMenu(targetTexture = null)
    {
        root.textureInternalId = targetTexture ? targetTexture.textureInternalId : -1
        root.textureIndex = targetTexture ? targetTexture.index : -1

        materialBrowserTexturesModel.updateSceneEnvState()
        materialBrowserTexturesModel.updateSelectionState()

        popup()
    }

    closePolicy: StudioControls.Menu.CloseOnEscape | StudioControls.Menu.CloseOnPressOutside

    onClosed: {
        root.textureIndex = -1
        root.textureInternalId = -1
    }

    StudioControls.MenuItem {
        text: qsTr("Apply to selected model")
        enabled: root.textureInternalId >= 0 && materialBrowserTexturesModel.hasSingleModelSelection
        onTriggered: materialBrowserTexturesModel.applyToSelectedModel(root.textureInternalId)
    }

    StudioControls.MenuItem {
        text: qsTr("Apply to selected material(s)")
        enabled: root.textureInternalId >= 0 && materialBrowserTexturesModel.onlyMaterialsSelected
        onTriggered: materialBrowserTexturesModel.applyToSelectedMaterial(root.textureInternalId)
    }

    StudioControls.MenuItem {
        text: qsTr("Apply as light probe")
        enabled: root.textureInternalId >= 0 && materialBrowserTexturesModel.hasSceneEnv
        onTriggered: materialBrowserTexturesModel.applyAsLightProbe(root.textureInternalId)
    }

    StudioControls.MenuSeparator {}

    StudioControls.MenuItem {
        text: qsTr("Duplicate")
        enabled: root.textureInternalId >= 0
        onTriggered: materialBrowserTexturesModel.duplicateTexture(root.textureIndex)
    }

    StudioControls.MenuItem {
        text: qsTr("Delete")
        enabled: root.textureInternalId >= 0
        onTriggered: materialBrowserTexturesModel.deleteTexture(root.textureIndex)
    }

    StudioControls.MenuSeparator {}

    StudioControls.MenuItem {
        text: qsTr("Create New Texture")
        onTriggered: materialBrowserTexturesModel.addNewTexture()
    }
}
