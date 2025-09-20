import qbs

QtcPlugin {
    name: "Boot2Qt"

    Depends { name: "Core" }
    Depends { name: "Debugger" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "QmlDebug" }
    Depends { name: "QtSupport" }
    Depends { name: "RemoteLinux" }
    Depends { name: "Utils" }

    cpp.defines: base.concat("BOOT2QT_LIBRARY")

    Group {
        name: "General"
        files: [
            "qdbutils.cpp",
            "qdbutils.h",
            "qdbconstants.h",
            "qdb_global.h",
            "qdbdevice.cpp",
            "qdbdevice.h",
            "qdbdevicedebugsupport.cpp",
            "qdbdevicedebugsupport.h",
            "qdbmakedefaultappstep.cpp",
            "qdbmakedefaultappstep.h",
            "qdbplugin.cpp",
            "qdbstopapplicationstep.cpp",
            "qdbstopapplicationstep.h",
            "qdbtr.h",
            "qdbqtversion.cpp",
            "qdbqtversion.h",
            "qdbrunconfiguration.cpp",
            "qdbrunconfiguration.h",
        ]
    }

    Group {
        name: "Device Detection"
        prefix: "device-detection/"
        files: [
            "devicedetector.cpp",
            "devicedetector.h",
            "hostmessages.cpp",
            "hostmessages.h",
            "qdbdevicetracker.cpp",
            "qdbdevicetracker.h",
            "qdbwatcher.h",
            "qdbwatcher.cpp",
            "qdbmessagetracker.cpp",
            "qdbmessagetracker.h",
        ]
    }

    Group {
        name: "images"
        prefix: "images/"
        files: [
            "qdbdevice.png",
            "qdbdevice@2x.png",
            "qdbdevicesmall.png",
            "qdbdevicesmall@2x.png",
        ]
        fileTags: "qt.core.resource_data"
        Qt.core.resourcePrefix: "/qdb"
    }
}
