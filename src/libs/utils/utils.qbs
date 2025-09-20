import qbs.FileInfo

QtcLibrary {
    name: "Utils"
    Properties { cpp.includePaths: base.concat("mimetypes2", ".") }
    cpp.defines: base.concat(["UTILS_LIBRARY"])
    Properties { cpp.dynamicLibraries: base }

    Properties {
        condition: qbs.targetOS.contains("windows")
        cpp.dynamicLibraries: {
            var winLibs = ["user32", "iphlpapi", "ws2_32", "shell32", "ole32"];
            if (qbs.toolchain.contains("mingw")) {
                winLibs.push("uuid");
                if (libarchive_static.present)
                    winLibs.push("bcrypt");
            }
            if (qbs.toolchain.contains("msvc"))
                winLibs.push("dbghelp");
            return winLibs;
        }
    }
    Properties {
        condition: qbs.targetOS.contains("unix")
        cpp.dynamicLibraries: {
            var unixLibs = [];
            if (!qbs.targetOS.contains("macos"))
                unixLibs.push("X11");
            if (!qbs.targetOS.contains("openbsd"))
                unixLibs.push("pthread");
            return unixLibs;
        }
    }

    cpp.enableExceptions: true

    Properties {
        condition: qbs.targetOS.contains("macos")
        cpp.frameworks: ["Foundation", "AppKit"]
    }

    Depends { name: "Qt"; submodules: ["concurrent", "core-private", "network", "printsupport", "qml", "widgets", "xml"] }
    Depends { name: "Qt.macextras"; condition: Qt.core.versionMajor < 6 && qbs.targetOS.contains("macos") }
    Depends { name: "Spinner" }
    Depends { name: "Tasking" }
    Depends { name: "ptyqt" }
    Depends { name: "libarchive_static"; required: false} // in fact it's a hard dependency

    Properties {
        condition: libarchive_static.present
        cpp.includePaths: libarchive_static.libarchiveIncludeDir
        cpp.libraryPaths: libarchive_static.libarchiveLibDir
        cpp.staticLibraries: libarchive_static.libarchiveStatic
                             ? libarchive_static.libarchiveNames : []
        cpp.dynamicLibraries: !libarchive_static.libarchiveStatic
                              ? libarchive_static.libarchiveNames : []
    }

    files: [
        "action.cpp",
        "action.h",
        "algorithm.h",
        "ansiescapecodehandler.cpp",
        "ansiescapecodehandler.h",
        "appinfo.cpp",
        "appinfo.h",
        "appmainwindow.cpp",
        "appmainwindow.h",
        "aspects.cpp",
        "aspects.h",
        "async.cpp",
        "async.h",
        "basetreeview.cpp",
        "basetreeview.h",
        "benchmarker.cpp",
        "benchmarker.h",
        "buildablehelperlibrary.cpp",
        "buildablehelperlibrary.h",
        "builderutils.h",
        "camelcasecursor.cpp",
        "camelcasecursor.h",
        "categorysortfiltermodel.cpp",
        "categorysortfiltermodel.h",
        "changeset.cpp",
        "changeset.h",
        "checkablemessagebox.cpp",
        "checkablemessagebox.h",
        "clangutils.cpp",
        "clangutils.h",
        "classnamevalidatinglineedit.cpp",
        "classnamevalidatinglineedit.h",
        "codegeneration.cpp",
        "codegeneration.h",
        "commandline.cpp",
        "commandline.h",
        "completinglineedit.cpp",
        "completinglineedit.h",
        "completingtextedit.cpp",
        "completingtextedit.h",
        "cpplanguage_details.h",
        "crumblepath.cpp",
        "crumblepath.h",
        "datafromprocess.h",
        "delegates.cpp",
        "delegates.h",
        "detailsbutton.cpp",
        "detailsbutton.h",
        "detailswidget.cpp",
        "detailswidget.h",
        "devicefileaccess.cpp",
        "devicefileaccess.h",
        "deviceshell.cpp",
        "deviceshell.h",
        "differ.cpp",
        "differ.h",
        "displayname.cpp",
        "displayname.h",
        "dropsupport.cpp",
        "dropsupport.h",
        "elfreader.cpp",
        "elfreader.h",
        "elidinglabel.cpp",
        "elidinglabel.h",
        "environment.cpp",
        "environment.h",
        "environmentdialog.cpp",
        "environmentdialog.h",
        "environmentmodel.cpp",
        "environmentmodel.h",
        "execmenu.cpp",
        "execmenu.h",
        "externalterminalprocessimpl.cpp",
        "externalterminalprocessimpl.h",
        "fadingindicator.cpp",
        "fadingindicator.h",
        "faketooltip.cpp",
        "faketooltip.h",
        "fancylineedit.cpp",
        "fancylineedit.h",
        "fancymainwindow.cpp",
        "fancymainwindow.h",
        "filecrumblabel.cpp",
        "filecrumblabel.h",
        "fileinprojectfinder.cpp",
        "fileinprojectfinder.h",
        "filenamevalidatinglineedit.cpp",
        "filenamevalidatinglineedit.h",
        "filepath.cpp",
        "filepath.h",
        "filesearch.cpp",
        "filesearch.h",
        "filestreamer.cpp",
        "filestreamer.h",
        "filestreamermanager.cpp",
        "filestreamermanager.h",
        "filesystemmodel.cpp",
        "filesystemmodel.h",
        "filesystemwatcher.cpp",
        "filesystemwatcher.h",
        "fileutils.cpp",
        "fileutils.h",
        "filewizardpage.cpp",
        "filewizardpage.h",
        "futuresynchronizer.cpp",
        "futuresynchronizer.h",
        "fuzzymatcher.cpp",
        "fuzzymatcher.h",
        "globalfilechangeblocker.cpp",
        "globalfilechangeblocker.h",
        "guard.cpp",
        "guard.h",
        "guardedcallback.h",
        "guiutils.cpp",
        "guiutils.h",
        "highlightingitemdelegate.cpp",
        "highlightingitemdelegate.h",
        "historycompleter.cpp",
        "historycompleter.h",
        "hostosinfo.h",
        "hostosinfo.cpp",
        "htmldocextractor.cpp",
        "htmldocextractor.h",
        "icon.cpp",
        "icon.h",
        "icondisplay.cpp",
        "icondisplay.h",
        "id.cpp",
        "id.h",
        "indexedcontainerproxyconstiterator.h",
        "infobar.cpp",
        "infobar.h",
        "infolabel.cpp",
        "infolabel.h",
        "itemviews.cpp",
        "itemviews.h",
        "jsontreeitem.cpp",
        "jsontreeitem.h",
        "layoutbuilder.cpp",
        "layoutbuilder.h",
        "link.cpp",
        "link.h",
        "listmodel.h",
        "listutils.h",
        "lua.cpp",
        "lua.h",
        "macroexpander.cpp",
        "macroexpander.h",
        "markdownbrowser.cpp",
        "markdownbrowser.h",
        "mathutils.cpp",
        "mathutils.h",
        "mimeconstants.h",
        "mimeutils.h",
        "minimizableinfobars.cpp",
        "minimizableinfobars.h",
        "multitextcursor.cpp",
        "multitextcursor.h",
        "movie.cpp",
        "movie.h",
        "namevaluedictionary.cpp",
        "namevaluedictionary.h",
        "namevalueitem.cpp",
        "namevalueitem.h",
        "namevaluesdialog.cpp",
        "namevaluesdialog.h",
        "namevaluevalidator.cpp",
        "namevaluevalidator.h",
        "navigationtreeview.cpp",
        "navigationtreeview.h",
        "networkaccessmanager.cpp",
        "networkaccessmanager.h",
        "optionpushbutton.h",
        "optionpushbutton.cpp",
        "osspecificaspects.h",
        "outputformat.h",
        "outputformatter.cpp",
        "outputformatter.h",
        "overlaywidget.cpp",
        "overlaywidget.h",
        "overridecursor.cpp",
        "overridecursor.h",
        "passworddialog.cpp",
        "passworddialog.h",
        "pathchooser.cpp",
        "pathchooser.h",
        "pathlisteditor.cpp",
        "pathlisteditor.h",
        "persistentsettings.cpp",
        "persistentsettings.h",
        "pointeralgorithm.h",
        "port.cpp",
        "port.h",
        "portlist.cpp",
        "portlist.h",
        "predicates.h",
        "qtcprocess.cpp",
        "qtcprocess.h",
        "processenums.h",
        "processhandle.cpp",
        "processhandle.h",
        "processhelper.cpp",
        "processhelper.h",
        "processinfo.cpp",
        "processinfo.h",
        "processinterface.cpp",
        "processinterface.h",
        "processreaper.cpp",
        "processreaper.h",
        "progressdialog.cpp",
        "progressdialog.h",
        "progressindicator.cpp",
        "progressindicator.h",
        "projectintropage.cpp",
        "projectintropage.h",
        "proxyaction.cpp",
        "proxyaction.h",
        "qrcparser.cpp",
        "qrcparser.h",
        "qtcassert.cpp",
        "qtcassert.h",
        "qtcolorbutton.cpp",
        "qtcolorbutton.h",
        "qtcsettings.cpp",
        "qtcsettings.h",
        "qtcwidgets.cpp",
        "qtcwidgets.h",
        "ranges.h",
        "reloadpromptutils.cpp",
        "reloadpromptutils.h",
        "removefiledialog.cpp",
        "removefiledialog.h",
        "result.cpp",
        "result.h",
        "savefile.cpp",
        "savefile.h",
        "shutdownguard.cpp",
        "shutdownguard.h",
        "scopedswap.h",
        "scopedtimer.cpp",
        "scopedtimer.h",
        "searchresultitem.cpp",
        "searchresultitem.h",
        "set_algorithm.h",
        "settingsaccessor.cpp",
        "settingsaccessor.h",
        "settingsselector.cpp",
        "settingsselector.h",
        "sizedarray.h",
        "smallstring.h",
        "smallstringiterator.h",
        "smallstringio.h",
        "smallstringliteral.h",
        "smallstringlayout.h",
        "smallstringmemory.h",
        "smallstringvector.h",
        "sortfiltermodel.h",
        "span.h",
        "../3rdparty/span/span.hpp",
        "statuslabel.cpp",
        "statuslabel.h",
        "store.cpp",
        "store.h",
        "storekey.h",
        "stringtable.cpp",
        "stringtable.h",
        "stringutils.cpp",
        "stringutils.h",
        "styleanimator.cpp",
        "styleanimator.h",
        "styledbar.cpp",
        "styledbar.h",
        "stylehelper.cpp",
        "stylehelper.h",
        "summarywidget.cpp",
        "summarywidget.h",
        "synchronizedvalue.h",
        "templateengine.cpp",
        "templateengine.h",
        "temporarydirectory.cpp",
        "temporarydirectory.h",
        "temporaryfile.cpp",
        "temporaryfile.h",
        "terminalcommand.cpp",
        "terminalcommand.h",
        "terminalhooks.cpp",
        "terminalhooks.h",
        "terminalinterface.cpp",
        "terminalinterface.h",
        "textfieldcheckbox.cpp",
        "textfieldcheckbox.h",
        "textfieldcombobox.cpp",
        "textfieldcombobox.h",
        "textfileformat.cpp",
        "textfileformat.h",
        "textutils.cpp",
        "textutils.h",
        "threadutils.cpp",
        "threadutils.h",
        "transientscroll.cpp",
        "transientscroll.h",
        "treemodel.cpp",
        "treemodel.h",
        "treeviewcombobox.cpp",
        "treeviewcombobox.h",
        "headerviewstretcher.cpp",
        "headerviewstretcher.h",
        "unarchiver.cpp",
        "unarchiver.h",
        "uncommentselection.cpp",
        "uncommentselection.h",
        "uniqueobjectptr.h",
        "unixutils.cpp",
        "unixutils.h",
        "url.cpp",
        "url.h",
        "utils.qrc",
        "utils_global.h",
        "utilsicons.h",
        "utilsicons.cpp",
        "utilstr.h",
        "variablechooser.cpp",
        "variablechooser.h",
        "winutils.cpp",
        "winutils.h",
        "wizard.cpp",
        "wizard.h",
        "wizardpage.cpp",
        "wizardpage.h",
        "images/*.png",
    ]

    Group {
        name: "FSEngine"
        prefix: "fsengine/"
        cpp.defines: outer.concat("QTC_UTILS_WITH_FSENGINE")
        files: [
            "diriterator.h",
            "fileiconprovider.cpp",
            "fileiconprovider.h",
            "fileiteratordevicesappender.h",
            "fixedlistfsengine.h",
            "fsengine.cpp",
            "fsengine.h",
            "fsenginehandler.cpp",
            "fsenginehandler.h",
        ]
    }

    Group {
        name: "Theme"
        prefix: "theme/"
        files: [
            "theme.cpp",
            "theme.h",
            "theme_p.h",
        ]
    }

    Group {
        name: "Tooltip"
        prefix: "tooltip/"
        files: [
            "effects.h",
            "tips.cpp",
            "tips.h",
            "tooltip.cpp",
            "tooltip.h",
        ]
    }

    Group {
        name: "FileUtils_macos"
        condition: qbs.targetOS.contains("macos")
        files: [
            "fileutils_mac.h", "fileutils_mac.mm",
        ]
    }

    Group {
        name: "Theme_macos"
        condition: qbs.targetOS.contains("macos")
        prefix: "theme/"
        files: [
            "theme_mac.h", "theme_mac.mm",
        ]
    }

    Group {
        name: "ProcessHandle_macos"
        condition: qbs.targetOS.contains("macos")
        files: [
            "processhandle_mac.mm",
        ]
    }

    Group {
        name: "MimeTypes"
        prefix: "mimetypes2/"
        files: [
            "mimedatabase.cpp",
            "mimedatabase.h",
            "mimedatabase_p.h",
            "mimeglobpattern.cpp",
            "mimeglobpattern_p.h",
            "mimemagicrule.cpp",
            "mimemagicrule_p.h",
            "mimemagicrulematcher.cpp",
            "mimemagicrulematcher_p.h",
            "mimeprovider.cpp",
            "mimeprovider_p.h",
            "mimetype.cpp",
            "mimetype.h",
            "mimetype_p.h",
            "mimetypeparser.cpp",
            "mimetypeparser_p.h",
            "mimeutils.cpp"
        ]
    }

    Group {
        name: "TouchBar support"
        prefix: "touchbar/"
        files: "touchbar.h"
        Group {
            name: "TouchBar implementation"
            condition: qbs.targetOS.contains("macos")
            files: [
                "touchbar_appdelegate_mac_p.h",
                "touchbar_mac_p.h",
                "touchbar_mac.mm",
                "touchbar_appdelegate_mac.mm",
            ]
        }
        Group {
            name: "TouchBar stub"
            condition: !qbs.targetOS.contains("macos")
            files: "touchbar.cpp"
        }
    }

    Group {
        name: "PlainTextEdit"
        prefix: "plaintextedit/"
        files: [
            "inputcontrol.cpp",
            "inputcontrol.h",
            "plaintextedit.cpp",
            "plaintextedit.h",
            "widgettextcontrol.cpp",
            "widgettextcontrol.h",
            "plaintexteditaccessibility.cpp",
            "plaintexteditaccessibility.h",
        ]
    }

    Export {
        Depends { name: "Qt"; submodules: ["concurrent", "widgets" ] }
        Depends { name: "Tasking" }
        cpp.includePaths: exportingProduct.sourceDirectory + "/mimetypes2"
    }
}
