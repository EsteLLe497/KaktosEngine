#include "Persistence.h"

#include <objbase.h>
#include <windows.h>

namespace Persistence
{
    bool WriteUtf8Atomic(const std::wstring& path, const std::string& utf8)
    {
        const std::wstring temporaryPath = path + L".tmp";
        const std::wstring backupPath = path + L".bak";
        HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        BOOL ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
        if (ok && !utf8.empty())
        {
            ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) && written == static_cast<DWORD>(utf8.size());
        }
        if (ok)
        {
            ok = FlushFileBuffers(file);
        }
        CloseHandle(file);

        if (ok)
        {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ok = ReplaceFileW(path.c_str(), temporaryPath.c_str(), backupPath.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
            }
            else
            {
                ok = MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
            }
        }
        if (!ok)
        {
            DeleteFileW(temporaryPath.c_str());
        }
        return ok == TRUE;
    }

    std::wstring CreateId()
    {
        GUID guid = {};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return std::to_wstring(GetTickCount64());
        }
        wchar_t text[40] = {};
        StringFromGUID2(guid, text, static_cast<int>(_countof(text)));
        std::wstring result = text;
        if (result.size() >= 2 && result.front() == L'{' && result.back() == L'}')
        {
            result = result.substr(1, result.size() - 2);
        }
        return result;
    }

    std::wstring HashText(const std::wstring& text)
    {
        unsigned long long hash = 1469598103934665603ull;
        for (wchar_t ch : text)
        {
            hash ^= static_cast<unsigned long long>(ch);
            hash *= 1099511628211ull;
        }
        wchar_t buffer[17] = {};
        swprintf_s(buffer, L"%016llx", hash);
        return buffer;
    }
}
