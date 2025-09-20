// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "procinterrupt.h"
#include "debuggerconstants.h"

#include <QDir>
#include <QGuiApplication>
#include <QProcess> // makes kill visible on Windows.

using namespace Debugger::Internal;

static inline QString msgCannotInterrupt(qint64 pid, const QString &why)
{
    return QString::fromLatin1("Cannot interrupt process %1: %2").arg(pid).arg(why);
}

#if defined(Q_OS_WIN)
#include <utils/winutils.h>
#include <windows.h>

#if !defined(PROCESS_SUSPEND_RESUME) // Check flag for MinGW
#    define PROCESS_SUSPEND_RESUME (0x0800)
#endif // PROCESS_SUSPEND_RESUME

static BOOL isWow64Process(HANDLE hproc)
{
    using LPFN_ISWOW64PROCESS = BOOL (WINAPI*)(HANDLE, PBOOL);

    BOOL ret = false;

    static LPFN_ISWOW64PROCESS fnIsWow64Process = NULL;
    if (!fnIsWow64Process) {
        if (HMODULE hModule = GetModuleHandle(L"kernel32.dll"))
            fnIsWow64Process = reinterpret_cast<LPFN_ISWOW64PROCESS>(GetProcAddress(hModule, "IsWow64Process"));
    }

    if (!fnIsWow64Process) {
        qWarning("Cannot retrieve symbol 'IsWow64Process'.");
        return false;
    }

    if (!fnIsWow64Process(hproc, &ret)) {
        qWarning("IsWow64Process() failed for %p: %s",
                 hproc, qPrintable(Utils::winErrorMessage(GetLastError())));
        return false;
    }
    return ret;
}

// Open the process and break into it
bool Debugger::Internal::interruptProcess(qint64 pID, QString *errorMessage)
{
    bool ok = false;
    HANDLE inferior = NULL;
    do {
        const DWORD rights = PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION
                |PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ
                |PROCESS_DUP_HANDLE|PROCESS_TERMINATE|PROCESS_CREATE_THREAD|PROCESS_SUSPEND_RESUME;
        inferior = OpenProcess(rights, FALSE, DWORD(pID));
        if (inferior == NULL) {
            *errorMessage = QString::fromLatin1("Cannot open process %1: %2").
                    arg(pID).arg(Utils::winErrorMessage(GetLastError()));
            break;
        }

        enum DebugBreakApi {
            UseDebugBreakApi,
            UseWin64Interrupt,
            UseWin32Interrupt
        };
/*
    Windows 64 bit has a 32 bit subsystem (WOW64) which makes it possible to run a
    32 bit application inside a 64 bit environment.
    When GDB is used DebugBreakProcess must be called from the same system (32/64 bit) running
    the inferior.
    Therefore we need helper executables (win(32/64)interrupt.exe) on Windows 64 bit calling
    DebugBreakProcess from the correct system.

    DebugBreak matrix for windows

    Api = UseDebugBreakApi
    Win64 = UseWin64Interrupt
    Win32 = UseWin32Interrupt
    N/A = This configuration is not possible

          | Windows 32bit   | Windows 64bit
          | QtCreator 32bit | QtCreator 32bit                   | QtCreator 64bit
          | Inferior 32bit  | Inferior 32bit  | Inferior 64bit  | Inferior 32bit  | Inferior 64bit |
----------|-----------------|-----------------|-----------------|-----------------|----------------|
GDB 32bit | Api             | Api             | NA              | Win32           | NA             |
    64bit | NA              | Api             | Win64           | Win32           | Api            |
----------|-----------------|-----------------|-----------------|-----------------|----------------|

*/

        DebugBreakApi breakApi = UseDebugBreakApi;
#ifdef Q_OS_WIN64
        if (isWow64Process(inferior))
            breakApi = UseWin32Interrupt;
#else
        if (isWow64Process(GetCurrentProcess()) && !isWow64Process(inferior))
            breakApi = UseWin64Interrupt;
#endif
        if (breakApi == UseDebugBreakApi) {
            ok = DebugBreakProcess(inferior);
            if (!ok)
                *errorMessage = "DebugBreakProcess failed: " + Utils::winErrorMessage(GetLastError());
        } else {
            const QString executable = breakApi == UseWin32Interrupt
                    ? QCoreApplication::applicationDirPath() + "/win32interrupt.exe"
                    : QCoreApplication::applicationDirPath() + "/win64interrupt.exe";
            if (!QFileInfo::exists(executable)) {
                *errorMessage = QString::fromLatin1(
                                    "%1 does not exist. Your %2 installation seems to be corrupt.")
                                    .arg(
                                        QDir::toNativeSeparators(executable),
                                        QGuiApplication::applicationDisplayName());
                break;
            }
            switch (QProcess::execute(executable, QStringList(QString::number(pID)))) {
            case -2:
                *errorMessage = QString::fromLatin1("Cannot start %1. Check src\\tools\\win64interrupt\\win64interrupt.c for more information.").
                                arg(QDir::toNativeSeparators(executable));
                break;
            case 0:
                ok = true;
                break;
            default:
                *errorMessage = QDir::toNativeSeparators(executable)
                                + " could not break the process.";
                break;
            }
            break;
        }
    } while (false);
    if (inferior != NULL)
        CloseHandle(inferior);
    if (!ok)
        *errorMessage = msgCannotInterrupt(pID, *errorMessage);
    return ok;
}

#else // Q_OS_WIN

#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

bool Debugger::Internal::interruptProcess(qint64 pID, QString *errorMessage)
{
    if (pID <= 0) {
        *errorMessage = msgCannotInterrupt(pID, QString::fromLatin1("Invalid process id."));
        return false;
    }
    if (kill(pID, SIGINT)) {
        *errorMessage = msgCannotInterrupt(pID, QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    return true;
}

#endif // !Q_OS_WIN
