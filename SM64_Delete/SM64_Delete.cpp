#include <windows.h>
#include <string>
#include <sstream>

// Funktion f�r att radera ett registernamn med r�tt root-handle
bool DeleteRegistryValue(HKEY hRoot, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey;
    // �ppna nyckeln med r�tt access
    LONG res = RegOpenKeyExW(hRoot, subKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey);
    if (res != ERROR_SUCCESS) return false;

    // F�rs�k ta bort v�rdet
    res = RegDeleteValueW(hKey, valueName);
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

// Konvertera string till HKEY
HKEY StringToHKEY(const std::wstring& str) {
    if (str == L"HKLM" || str == L"HKEY_LOCAL_MACHINE") return HKEY_LOCAL_MACHINE;
    if (str == L"HKCU" || str == L"HKEY_CURRENT_USER") return HKEY_CURRENT_USER;
    return NULL;
}

// Huvudprogram
int wmain(int argc, wchar_t* argv[])
{
    // G�r processen DPI-aware s� att MessageBox och annat ser bra ut p� HiDPI/4K
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Kontrollera att vi f�tt alla tre argument: root, subKey, valueName
    if (argc < 4) {
        MessageBoxW(NULL,
            L"This helper should not be run manually.\nPlease run StartupMonitor64.exe instead.",
            L"SM64_Delete", MB_OK | MB_ICONWARNING);
        return 1;
    }

    // L�s argumenten
    HKEY hRoot = StringToHKEY(argv[1]);
    const wchar_t* subKey = argv[2];
    const wchar_t* valueName = argv[3];

    // Kontrollera giltig root key
    if (!hRoot) {
        MessageBoxW(NULL,
            L"Invalid root key specified.",
            L"SM64_Delete", MB_OK | MB_ICONERROR);
        return 1;
    }

    // F�rs�k radera posten
    if (DeleteRegistryValue(hRoot, subKey, valueName)) {
        std::wstring msg = L"Successfully deleted \"" + std::wstring(valueName) + L"\" from:\n" + subKey;
        MessageBoxW(NULL, msg.c_str(), L"SM64_Delete", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    else {
        std::wstringstream ss;
        ss << L"Failed to delete \"" << valueName << L"\" from:\n" << subKey
            << L"\n\nReason: Administrative privileges were denied or the value does not exist.";
        MessageBoxW(NULL, ss.str().c_str(), L"SM64_Delete", MB_OK | MB_ICONERROR);
        return 1;
    }
}
