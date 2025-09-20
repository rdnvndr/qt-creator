// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import HelperWidgets 2.0 as HelperWidgets
import StudioControls as StudioControls
import StudioTheme as StudioTheme

Rectangle {
    id: root

    property HelperWidgets.QmlMaterialNodeProxy backend: materialNodeBackend
    property alias pinned: pinButton.checked
    property alias showPinButton: pinButton.visible

    property StudioTheme.ControlStyle buttonStyle: StudioTheme.ViewBarButtonStyle {
        // This is how you can override stuff from the control styles
        baseIconFontSize: StudioTheme.Values.bigIconFontSize
    }

    Connections {
        target: HelperWidgets.Controller

        function onCloseContextMenu() {
            root.closeContextMenu()
        }
    }

    implicitHeight: image.height

    clip: true
    color: "#000000"

    // Called from C++ to close context menu on focus out
    function closeContextMenu()
    {
        modelMenu.close()
        envMenu.close()
    }

    function refreshPreview()
    {
        image.source = ""
        image.source = "image://nodeInstance/preview"
    }

    Connections {
        target: root.backend

        function onPreviewEnvChanged() {
            envMenu.updateEnvParams(backend.previewEnv)
            root.refreshPreview()
        }

        function onPreviewModelChanged() {
            root.refreshPreview()
        }
    }

    Image {
        id: image

        anchors.fill: parent
        fillMode: Image.PreserveAspectFit

        source: "image://nodeInstance/preview"
        cache: false
        smooth: true

        sourceSize.width: image.width
        sourceSize.height: image.height

        Rectangle {
            id: toolbarRect

            radius: 10
            color: StudioTheme.Values.themeToolbarBackground
            width: optionsToolbar.width + 2 * toolbarRect.radius
            height: optionsToolbar.height + toolbarRect.radius
            anchors.left: parent.left
            anchors.leftMargin: -toolbarRect.radius
            anchors.verticalCenter: parent.verticalCenter

            Column {
                id: optionsToolbar

                spacing: 5
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: optionsToolbar.spacing

                HelperWidgets.AbstractButton {
                    id: pinButton

                    style: root.buttonStyle
                    buttonIcon: pinButton.checked ? StudioTheme.Constants.pin : StudioTheme.Constants.unpin
                    checkable: true
                }

                HelperWidgets.AbstractButton {
                    id: previewEnvMenuButton

                    style: root.buttonStyle
                    buttonIcon: StudioTheme.Constants.textures_medium
                    tooltip: qsTr("Select preview environment.")
                    onClicked: envMenu.popup()
                }

                HelperWidgets.AbstractButton {
                    id: previewModelMenuButton

                    style: root.buttonStyle
                    buttonIcon: StudioTheme.Constants.cube_medium
                    tooltip: qsTr("Select preview model.")
                    onClicked: modelMenu.popup()
                }
            }
        }
    }

    StudioControls.Menu {
        id: modelMenu

        closePolicy: StudioControls.Menu.CloseOnEscape | StudioControls.Menu.CloseOnPressOutside

        ListModel {
            id: modelMenuModel
            ListElement {
                modelName: qsTr("Cone")
                modelStr: "#Cone"
            }
            ListElement {
                modelName: qsTr("Cube")
                modelStr: "#Cube"
            }
            ListElement {
                modelName: qsTr("Cylinder")
                modelStr: "#Cylinder"
            }
            ListElement {
                modelName: qsTr("Sphere")
                modelStr: "#Sphere"
            }
        }

        Repeater {
            model: modelMenuModel
            StudioControls.MenuItemWithIcon {
                text: modelName
                onClicked: root.backend.previewModel = modelStr
                checkable: true
                checked: root.backend.previewModel === modelStr
            }
        }
    }

    StudioControls.Menu {
        id: envMenu

        property string previewEnvName
        property string previewEnvValue

        signal envParametersChanged()

        closePolicy: StudioControls.Menu.CloseOnEscape | StudioControls.Menu.CloseOnPressOutside

        Component.onCompleted: envMenu.updateEnvParams(root.backend.previewEnv)

        function updateEnvParams(str: string) {
            let eqFound = str.lastIndexOf("=")
            let newEnvName = (eqFound > 0) ? str.substr(0, eqFound) : str
            let newEnvValue = (eqFound > 0) ? str.substr(eqFound + 1, str.length - eqFound) : ""

            if (envMenu.previewEnvName !== newEnvName
                    || envMenu.previewEnvValue !== newEnvValue) {
                envMenu.previewEnvName = newEnvName
                envMenu.previewEnvValue = newEnvValue
                envMenu.envParametersChanged()
            }
        }

        EnvMenuItem {
            envName: qsTr("Basic")
            envStr: "Basic"
        }

        EnvMenuItem {
            id: colorItem

            property color color
            property bool colorIsValid: false

            envName: qsTr("Color")
            envStr: "Color"
            checked: false

            Component.onCompleted: update()
            onColorIsValidChanged: updatePopupOriginalColor()

            onClicked: {
                colorItem.updatePopupOriginalColor()
                colorPopup.open(colorItem)
            }

            onColorChanged: {
                colorItem.envStr = colorItem.checked
                        ? "Color=" + color.toString()
                        : "Color"
                colorItem.commit()
            }

            function updatePopupOriginalColor() {
                if (colorItem.colorIsValid)
                    colorPopup.originalColor = colorItem.color
            }

            function update() {
                colorItem.checked = envMenu.previewEnvName === "Color"
                if (colorItem.checked && envMenu.previewEnvValue) {
                    colorItem.color = envMenu.previewEnvValue
                    colorItem.colorIsValid = true
                } else {
                    colorItem.colorIsValid = false
                }
            }

            Connections {
                target: envMenu
                function onEnvParametersChanged() {
                    colorItem.update();
                }
            }
        }

        EnvMenuItem {
            envName: qsTr("Studio")
            envStr: "SkyBox=preview_studio"
        }

        EnvMenuItem {
            envName: qsTr("Landscape")
            envStr: "SkyBox=preview_landscape"
        }
    }

    ColorEditorPopup {
        id: colorPopup

        currentColor: colorItem.color
        onActivateColor: (color) => colorItem.color = color
    }

    component EnvMenuItem: StudioControls.MenuItemWithIcon {
        required property string envName
        property string envStr

        function commit() {
            root.backend.previewEnv = envStr
        }

        text: envName
        onClicked: commit()
        checkable: false
        checked: root.backend.previewEnv === envStr
    }
}
