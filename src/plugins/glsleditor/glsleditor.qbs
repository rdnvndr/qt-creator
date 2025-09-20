import qbs 1.0

QtcPlugin {
    name: "GLSLEditor"

    Depends { name: "Qt.widgets" }
    Depends { name: "GLSL" }
    Depends { name: "CPlusPlus" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "TextEditor" }
    Depends { name: "CppEditor" }

    files: [
        "glslautocompleter.cpp",
        "glslautocompleter.h",
        "glslcompletionassist.cpp",
        "glslcompletionassist.h",
        "glsleditor.cpp",
        "glsleditor.h",
        "glsleditorconstants.h",
        "glsleditorplugin.cpp",
        "glsleditortr.h",
        "glslhighlighter.cpp",
        "glslhighlighter.h",
        "glslindenter.cpp",
        "glslindenter.h",
    ]

    Group {
        name: "images"
        files: [
            "images/glslfile.png",
        ]
        fileTags: "qt.core.resource_data"
    }
}
