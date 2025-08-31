#include <windows.h>
#include <map>
#include <string>
#include <sstream>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <vector>
#include <bcrypt.h>

// Struktur för snapshot
using RegistryMap = std::map<std::wstring, std::wstring>;

//-----------------------
// FUNKTION: Läs register
//-----------------------
RegistryMap ReadRegistry(HKEY hRoot, const wchar_t* subKey, REGSAM flag) {

    RegistryMap result;
    HKEY hKey;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ | flag, &hKey) != ERROR_SUCCESS)
        return result;

    DWORD index = 0;
    wchar_t valueName[256];
    BYTE valueData[1024];
    DWORD nameSize, dataSize, type;

    while (true) {
        nameSize = 256;
        dataSize = 1024;
        LONG ret = RegEnumValueW(hKey, index, valueName, &nameSize, NULL, &type, valueData, &dataSize);
        if (ret == ERROR_NO_MORE_ITEMS)
            break;

        if (ret == ERROR_SUCCESS) {
            if (type == REG_SZ) {
                result[valueName] = (wchar_t*)valueData;
            }
            else if (type == REG_EXPAND_SZ) {
                // Expandera miljövariabler till full sökväg
                wchar_t expanded[1024];
                DWORD len = ExpandEnvironmentStringsW((wchar_t*)valueData, expanded, 1024);
                if (len > 0 && len < 1024) {
                    result[valueName] = expanded;
                }
                else {
                    // Om expansion misslyckas, spara originalsträngen
                    result[valueName] = (wchar_t*)valueData;
                }
            }
        }
        index++;
    }

    RegCloseKey(hKey);
    return result;
}

//------------------------------------------------------
// Startar delete-programmet med admin via ShellExecute:
//------------------------------------------------------
bool LaunchDeleteProcessAndCheck(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName) {

    static HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Global\\SM64_Delete_Mutex");
    WaitForSingleObject(hMutex, INFINITE);

    // Bestäm root-hive som sträng
    std::wstring rootStr = (hRoot == HKEY_LOCAL_MACHINE) ? L"HKLM" : L"HKCU";

    // Skicka med root, subKey och valueName korrekt med citattecken
    std::wstring args = rootStr + L" \"" + subKey + L"\" \"" + valueName + L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"SM64_Delete.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        // Misslyckades direkt
        MessageBoxW(NULL, (L"Failed to start delete process for: " + valueName).c_str(),
            L"StartupMonitor64 Warning", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
        ReleaseMutex(hMutex);
        return false;
    }

    // Vänta på att delete-processen avslutas och stäng handle
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }

    // Kolla om posten fortfarande finns i registret
    RegistryMap snapshotCheck = ReadRegistry(hRoot, subKey.c_str(), (hRoot == HKEY_LOCAL_MACHINE) ? KEY_WOW64_64KEY : KEY_WOW64_64KEY);
    bool stillExists = (snapshotCheck.find(valueName) != snapshotCheck.end());

    if (stillExists) {
        std::wstringstream ss;
        ss << L"Failed to delete autostart entry for:\n\n[" << valueName
            << L"]\n\nRegistry Key: " << subKey
            << L"\n\nReason: Administrative privileges were denied (UAC prompt rejected).";
        MessageBoxW(NULL, ss.str().c_str(),
            L"StartupMonitor64 Warning", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
    }

    ReleaseMutex(hMutex);
    return !stillExists; // true = raderad
}


