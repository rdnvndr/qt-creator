// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick3D
import MouseArea3D

Item {
    id: viewRoot
    width: 1024
    height: 768
    visible: true

    property Node activeScene: null
    property int activeViewport: 0
    property var editViews: [null, null, null, null]
    property var usePerspective: [true, false, false, false]
    property var overlayViews: [overlayView0, overlayView1, overlayView2, overlayView3]
    property var cameraControls: [cameraControl0, cameraControl1, cameraControl2, cameraControl3]
    property var viewRects: [viewRect0, viewRect1, viewRect2, viewRect3]
    property var materialOverrides:
        [DebugSettings.None, DebugSettings.None, DebugSettings.None, DebugSettings.None]
    property var showWireframes: [false, false, false, false]
    property var activeEditView: editViews[activeViewport]
    property var activeOverlayView: overlayViews[activeViewport]
    property string sceneId

    property bool showEditLight: false
    property bool showGrid: true
    property bool showLookAt: true
    property bool showSelectionBox: true
    property bool showIconGizmo: true
    property bool showCameraFrustum: false
    property bool showParticleEmitter: false
    property bool globalOrientation: false
    property alias contentItem: contentItem
    property color backgroundGradientColorStart: "#222222"
    property color backgroundGradientColorEnd: "#999999"
    property color gridColor: "#cccccc"
    property color viewportBorderColor: "#aaaaaaaa"
    property bool syncEnvBackground: true
    property string activePreset: "Single"
    property bool flyMode: false
    property bool showCameraSpeed: false
    property string cameraViewMode

    // The presets used to customize the display of the viewports
    property var viewportPresets: {
        "Single": {
            numViewports: 1,
            viewRects: [
                { x: 0.0, y: 0.0, width: 1.0, height: 1.0 }
            ]
        },
        "Quad": {
            numViewports: 4,
            viewRects: [
                { x: 0.0, y: 0.0, width: 0.5, height: 0.5 },
                { x: 0.5, y: 0.0, width: 0.5, height: 0.5 },
                { x: 0.0, y: 0.5, width: 0.5, height: 0.5 },
                { x: 0.5, y: 0.5, width: 0.5, height: 0.5 }
            ]
        },
        "3Left1Right": {
            numViewports: 4,
            viewRects: [
                { x: 0.0,  y: 0.0,  width: 0.25, height: 0.33 },
                { x: 0.0,  y: 0.33, width: 0.25, height: 0.34 },
                { x: 0.0,  y: 0.67, width: 0.25, height: 0.33 },
                { x: 0.25, y: 0.0,  width: 0.75, height: 1.0  }
            ]
        },
        "2Horizontal": {
            numViewports: 2,
            viewRects: [
                { x: 0.0, y: 0.0,  width: 1.0, height: 0.5 },
                { x: 0.0, y: 0.5,  width: 1.0, height: 0.5 }
            ]
        },
        "2Vertical": {
            numViewports: 2,
            viewRects: [
                { x: 0.0, y: 0.0,  width: 0.5, height: 1.0 },
                { x: 0.5, y: 0.0,  width: 0.5, height: 1.0 }
            ]
        }
        //TODO: reset of presets
    };

    enum SelectionMode { Item, Group }
    enum TransformMode { Move, Rotate, Scale }

    property int selectionMode: EditView3D.SelectionMode.Item
    property int transformMode: EditView3D.TransformMode.Move

    property Node selectedNode: null // This is multiSelectionNode in multi-selection case
    property var selectedNodes: [] // All selected nodes
    property int selectionBoxCount: 0
    property alias multiSelectionNode: multiSelectionNode

    property rect viewPortRect: Qt.rect(0, 0, 1000, 1000)
    property Node activeParticleSystem: null
    property bool shuttingDown: false

    property real fps: 0

    signal selectionChanged(var selectedNodes)
    signal commitObjectProperty(var objects, var propNames)
    signal changeObjectProperty(var objects, var propNames)
    signal notifyActiveSceneChange()
    signal notifyActiveViewportChange(int index)

    onUsePerspectiveChanged:      _generalHelper.storeToolState(sceneId, "usePerspective", usePerspective)
    onShowEditLightChanged:       _generalHelper.storeToolState(sceneId, "showEditLight", showEditLight)
    onGlobalOrientationChanged:   _generalHelper.storeToolState(sceneId, "globalOrientation", globalOrientation)
    onShowGridChanged:            _generalHelper.storeToolState(sceneId, "showGrid", showGrid);
    onShowLookAtChanged:          _generalHelper.storeToolState(sceneId, "showLookAt", showLookAt);
    onSyncEnvBackgroundChanged:   _generalHelper.storeToolState(sceneId, "syncEnvBackground", syncEnvBackground);
    onShowSelectionBoxChanged:    _generalHelper.storeToolState(sceneId, "showSelectionBox", showSelectionBox);
    onShowIconGizmoChanged:       _generalHelper.storeToolState(sceneId, "showIconGizmo", showIconGizmo);
    onShowCameraFrustumChanged:   _generalHelper.storeToolState(sceneId, "showCameraFrustum", showCameraFrustum);
    onCameraViewModeChanged:      _generalHelper.storeToolState(sceneId, "cameraViewMode", cameraViewMode)
    onShowParticleEmitterChanged: _generalHelper.storeToolState(sceneId, "showParticleEmitter", showParticleEmitter);
    onSelectionModeChanged:       _generalHelper.storeToolState(sceneId, "selectionMode", selectionMode);
    onTransformModeChanged:       _generalHelper.storeToolState(sceneId, "transformMode", transformMode);
    onMaterialOverridesChanged:   _generalHelper.storeToolState(sceneId, "matOverride", materialOverrides);
    onShowWireframesChanged:      _generalHelper.storeToolState(sceneId, "showWireframe", showWireframes);
    onActivePresetChanged: {
        _generalHelper.storeToolState(sceneId, "activePreset", activePreset);
        _generalHelper.requestOverlayUpdate();
    }
    onActiveViewportChanged: {
        _generalHelper.storeToolState(sceneId, "activeViewport", activeViewport);
        cameraControls[activeViewport].forceActiveFocus();
        notifyActiveViewportChange(activeViewport);
    }

    onActiveSceneChanged: updateActiveScene()

    function aboutToShutDown()
    {
        shuttingDown = true;
    }

    function createEditViews()
    {
        var component = Qt.createComponent("SceneView3D.qml");
        if (component.status === Component.Ready) {
            for (var i = 0; i < 4; ++i) {
                editViews[i] = component.createObject(viewRects[i],
                                                      {"usePerspective": usePerspective[i],
                                                       "showSceneLight": showEditLight,
                                                       "showGrid": showGrid,
                                                       "gridColor": gridColor,
                                                       "importScene": activeScene,
                                                       "cameraLookAt": cameraControls[i]._lookAtPoint,
                                                       "z": 1,
                                                       "sceneEnv.debugSettings.materialOverride": materialOverrides[i],
                                                       "sceneEnv.debugSettings.wireframeEnabled": showWireframes[i],
                                                       "selectedNode": selectedNode});
                editViews[i].showSceneLight = Qt.binding(function() {return showEditLight;});
                editViews[i].showGrid = Qt.binding(function() {return showGrid;});
                editViews[i].gridColor = Qt.binding(function() {return gridColor;});
                editViews[i].selectedNode = Qt.binding(function() {return selectedNode;});
            }
            editViews[0].cameraLookAt = Qt.binding(function() {return cameraControl0._lookAtPoint;});
            editViews[1].cameraLookAt = Qt.binding(function() {return cameraControl1._lookAtPoint;});
            editViews[2].cameraLookAt = Qt.binding(function() {return cameraControl2._lookAtPoint;});
            editViews[3].cameraLookAt = Qt.binding(function() {return cameraControl3._lookAtPoint;});

            editViews[0].sceneEnv.debugSettings.materialOverride = Qt.binding(function() {return materialOverrides[0];});
            editViews[1].sceneEnv.debugSettings.materialOverride = Qt.binding(function() {return materialOverrides[1];});
            editViews[2].sceneEnv.debugSettings.materialOverride = Qt.binding(function() {return materialOverrides[2];});
            editViews[3].sceneEnv.debugSettings.materialOverride = Qt.binding(function() {return materialOverrides[3];});

            editViews[0].usePerspective = Qt.binding(function() {return usePerspective[0];});
            editViews[1].usePerspective = Qt.binding(function() {return usePerspective[1];});
            editViews[2].usePerspective = Qt.binding(function() {return usePerspective[2];});
            editViews[3].usePerspective = Qt.binding(function() {return usePerspective[3];});

            editViews[0].sceneEnv.debugSettings.wireframeEnabled = Qt.binding(function() {return showWireframes[0];});
            editViews[1].sceneEnv.debugSettings.wireframeEnabled = Qt.binding(function() {return showWireframes[1];});
            editViews[2].sceneEnv.debugSettings.wireframeEnabled = Qt.binding(function() {return showWireframes[2];});
            editViews[3].sceneEnv.debugSettings.wireframeEnabled = Qt.binding(function() {return showWireframes[3];});

            selectionBoxCount = 0;
            editViewsChanged();
            cameraControls[activeViewport].forceActiveFocus();
            return true;
        }
        return false;
    }

    function updateActiveScene()
    {
        let viewsDeleted = false;
        for (var i = 0; i < editViews.length; ++i) {
            if (editViews[i]) {
                editViews[i].visible = false;
                editViews[i].destroy();
                editViews[i] = null;
                viewsDeleted = true;
            }
        }

        // importScene cannot be updated after initial set, so we need to reconstruct entire View3D
        if (createEditViews()) {
            if (activeScene) {
                var toolStates = _generalHelper.getToolStates(sceneId);
                if (Object.keys(toolStates).length > 0) {
                    updateToolStates(toolStates, true);
                } else {
                    // Don't inherit the edit light state from the previous scene, but rather
                    // turn the edit light on for scenes that do not have any scene
                    // lights, and turn it off for scenes that have.
                    var hasSceneLight = false;
                    for (var j = 0; j < overlayView0.lightIconGizmos.length; ++j) {
                        if (overlayView0.lightIconGizmos[j].scene === activeScene) {
                            hasSceneLight = true;
                            break;
                        }
                    }
                    showEditLight = !hasSceneLight && !_generalHelper.sceneHasLightProbe(sceneId);

                    // Don't inherit camera angles from the previous scene
                    for (let i = 0; i < 4; ++i)
                        cameraControls[i].restoreDefaultState();

                    storeCurrentToolStates();
                }
            } else {
                // When active scene is deleted, this function gets called by object deletion
                // handlers without going through setActiveScene, so make sure sceneId is cleared.
                // This is skipped during application shutdown, as calling QQuickText::setText()
                // during application shutdown can crash the application.
                if (!shuttingDown) {
                    sceneId = "";
                    storeCurrentToolStates();
                }
            }

            updateEnvBackground();

            notifyActiveSceneChange();
        } else if (viewsDeleted) {
            editViewsChanged();
        }
    }

    function setActiveScene(newScene, newSceneId)
    {
        var needExplicitUpdate = !activeScene && !newScene;

        sceneId = newSceneId;
        activeScene = newScene;

        if (needExplicitUpdate)
            updateActiveScene();
    }

    function handleActiveSceneIdChange(newId)
    {
        if (sceneId !== newId) {
            sceneId = newId;
            storeCurrentToolStates();
        }
    }

    function fitToView()
    {
        if (activeEditView) {
            var boxModels = [];
            if (selectedNodes.length > 1) {
                for (var i = 0; i < selectedNodes.length; ++i) {
                    if (selectionBoxCount > i)
                        boxModels.push(activeEditView.selectionBoxes[i].model)
                }
            } else if (selectedNodes.length > 0 && selectionBoxCount > 0) {
                boxModels.push(activeEditView.selectionBoxes[0].model);
            }
            cameraControls[activeViewport].focusObject(
                        boxModels, activeEditView.camera.eulerRotation, true, false);
        }
    }

    function alignCamerasToView(cameraNodes)
    {
        if (activeEditView) {
            cameraControls[activeViewport].alignCameras(cameraNodes);
            var propertyNames = ["position", "eulerRotation"];
            viewRoot.changeObjectProperty(cameraNodes, propertyNames);
            viewRoot.commitObjectProperty(cameraNodes, propertyNames);
        }
    }

    function alignViewToCamera(cameraNodes)
    {
        if (activeEditView)
            cameraControls[activeViewport].alignView(cameraNodes);
    }

    function updateBackgroundColors(colors)
    {
        if (colors.length === 1) {
            backgroundGradientColorStart = colors[0];
            backgroundGradientColorEnd = colors[0];
        } else {
            backgroundGradientColorStart = colors[0];
            backgroundGradientColorEnd = colors[1];
        }
    }

    function updateEnvBackground()
    {
        updateBackgroundColors(_generalHelper.bgColor);

        if (!editViews[0])
            return;

        for (var i = 0; i < 4; ++i) {
            if (syncEnvBackground) {
                if (_generalHelper.hasSceneEnvironmentData(sceneId)) {
                    let bgMode = _generalHelper.sceneEnvironmentBgMode(sceneId);
                    if ((!_generalHelper.sceneEnvironmentLightProbe(sceneId) && bgMode === SceneEnvironment.SkyBox)
                        || (!_generalHelper.sceneEnvironmentSkyBoxCubeMap(sceneId) && bgMode === SceneEnvironment.SkyBoxCubeMap)) {
                        editViews[i].sceneEnv.backgroundMode = SceneEnvironment.Color;
                    } else {
                        editViews[i].sceneEnv.backgroundMode = bgMode;
                    }
                    editViews[i].sceneEnv.lightProbe = _generalHelper.sceneEnvironmentLightProbe(sceneId);
                    editViews[i].sceneEnv.skyBoxCubeMap = _generalHelper.sceneEnvironmentSkyBoxCubeMap(sceneId);
                    editViews[i].sceneEnv.clearColor = _generalHelper.sceneEnvironmentColor(sceneId);
                } else if (activeScene) {
                    _generalHelper.updateSceneEnvToLast(editViews[i].sceneEnv,
                                                        editViews[i].defaultLightProbe,
                                                        editViews[i].defaultCubeMap);
                }
            } else {
                editViews[i].sceneEnv.backgroundMode = SceneEnvironment.Transparent;
                editViews[i].sceneEnv.lightProbe = null;
                editViews[i].sceneEnv.skyBoxCubeMap = null;
                editViews[i].sceneEnv.clearColor = "transparent";
            }
        }
    }

    // If resetToDefault is true, tool states not specifically set to anything will be reset to
    // their default state.
    function updateToolStates(toolStates, resetToDefault)
    {
        if ("showEditLight" in toolStates)
            showEditLight = toolStates.showEditLight;
        else if (resetToDefault)
            showEditLight = false;

        if ("showGrid" in toolStates)
            showGrid = toolStates.showGrid;
        else if (resetToDefault)
            showGrid = true;

        if ("showLookAt" in toolStates)
            showLookAt = toolStates.showLookAt;
        else if (resetToDefault)
            showLookAt = true;

        if ("syncEnvBackground" in toolStates) {
            syncEnvBackground = toolStates.syncEnvBackground;
            updateEnvBackground();
        } else if (resetToDefault) {
            syncEnvBackground = true;
            updateEnvBackground();
        }

        if ("showSelectionBox" in toolStates)
            showSelectionBox = toolStates.showSelectionBox;
        else if (resetToDefault)
            showSelectionBox = true;

        if ("showIconGizmo" in toolStates)
            showIconGizmo = toolStates.showIconGizmo;
        else if (resetToDefault)
            showIconGizmo = true;

        if ("showCameraFrustum" in toolStates)
            showCameraFrustum = toolStates.showCameraFrustum;
        else if (resetToDefault)
            showCameraFrustum = false;

        if ("cameraViewMode" in toolStates)
            cameraViewMode = toolStates.cameraViewMode
        else if (resetToDefault)
            cameraViewMode = "CameraOff"

        if ("showParticleEmitter" in toolStates)
            showParticleEmitter = toolStates.showParticleEmitter;
        else if (resetToDefault)
            showParticleEmitter = false;

        if ("usePerspective" in toolStates)
            usePerspective = toolStates.usePerspective;
        else if (resetToDefault)
            usePerspective = [true, false, false, false];

        if ("globalOrientation" in toolStates)
            globalOrientation = toolStates.globalOrientation;
        else if (resetToDefault)
            globalOrientation = false;

        if ("selectionMode" in toolStates)
            selectionMode = toolStates.selectionMode;
        else if (resetToDefault)
            selectionMode = EditView3D.SelectionMode.Item;

        if ("transformMode" in toolStates)
            transformMode = toolStates.transformMode;
        else if (resetToDefault)
            transformMode = EditView3D.TransformMode.Move;

        for (var i = 0; i < 4; ++i) {
            let propId = "editCamState" + i;
            if (propId in toolStates)
                cameraControls[i].restoreCameraState(toolStates[propId]);
            else if (resetToDefault)
                cameraControls[i].restoreDefaultState();
        }

        if ("flyMode" in toolStates) {
            flyMode = toolStates.flyMode;
            viewRoot.showCameraSpeed = false;
        } else if (resetToDefault) {
            flyMode = false;
            viewRoot.showCameraSpeed = false;
        }

        if ("activePreset" in toolStates)
            activePreset = toolStates.activePreset;
        else if (resetToDefault)
            activePreset = "Quad";
        applyViewportPreset(activePreset)

        if ("activeViewport" in toolStates)
            activeViewport = toolStates.activeViewport;
        else if (resetToDefault)
            activeViewport = 0;

        if ("showWireframe" in toolStates)
            showWireframes = toolStates.showWireframe;
        else if (resetToDefault)
            showWireframes = [false, false, false, false];

        if ("matOverride" in toolStates)
            materialOverrides = toolStates.matOverride;
        else if (resetToDefault)
            materialOverrides = [DebugSettings.None, DebugSettings.None, DebugSettings.None, DebugSettings.None];
    }

    function storeCurrentToolStates()
    {
        _generalHelper.storeToolState(sceneId, "showEditLight", showEditLight)
        _generalHelper.storeToolState(sceneId, "showGrid", showGrid)
        _generalHelper.storeToolState(sceneId, "showLookAt", showLookAt)
        _generalHelper.storeToolState(sceneId, "syncEnvBackground", syncEnvBackground)
        _generalHelper.storeToolState(sceneId, "showSelectionBox", showSelectionBox)
        _generalHelper.storeToolState(sceneId, "showIconGizmo", showIconGizmo)
        _generalHelper.storeToolState(sceneId, "showCameraFrustum", showCameraFrustum)
        _generalHelper.storeToolState(sceneId, "cameraViewMode", cameraViewMode)
        _generalHelper.storeToolState(sceneId, "showParticleEmitter", showParticleEmitter)
        _generalHelper.storeToolState(sceneId, "usePerspective", usePerspective)
        _generalHelper.storeToolState(sceneId, "globalOrientation", globalOrientation)
        _generalHelper.storeToolState(sceneId, "selectionMode", selectionMode);
        _generalHelper.storeToolState(sceneId, "transformMode", transformMode);
        _generalHelper.storeToolState(sceneId, "activePreset", activePreset)
        _generalHelper.storeToolState(sceneId, "activeViewport", activeViewport)
        _generalHelper.storeToolState(sceneId, "showWireframe", showWireframes)
        _generalHelper.storeToolState(sceneId, "matOverride", materialOverrides)

        for (var i = 0; i < 4; ++i)
            cameraControls[i].storeCameraState(0);
    }

    function ensureSelectionBoxes(count)
    {
        for (var i = 0; i < 4; ++i) {
            if (editViews[i])
                editViews[i].ensureSelectionBoxes(count);
        }

        selectionBoxCount = count;
    }

    function selectObjects(objects)
    {
        // Create selection boxes as necessary. One more box than is actually needed is created, so
        // that we always have a previously created box to use for new selection.
        // This fixes an occasional visual glitch when creating a new box.
        ensureSelectionBoxes(objects.length + 1)

        for (var idx = 0; idx < 4; ++idx) {
            if (editViews[idx]) {
                var i;
                for (i = 0; i < objects.length; ++i)
                    editViews[idx].selectionBoxes[i].targetNode = objects[i];
                for (i = objects.length; i < editViews[idx].selectionBoxes.length; ++i)
                    editViews[idx].selectionBoxes[i].targetNode = null;
            }
        }

        selectedNodes = objects;
        if (objects.length === 0) {
            selectedNode = null;
        } else if (objects.length > 1) {
            selectedNode = multiSelectionNode;
            _generalHelper.setMultiSelectionTargets(multiSelectionNode, objects);
        } else {
            selectedNode = objects[0];
        }
    }

    function handleObjectClicked(object, button, multi)
    {
        if (object instanceof View3D) {
            // View3D can be the resolved pick target in case the 3D editor is showing content
            // of a component that has View3D as root. In that case locking is resolved on C++ side
            // and we ignore multiselection.
            selectObjects([]);
            selectionChanged([object]);
            return;
        }

        var clickedObject;

        // Click on locked object is treated same as click on empty space
        if (!_generalHelper.isLocked(object))
            clickedObject = object;

        if (selectionMode === EditView3D.SelectionMode.Group) {
            while (clickedObject && clickedObject !== activeScene
                   && (activeScene instanceof Model || clickedObject.parent !== activeScene)) {
                clickedObject = clickedObject.parent;
            }
        }
        // Object selection logic:
        // Regular click: Clear any multiselection, single-selects the clicked object
        // Ctrl-click: No objects selected: Act as single select
        //             One or more objects selected: Multiselect
        // Null object always clears entire selection
        var newSelection = [];
        if (clickedObject) {
            if (button === Qt.RightButton) {
                // Right-clicking does only single selection (when clickedObject is unselected)
                // This is needed for selecting a target for the context menu
                if (!selectedNodes.includes(clickedObject))
                    newSelection[0] = clickedObject;
                else
                    newSelection = selectedNodes;
            } else if (multi && selectedNodes.length > 0) {
                var deselect = false;
                for (var i = 0; i < selectedNodes.length; ++i) {
                    // Multiselecting already selected object clears that object from selection
                    if (selectedNodes[i] !== clickedObject)
                        newSelection[newSelection.length] = selectedNodes[i];
                    else
                        deselect = true;
                }
                if (!deselect)
                    newSelection[newSelection.length] = clickedObject;
            } else {
                newSelection[0] = clickedObject;
            }
        }
        selectObjects(newSelection);
        selectionChanged(newSelection);
    }

    //TODO: only update the active viewport views
    function addLightGizmo(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].addLightGizmo(scene, obj);
    }

    function addCameraGizmo(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].addCameraGizmo(scene, obj);
    }

    function addParticleSystemGizmo(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].addParticleSystemGizmo(scene, obj);
    }

    function addParticleEmitterGizmo(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].addParticleEmitterGizmo(scene, obj);
    }

    function addReflectionProbeGizmo(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].addReflectionProbeGizmo(scene, obj);
    }

    function releaseLightGizmo(obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].releaseLightGizmo(obj);
    }

    function releaseCameraGizmo(obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].releaseCameraGizmo(obj);
    }

    function releaseParticleSystemGizmo(obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].releaseParticleSystemGizmo(obj);
    }

    function releaseParticleEmitterGizmo(obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].releaseParticleEmitterGizmo(obj);
    }

    function releaseReflectionProbeGizmo(obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].releaseReflectionProbeGizmo(obj);
    }

    function updateLightGizmoScene(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].updateLightGizmoScene(scene, obj);
    }

    function updateCameraGizmoScene(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].updateCameraGizmoScene(scene, obj);
    }

    function updateParticleSystemGizmoScene(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].updateParticleSystemGizmoScene(scene, obj);
    }

    function updateParticleEmitterGizmoScene(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].updateParticleEmitterGizmoScene(scene, obj);
    }

    function updateReflectionProbeGizmoScene(scene, obj)
    {
        for (var i = 0; i < 4; ++i)
            overlayViews[i].updateReflectionProbeGizmoScene(scene, obj);
    }

    function resolveViewportPoint(x, y)
    {
        let rect = viewRects[activeViewport];
        // Check invisible or out or range, then fallback to original origin
        if (!rect || !rect.visible)
            return Qt.point(x, y);

        // Transform topleft of the active viewport to be the origin
        return Qt.point(x - rect.x, y - rect.y);
    }

    function updateActiveViewport(x, y)
    {
        for (let i = 0; i < 4; ++i) {
            let rect = viewRects[i];
            if (!rect.visible)
                continue;

            if (x >= rect.x && x <= rect.x + rect.width
             && y >= rect.y && y <= rect.y + rect.height) {
                activeViewport = i;
                return;
            }
        }

        // TODO: if click outside all visible viewRects, do nothing
        // or reset to e.g. activeVireport = -1 or 0
    }

    function gizmoAt(x, y)
    {
        updateActiveViewport(x, y);
        let viewportPoint = resolveViewportPoint(x, y);

        return activeOverlayView.gizmoAt(viewportPoint.x, viewportPoint.y);
    }

    function rotateEditCamera(angles)
    {
        cameraControls[activeViewport].rotateCamera(angles);
    }

    function moveEditCamera(amounts)
    {
        cameraControls[activeViewport].moveCamera(amounts);
    }

    // Update viewports based on selected preset
    function applyViewportPreset(presetName)
    {
        let preset = viewportPresets[presetName];
        if (!preset)
            return;

        let count = preset.numViewports;

        for (let i = 0; i < 4; ++i) {
            if (i < count) {
                viewRects[i].visible = true;
                viewRects[i].x = preset.viewRects[i].x * viewContainer.width;
                viewRects[i].y = preset.viewRects[i].y * viewContainer.height;
                viewRects[i].width = preset.viewRects[i].width * viewContainer.width;
                viewRects[i].height = preset.viewRects[i].height * viewContainer.height;
            } else {
                viewRects[i].visible = false;
            }
        }

        //TODO: Do we need this here?
        cameraView.updateSnapping();
    }

    Component.onCompleted: {
        createEditViews();
        selectObjects([]);
        applyViewportPreset(activePreset)
        // Work-around the fact that the projection matrix for the camera is not calculated until
        // the first frame is rendered, so any initial calls to mapFrom3DScene() will fail.
        _generalHelper.requestOverlayUpdate();
    }

    onWidthChanged: {
        applyViewportPreset(activePreset)
        _generalHelper.requestOverlayUpdate()
    }
    onHeightChanged: {
        applyViewportPreset(activePreset)
        _generalHelper.requestOverlayUpdate()
    }

    Connections {
        target: _generalHelper
        function onLockedStateChanged(node)
        {
            for (var i = 0; i < 4; ++i)
                overlayViews[i].handleLockedStateChange(node);
        }

        function onHiddenStateChanged(node)
        {
            for (var i = 0; i < 4; ++i)
                overlayViews[i].handleHiddenStateChange(node);
        }

        function onUpdateDragTooltip()
        {
            gizmoLabel.updateLabel();
            rotateGizmoLabel.updateLabel();
        }

        function onSceneEnvDataChanged()
        {
            updateEnvBackground();
        }

        function onCameraSpeedChanged() {
            _generalHelper.requestTimerEvent("hideSpeed", 1000);
            viewRoot.showCameraSpeed = true
        }

        function onRequestedTimerEvent(timerId) {
            if (timerId === "hideSpeed") {
                viewRoot.showCameraSpeed = false;
                _generalHelper.requestRender();
            }
        }
    }

    // Shared nodes of the overlay, set as importScene on all overlay views.
    // Content here can be used as is on all viewports.
    // Nodes that utilize autoscaling or otherwise need to have different appearance on each viewport
    // need to have separate copy on each viewport.
    Node {
        id: overlayScene

        Node {
            id: multiSelectionNode
            objectName: "multiSelectionNode"
        }
    }

    Item {
        id: contentItem
        anchors.fill: parent

        Item {
            id: viewContainer
            anchors.fill: parent

            Gradient {
                id: bgGradient
                GradientStop { position: 1.0; color: backgroundGradientColorStart }
                GradientStop { position: 0.0; color: backgroundGradientColorEnd }
            }

            Rectangle {
                id: viewRect0
                gradient: bgGradient
                border.width: 1
                border.color: viewportBorderColor
                OverlayView3D {
                    id: overlayView0
                    editView: viewRoot.editViews[0]
                    viewRoot: viewRoot
                    importScene: overlayScene

                    onChangeObjectProperty: (nodes, props) => {
                        viewRoot.changeObjectProperty(nodes, props);
                    }
                    onCommitObjectProperty: (nodes, props) => {
                        viewRoot.commitObjectProperty(nodes, props);
                    }
                }
                EditCameraController {
                    id: cameraControl0
                    viewRoot: viewRoot
                    viewportId: 0
                }
            }

            Rectangle {
                id: viewRect1
                gradient: bgGradient
                border.width: 1
                border.color: viewportBorderColor
                OverlayView3D {
                    id: overlayView1
                    editView: viewRoot.editViews[1]
                    viewRoot: viewRoot
                    importScene: overlayScene

                    onChangeObjectProperty: (nodes, props) => {
                        viewRoot.changeObjectProperty(nodes, props);
                    }
                    onCommitObjectProperty: (nodes, props) => {
                        viewRoot.commitObjectProperty(nodes, props);
                    }
                }
                EditCameraController {
                    id: cameraControl1
                    viewRoot: viewRoot
                    viewportId: 1
                }
            }

            Rectangle {
                id: viewRect2
                gradient: bgGradient
                border.width: 1
                border.color: viewportBorderColor
                OverlayView3D {
                    id: overlayView2
                    editView: viewRoot.editViews[2]
                    viewRoot: viewRoot
                    importScene: overlayScene

                    onChangeObjectProperty: (nodes, props) => {
                        viewRoot.changeObjectProperty(nodes, props);
                    }
                    onCommitObjectProperty: (nodes, props) => {
                        viewRoot.commitObjectProperty(nodes, props);
                    }
                }
                EditCameraController {
                    id: cameraControl2
                    viewRoot: viewRoot
                    viewportId: 2
                }
            }

            Rectangle {
                id: viewRect3
                gradient: bgGradient
                border.width: 1
                border.color: viewportBorderColor
                OverlayView3D {
                    id: overlayView3
                    editView: viewRoot.editViews[3]
                    viewRoot: viewRoot
                    importScene: overlayScene

                    onChangeObjectProperty: (nodes, props) => {
                        viewRoot.changeObjectProperty(nodes, props);
                    }
                    onCommitObjectProperty: (nodes, props) => {
                        viewRoot.commitObjectProperty(nodes, props);
                    }
                }
                EditCameraController {
                    id: cameraControl3
                    viewRoot: viewRoot
                    viewportId: 3
                }
            }

            // Active viewport highlight
            Rectangle {
                visible: activePreset !== "Single" && viewRects[viewRoot.activeViewport].visible
                x: viewRects[viewRoot.activeViewport].x
                y: viewRects[viewRoot.activeViewport].y
                width: viewRects[viewRoot.activeViewport].width
                height: viewRects[viewRoot.activeViewport].height
                border.width: 2
                border.color: "#57B9FC"
                color: "transparent"
                z: 1000 // Edge case to make sure selection rect drawn over everything
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: false
                z: -10

                property MouseArea3D freeDraggerArea
                property point pressPoint
                property bool initialMoveBlock: false

                onPressed: (mouse) => {
                    if (viewRoot.flyMode)
                        return;

                    viewRoot.updateActiveViewport(mouse.x, mouse.y);

                    let viewportPoint = viewRoot.resolveViewportPoint(mouse.x, mouse.y);

                    if (viewRoot.activeEditView) {
                        // First pick overlay to check for hits there
                        var pickResult = _generalHelper.pickViewAt(activeOverlayView,
                                                                   viewportPoint.x, viewportPoint.y);
                        var resolvedResult = _generalHelper.resolvePick(pickResult.objectHit);

                        if (!resolvedResult) {
                            // No hits from overlay view, pick the main scene
                            pickResult = _generalHelper.pickViewAt(viewRoot.activeEditView,
                                                                   viewportPoint.x, viewportPoint.y);
                            resolvedResult = _generalHelper.resolvePick(pickResult.objectHit);
                        }

                        handleObjectClicked(resolvedResult, mouse.button,
                                            mouse.modifiers & Qt.ControlModifier);

                        if (pickResult.objectHit && pickResult.objectHit instanceof Node) {
                            if (transformMode === EditView3D.TransformMode.Move)
                                freeDraggerArea = activeOverlayView.moveGizmo.freeDraggerArea;
                            else if (transformMode === EditView3D.TransformMode.Rotate)
                                freeDraggerArea = activeOverlayView.rotateGizmo.freeDraggerArea;
                            else if (transformMode === EditView3D.TransformMode.Scale)
                                freeDraggerArea = activeOverlayView.scaleGizmo.freeDraggerArea;
                            pressPoint.x = mouse.x;
                            pressPoint.y = mouse.y;
                            initialMoveBlock = true;
                        } else {
                            mouse.accepted = false;
                        }
                    }
                }
                onPositionChanged: (mouse) => {
                    if (freeDraggerArea) {
                        let viewportPoint = viewRoot.resolveViewportPoint(mouse.x, mouse.y);
                        let viewportPress = viewRoot.resolveViewportPoint(pressPoint.x, pressPoint.y);
                        if (initialMoveBlock && Math.abs(viewportPress.x - viewportPoint.x)
                            + Math.abs(viewportPress.y - viewportPoint.y) > 10) {
                            // Don't force press event at actual press, as that puts the gizmo
                            // in free-dragging state, which is bad UX if drag is not actually done
                            freeDraggerArea.forcePressEvent(viewportPress.x, viewportPress.y);
                            freeDraggerArea.forceMoveEvent(viewportPoint.x, viewportPoint.y);
                            initialMoveBlock = false;
                        } else {
                            freeDraggerArea.forceMoveEvent(viewportPoint.x, viewportPoint.y);
                        }
                    }
                }

                function handleRelease(mouse)
                {
                    if (freeDraggerArea) {
                        if (initialMoveBlock) {
                            let viewportPress = viewRoot.resolveViewportPoint(pressPoint.x, pressPoint.y);
                            freeDraggerArea.forceReleaseEvent(viewportPress.x, viewportPress.y);
                        } else {
                            let viewportPoint = viewRoot.resolveViewportPoint(mouse.x, mouse.y);
                            freeDraggerArea.forceReleaseEvent(viewportPoint.x, viewportPoint.y);
                        }
                        freeDraggerArea = null;
                    }
                }

                onReleased: (mouse) => {
                    handleRelease(mouse);
                }
                onCanceled: (mouse) => {
                    handleRelease(mouse);
                }
            }

            Overlay2D {
                id: gizmoLabel
                targetNode: activeOverlayView.moveGizmo.visible ? activeOverlayView.moveGizmo
                                                                : activeOverlayView.scaleGizmo
                targetView: activeOverlayView
                visible: targetNode.dragging
                z: 300

                function updateLabel()
                {
                    // This is skipped during application shutdown, as calling QQuickText::setText()
                    // during application shutdown can crash the application.
                    if (!gizmoLabel.visible || !viewRoot.selectedNode || shuttingDown)
                        return;
                    var targetProperty;
                    if (gizmoLabel.targetNode === activeOverlayView.moveGizmo)
                        gizmoLabelText.text = _generalHelper.snapPositionDragTooltip(viewRoot.selectedNode.position);
                    else
                        gizmoLabelText.text = _generalHelper.snapScaleDragTooltip(viewRoot.selectedNode.scale);
                }

                Connections {
                    target: viewRoot.selectedNode
                    function onPositionChanged() { gizmoLabel.updateLabel() }
                    function onScaleChanged() { gizmoLabel.updateLabel() }
                }

                onVisibleChanged: gizmoLabel.updateLabel()

                Rectangle {
                    color: "white"
                    x: -width / 2
                    y: -height - 8
                    width: gizmoLabelText.width + 4
                    height: gizmoLabelText.height + 4
                    border.width: 1
                    Text {
                        id: gizmoLabelText
                        anchors.centerIn: parent
                    }
                }
            }

            Rectangle {
                id: rotateGizmoLabel
                color: "white"
                width: rotateGizmoLabelText.width + 4
                height: rotateGizmoLabelText.height + 4
                border.width: 1
                visible: activeOverlayView.rotateGizmo.dragging
                z: 300

                Connections {
                    target: activeOverlayView.rotateGizmo
                    function onCurrentMousePosChanged()
                    {
                        let newPos = viewContainer.mapFromItem(
                                activeOverlayView,
                                activeOverlayView.rotateGizmo.currentMousePos.x,
                                activeOverlayView.rotateGizmo.currentMousePos.y);
                        rotateGizmoLabel.x = newPos.x - (10 + rotateGizmoLabel.width);
                        rotateGizmoLabel.y = newPos.y - (10 + rotateGizmoLabel.height);
                    }
                }

                function updateLabel()
                {
                    // This is skipped during application shutdown, as calling QQuickText::setText()
                    // during application shutdown can crash the application.
                    if (!rotateGizmoLabel.visible || !activeOverlayView.rotateGizmo.targetNode || shuttingDown)
                        return;
                    var degrees = activeOverlayView.rotateGizmo.currentAngle * (180 / Math.PI);
                    rotateGizmoLabelText.text = _generalHelper.snapRotationDragTooltip(degrees);
                }

                onVisibleChanged: rotateGizmoLabel.updateLabel()

                Connections {
                    target: activeOverlayView.rotateGizmo
                    function onCurrentAngleChanged() { rotateGizmoLabel.updateLabel() }
                }

                Text {
                    id: rotateGizmoLabelText
                    anchors.centerIn: parent
                }
            }

            Rectangle {
                id: lightGizmoLabel
                color: "white"
                width: lightGizmoLabelText.width + 4
                height: lightGizmoLabelText.height + 4
                border.width: 1
                visible: activeOverlayView.lightGizmo.dragging
                z: 300

                Connections {
                    target: activeOverlayView.lightGizmo
                    function onCurrentMousePosChanged()
                    {
                        let newPos = viewContainer.mapFromItem(
                                activeOverlayView,
                                activeOverlayView.lightGizmo.currentMousePos.x,
                                activeOverlayView.lightGizmo.currentMousePos.y);
                        lightGizmoLabel.x = newPos.x - (10 + lightGizmoLabel.width);
                        lightGizmoLabel.y = newPos.y - (10 + lightGizmoLabel.height);
                    }
                }

                Text {
                    id: lightGizmoLabelText
                    text: activeOverlayView.lightGizmo.currentLabel
                    anchors.centerIn: parent
                }
            }

            CameraView {
                id: cameraView

                showCameraView: viewRoot.cameraViewMode === "ShowSelectedCamera"
                alwaysOn: viewRoot.cameraViewMode === "AlwaysShowCamera"
                targetNode: viewRoot.selectedNode
                activeScene: viewRoot.activeScene
                activeSceneEnvironment: viewRoot.activeEditView.sceneEnv
                preferredCamera: _generalHelper.activeScenePreferredCamera
                preferredSize: Qt.size(viewRoot.width * 0.3, viewRoot.height * 0.3)
                viewPortSize: Qt.size(viewRoot.viewPortRect.width, viewRoot.viewPortRect.height)

                function updateSnapping() {
                    const rect = viewRoot.viewRects[viewRoot.activeViewport];
                    if (!rect || !rect.visible)
                        return;

                    const centerX = rect.x + rect.width / 2;
                    cameraView.snapLeft = centerX < viewContainer.width / 2;
                }

                Connections {
                    target: viewRoot
                    onActiveViewportChanged: cameraView.updateSnapping()
                }
            }
        }

        Text {
            id: sceneLabel
            text: viewRoot.sceneId
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 4
            font.pixelSize: 14
            color: "white"
        }

        Text {
            id: fpsLabel
            text: viewRoot.fps
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 4
            font.pixelSize: 12
            color: "white"
            visible: viewRoot.fps > 0
        }

        Rectangle {
            id: cameraSpeedLabel
            width: 120
            height: 65
            anchors.centerIn: parent
            opacity: 0.6
            radius: 10
            color: "white"
            visible: flyMode && viewRoot.showCameraSpeed
            enabled: false

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "Camera Speed"
                    font.pixelSize: 16
                }
                Text {
                    width: parent.width
                    height: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 20
                    text: _generalHelper.cameraSpeed.toLocaleString(Qt.locale(), 'f', 1)
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Control:
        case Qt.Key_Shift:
            gizmoLabel.updateLabel();
            rotateGizmoLabel.updateLabel();
            break;
        default:
            break;
        }
        event.accepted = false;
    }

    Keys.onReleased: (event) => {
        switch (event.key) {
        case Qt.Key_Control:
        case Qt.Key_Shift:
            gizmoLabel.updateLabel();
            rotateGizmoLabel.updateLabel();
            break;
        default:
            break;
        }
        event.accepted = false;
    }
}
