#pragma once

#include <string>

namespace Persistence
{
    bool WriteUtf8Atomic(const std::wstring& path, const std::string& utf8);
    std::wstring CreateId();
    std::wstring HashText(const std::wstring& text);
}
