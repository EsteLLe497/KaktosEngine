#pragma once

#include <windows.h>

class BackBuffer final
{
public:
    BackBuffer() = default;
    ~BackBuffer();

    BackBuffer(const BackBuffer&) = delete;
    BackBuffer& operator=(const BackBuffer&) = delete;

    HDC Begin(HDC target, int width, int height);
    void Present(HDC target, const RECT& paintRect) const;
    void Reset();

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
