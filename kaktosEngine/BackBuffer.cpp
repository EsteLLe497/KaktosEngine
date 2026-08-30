#include "BackBuffer.h"

BackBuffer::~BackBuffer()
{
    Reset();
}

HDC BackBuffer::Begin(HDC target, int width, int height)
{
    if (!target || width <= 0 || height <= 0)
    {
        return nullptr;
    }
    if (dc_ && bitmap_ && width_ == width && height_ == height)
    {
        return dc_;
    }

    Reset();
    dc_ = CreateCompatibleDC(target);
    bitmap_ = CreateCompatibleBitmap(target, width, height);
    if (!dc_ || !bitmap_)
    {
        Reset();
        return nullptr;
    }
    previousBitmap_ = SelectObject(dc_, bitmap_);
    width_ = width;
    height_ = height;
    return dc_;
}

void BackBuffer::Present(HDC target, const RECT& paintRect) const
{
    if (!target || !dc_)
    {
        return;
    }
    const int width = paintRect.right - paintRect.left;
    const int height = paintRect.bottom - paintRect.top;
    if (width > 0 && height > 0)
    {
        BitBlt(target, paintRect.left, paintRect.top, width, height, dc_, paintRect.left, paintRect.top, SRCCOPY);
    }
}

void BackBuffer::Reset()
{
    if (dc_ && previousBitmap_)
    {
        SelectObject(dc_, previousBitmap_);
    }
    if (bitmap_)
    {
        DeleteObject(bitmap_);
    }
    if (dc_)
    {
        DeleteDC(dc_);
    }
    dc_ = nullptr;
    bitmap_ = nullptr;
    previousBitmap_ = nullptr;
    width_ = 0;
    height_ = 0;
}
