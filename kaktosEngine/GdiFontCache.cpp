#include "GdiFontCache.h"

GdiFontCache::~GdiFontCache()
{
    Clear();
}

HFONT GdiFontCache::Get(int height, int weight, const wchar_t* face)
{
    const std::wstring key = std::to_wstring(height) + L"|" + std::to_wstring(weight) + L"|" + (face ? face : L"");
    const auto found = fonts_.find(key);
    if (found != fonts_.end())
    {
        return found->second;
    }
    HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    fonts_.emplace(key, font);
    return font;
}

void GdiFontCache::Clear()
{
    for (const auto& item : fonts_)
    {
        DeleteObject(item.second);
    }
    fonts_.clear();
}
