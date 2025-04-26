#pragma once
#include <string>
#include <functional>
#include <format>

#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace SafeDllMainUtil
{
    static long DumpMemory(EXCEPTION_POINTERS* Exception)
    {
        char Name[MAX_PATH] { 0 };
        auto NameEnd = Name + GetModuleFileNameA(GetModuleHandleA(nullptr), Name, MAX_PATH);

        SYSTEMTIME Time;
        GetSystemTime(&Time);
        wsprintfA(NameEnd - strlen(".exe"), "_%4d%02d%02d_%02d%02d%02d.dmp", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);

        HANDLE File = CreateFileA(Name, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (File == INVALID_HANDLE_VALUE)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo { 0 };
        ExceptionInfo.ThreadId = GetCurrentThreadId();
        ExceptionInfo.ExceptionPointers = Exception;
        ExceptionInfo.ClientPointers = false;

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            File,
            MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithFullMemory),
            Exception ? &ExceptionInfo : NULL,
            NULL,
            NULL);

        if (File)
        {
            CloseHandle(File);
            File = 0;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    LONG CrashHandler(EXCEPTION_POINTERS* ExceptionInfo)
    {
        if (!ExceptionInfo || !ExceptionInfo->ExceptionRecord || !ExceptionInfo->ContextRecord)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        auto SymInit = SymInitialize(GetCurrentProcess(), NULL, TRUE);

        std::vector<std::wstring> CallStack = {
            std::format(L"Exception Code\n0x{:X}", ExceptionInfo->ExceptionRecord->ExceptionCode),
            std::format(L"Exception Address\n{:P}", ExceptionInfo->ExceptionRecord->ExceptionAddress)
        };

        std::vector<TASKDIALOG_BUTTON> Buttons;

        CONTEXT* Context = ExceptionInfo->ContextRecord;
        STACKFRAME64 StackFrame = {};

        StackFrame.AddrPC.Offset = Context->Rip;
        StackFrame.AddrPC.Mode = AddrModeFlat;
        StackFrame.AddrFrame.Offset = Context->Rbp;
        StackFrame.AddrFrame.Mode = AddrModeFlat;
        StackFrame.AddrStack.Offset = Context->Rsp;
        StackFrame.AddrStack.Mode = AddrModeFlat;
        DWORD MachineType = IMAGE_FILE_MACHINE_AMD64;

        int FrameNum = 0;

        while (StackWalk64(MachineType,
            GetCurrentProcess(),
            GetCurrentThread(),
            &StackFrame,
            Context,
            NULL,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            NULL))
        {
            if (StackFrame.AddrPC.Offset == 0)
                break;

            auto Symbol = SYMBOL_INFO { 0 };
            Symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
            Symbol.MaxNameLen = MAX_SYM_NAME;

            DWORD64 Displacement = 0;
            std::string FuncName = "<Unknown Function>";
            if (SymFromAddr(GetCurrentProcess(), StackFrame.AddrPC.Offset, &Displacement, &Symbol))
            {
                FuncName = Symbol.Name;
            }

            IMAGEHLP_LINE64 LineInfo = {};
            LineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            DWORD LineDisplacement = 0;
            std::string FileInfo = "<Unknown SourceFile>";
            if (SymGetLineFromAddr64(GetCurrentProcess(), StackFrame.AddrPC.Offset, &LineDisplacement, &LineInfo))
            {
                FileInfo = std::format("{}:{}", LineInfo.FileName, LineInfo.LineNumber);
            }

            auto FuncNameW = std::wstring(FuncName.begin(), FuncName.end());
            auto FileInfoW = std::wstring(FileInfo.begin(), FileInfo.end());

            auto ButtonText = std::format(L"{} [{}]\n0x{:X}", FuncNameW, FileInfoW, StackFrame.AddrPC.Offset);

            CallStack.push_back(ButtonText);

            if (FrameNum > 100)
                break;
        }

        TASKDIALOGCONFIG Config = { sizeof(Config) };

        for (auto i = 0; i < CallStack.size(); i++)
        {
            Buttons.push_back({ IDYES + 2 + i, CallStack[i].c_str() });
        }

        Buttons.push_back({ IDYES, L"Dump Memory" });
        Buttons.push_back({ IDNO, L"Cancel" });

        Config.pButtons = Buttons.data();
        Config.cButtons = (UINT)Buttons.size();

        Config.hwndParent = NULL;
        Config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_USE_COMMAND_LINKS;
        Config.pszWindowTitle = L"Application Error";
        Config.pszMainIcon = TD_ERROR_ICON;
        Config.pszMainInstruction = L"An unhandled exception has occurred.";

    ShowDialog:
        int Button = 0;
        TaskDialogIndirect(&Config, &Button, NULL, NULL);

        switch (Button)
        {
        case IDYES:
        {
            DumpMemory(ExceptionInfo);
            break;
        }
        case IDNO:
        {
            break;
        }

        default:
        {
            auto Index = Button - IDYES - 2;

            if (Index >= 0 && Index < CallStack.size())
            {
                auto Address = CallStack[Index].substr(CallStack[Index].find(L"0x"));
                if (OpenClipboard(NULL))
                {
                    EmptyClipboard();
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, Address.size() * sizeof(wchar_t) + 1);
                    memcpy(GlobalLock(hGlob), Address.c_str(), Address.size() * sizeof(wchar_t) + 1);
                    GlobalUnlock(hGlob);
                    SetClipboardData(CF_UNICODETEXT, hGlob);
                    CloseClipboard();
                }
            }

            goto ShowDialog;
            break;
        }
        }

        if (SymInit)
        {
            SymCleanup(GetCurrentProcess());
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }

}

void SafeDllMain();

void SafeMainStub()
{
    __try
    {
        SafeDllMain();
    }
    __except (SafeDllMainUtil::CrashHandler(GetExceptionInformation()))
    {
    }
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)SafeMainStub, nullptr, 0, nullptr);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
