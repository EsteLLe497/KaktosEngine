#pragma once

#include <string>
#include <unordered_map>
#include <windows.h>

class GdiFontCache final
{
public:
    ~GdiFontCache();
    HFONT Get(int height, int weight, const wchar_t* face);
    void Clear();

private:
    std::unordered_map<std::wstring, HFONT> fonts_;
};