//--------------------------
// BEVAKA EN REGISTERNYCKEL:
//--------------------------
void WatchKey(HKEY hRoot, const wchar_t* subKey, REGSAM flag) {
    auto snapshot = ReadRegistry(hRoot, subKey, flag);

    HKEY hKey;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_NOTIFY | KEY_READ | flag, &hKey) != ERROR_SUCCESS)
        return;

    //NY WATCHKEY LOOP:
    while (true) {
        // Vänta på ändring
        RegNotifyChangeKeyValue(hKey, FALSE,
            REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
            NULL, FALSE);

        auto current = ReadRegistry(hRoot, subKey, flag);

        //--------------------------
        // --- NY ELLER ÄNDRAD POST:
        //--------------------------
        for (auto& [name, data] : current) {
            auto it = snapshot.find(name);

            if (it == snapshot.end()) {
                // NY POST HITTAD:
                std::wstringstream ss;
                ss << L"The program\n[" << name << L"]\n\nhas registered the executable\n\n"
                    << data << L"\n\nto run at system startup.\n\nDo you wish to allow this change?\n\nRegistry Key: " << subKey;

                int choice = MessageBoxW(NULL, ss.str().c_str(), L"StartupMonitor64 Warning",
                    MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL);
                
                if (choice == IDNO) {
                    if (!LaunchDeleteProcessAndCheck(hRoot, subKey, name)) {
                        // Misslyckades, snapshot uppdateras inte
                    }
                    else {
                        snapshot.erase(name); // endast ta bort snapshot om raderingen lyckades
                    }
                }
                else {
                    snapshot[name] = data; // uppdatera snapshot för tillåtna poster
                }


            }
            else if (it->second != data) {
                // POST ÄNDRAD:
                std::wstringstream ss;
                ss << L"The program\n[" << name << L"]\n\nhas changed its autostart entry:\n\n"
                    << L"Old Value: " << it->second << L"\n"
                    << L"New Value: " << data << L"\n\nRegistry Key: " << subKey;

                MessageBoxW(NULL, ss.str().c_str(),
                    L"StartupMonitor64 Info", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);

                snapshot[name] = data; // uppdatera snapshot
            }
        }


        //------------------------------------------
        // --- POST BORTTAGEN AV ANNAT PROGRAM: ---
        //------------------------------------------
        for (auto it = snapshot.begin(); it != snapshot.end(); ) {
            if (current.find(it->first) == current.end()) {
                
                //Meddelande om en post raderas i Registret, om man vill ha det
                //MessageBoxW(NULL, L"Post borttagen, Snapshot uppdaterad", L"StartupMonitor Info", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);

                it = snapshot.erase(it); // ta bort ur snapshot
            }
            else {
                ++it;
            }
        }
    }

    RegCloseKey(hKey);
}

//--------------------------------
// BEVAKA EN STARTUP-MAPP (folder)
//--------------------------------
void WatchFolder(const std::wstring& folderPath) {

    HANDLE hDir = CreateFileW(
        folderPath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (hDir == INVALID_HANDLE_VALUE)
        return;

    BYTE buffer[4096];
    DWORD bytesReturned;

    while (true) {
        if (ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned,
            NULL,
            NULL)) {

            BYTE* base = buffer;
            while (true) {
                FILE_NOTIFY_INFORMATION* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(base);
                std::wstring fileName(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

                // Reagera på NY fil:
                if (fni->Action == FILE_ACTION_ADDED) {
                    // Ny fil -> Ja/Nej
                    std::wstringstream ss;
                    ss << L"A program has added a file in the startup folder:\n\n"
                        << folderPath << L"\\" << fileName
                        << L"\n\nDo you wish to allow this change?";

                    int choice = MessageBoxW(NULL, ss.str().c_str(),
                        L"StartupMonitor64 Warning",
                        MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL);

                    if (choice == IDNO) {
                        std::wstring fullPath = folderPath + L"\\" + fileName;
                        DeleteFileW(fullPath.c_str());
                    }
                }
                else if (fni->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                    // Reagera på Namnändring -> endast info
                    std::wstringstream ss;
                    ss << L"A file in the startup folder has been renamed:\n\n"
                        << folderPath << L"\\" << fileName;

                    MessageBoxW(NULL, ss.str().c_str(),
                        L"StartupMonitor64 Info",
                        MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
                }


                if (fni->NextEntryOffset == 0) break;
                base += fni->NextEntryOffset;
            }
        }
    }

    CloseHandle(hDir);
}



//-----------------
//PROG STARTAR HÄR:
//-----------------
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {

    // Gör processen DPI-aware så att MessageBox och annat ritas skarpt på 4K/HiDPI
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ----------- Registry-trådar ----------- (SKA VARA MED DUBBLA \\ I C++):
    // 64-bit
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_64KEY);
        return 0;
    }, NULL, 0, NULL);

    // 64-bit
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", KEY_WOW64_64KEY);
        return 0;
    }, NULL, 0, NULL);

    // 32-bit
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_32KEY);
        return 0;
    }, NULL, 0, NULL);

    // 32-bit
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce", KEY_WOW64_32KEY);
        return 0;
    }, NULL, 0, NULL);

    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_64KEY);
        return 0;
    }, NULL, 0, NULL);

    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        WatchKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", KEY_WOW64_64KEY);
        return 0;
    }, NULL, 0, NULL);

    // ----------- Startup-folder-trådar -----------

    // All Users Startup Folder (C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup)
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        PWSTR path = NULL;
        if (SHGetKnownFolderPath(FOLDERID_CommonStartup, 0, NULL, &path) == S_OK) {
            WatchFolder(path);
            CoTaskMemFree(path);
        }
        return 0;
    }, NULL, 0, NULL);

    // Current User Startup Folder (C:\Users\<User>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup)
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)[](LPVOID)->DWORD {
        PWSTR path = NULL;
        if (SHGetKnownFolderPath(FOLDERID_Startup, 0, NULL, &path) == S_OK) {
            WatchFolder(path);
            CoTaskMemFree(path);
        }
        return 0;
    }, NULL, 0, NULL);

    // Enkel meddelandeloop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
