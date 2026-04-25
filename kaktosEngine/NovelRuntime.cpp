#include "framework.h"
#include "NovelRuntime.h"
#include "resource.h"

#include <commdlg.h>
#include <gdiplusheaders.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <functional>
#include <iterator>
#include <sstream>
#include <unordered_set>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef L
#undef C
#undef R

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")

namespace
{
    void DebugTrace(const std::wstring& message)
    {
        OutputDebugStringW((message + L"\n").c_str());
    }

    std::wstring NormalizeFullPath(const std::wstring& path)
    {
        if (path.empty())
        {
            return path;
        }

        WCHAR buffer[MAX_PATH * 4] = {};
        const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(buffer)), buffer, nullptr);
        if (length == 0 || length >= _countof(buffer))
        {
            return path;
        }
        return std::wstring(buffer, length);
    }

    bool StartsWithText(const std::wstring& value, const std::wstring& prefix)
    {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    std::wstring NextIfOperator(const std::wstring& current)
    {
        static const wchar_t* kOperators[] = { L"eq", L"ne", L"gt", L"ge", L"lt", L"le" };
        for (size_t i = 0; i < _countof(kOperators); ++i)
        {
            if (current == kOperators[i])
            {
                return kOperators[(i + 1) % _countof(kOperators)];
            }
        }
        return L"eq";
    }

    std::wstring GetIfOperatorLabel(const std::wstring& op)
    {
        if (op == L"eq") return L"\u7b49\u3057\u3044";
        if (op == L"ne") return L"\u7b49\u3057\u304f\u306a\u3044";
        if (op == L"gt") return L"\u3088\u308a\u5927\u304d\u3044";
        if (op == L"ge") return L"\u4ee5\u4e0a";
        if (op == L"lt") return L"\u3088\u308a\u5c0f\u3055\u3044";
        if (op == L"le") return L"\u4ee5\u4e0b";
        return op;
    }

    std::wstring GetChoiceParamKey(const wchar_t* prefix, size_t index)
    {
        return std::wstring(prefix) + std::to_wstring(index);
    }

    std::wstring GetNextVariableName(const std::vector<VariableDefinition>& definitions, const std::wstring& current)
    {
        if (definitions.empty())
        {
            return current;
        }
        for (size_t i = 0; i < definitions.size(); ++i)
        {
            if (definitions[i].name == current)
            {
                return definitions[(i + 1) % definitions.size()].name;
            }
        }
        return definitions.front().name;
    }

    VariableType InferVariableType(const std::wstring& value)
    {
        std::wstring normalized = value;
        if (!normalized.empty())
        {
            CharLowerBuffW(&normalized[0], static_cast<DWORD>(normalized.size()));
        }
        if (normalized == L"true" || normalized == L"false")
        {
            return VariableType::Bool;
        }
        if (!value.empty())
        {
            bool numeric = true;
            for (size_t i = (value[0] == L'-' || value[0] == L'+') ? 1 : 0; i < value.size(); ++i)
            {
                if (!iswdigit(value[i]))
                {
                    numeric = false;
                    break;
                }
            }
            if (numeric)
            {
                return VariableType::Integer;
            }
        }
        return VariableType::String;
    }

    bool ParseBoolValue(const std::wstring& value, bool defaultValue = true)
    {
        if (value.empty())
        {
            return defaultValue;
        }

        std::wstring normalized = value;
        CharLowerBuffW(&normalized[0], static_cast<DWORD>(normalized.size()));
        return normalized == L"true" || normalized == L"1" || normalized == L"yes" || normalized == L"on";
    }

    bool FileExistsForScenario(const std::wstring& baseDir, const std::wstring& path)
    {
        if (path.empty())
        {
            return true;
        }

        DWORD attributes = GetFileAttributesW(CombinePath(baseDir, path).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            attributes = GetFileAttributesW(path.c_str());
        }
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    int ParseIntValue(const std::wstring& value, int defaultValue)
    {
        if (value.empty())
        {
            return defaultValue;
        }

        return _wtoi(value.c_str());
    }

    BYTE ClampByteValue(int value)
    {
        return static_cast<BYTE>((std::max)(0, (std::min)(255, value)));
    }

    void DrawVerticalScrollbar(HDC hdc, const RECT& contentRect, int scrollOffset, int scrollMax, COLORREF trackColor, COLORREF thumbColor)
    {
        if (scrollMax <= 0)
        {
            return;
        }

        const int trackWidth = 10;
        RECT trackRect = { contentRect.right - 12, contentRect.top, contentRect.right - 12 + trackWidth, contentRect.bottom };
        if (trackRect.bottom - trackRect.top <= 18)
        {
            return;
        }

        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        FillRect(hdc, &trackRect, trackBrush);
        DeleteObject(trackBrush);
        FrameRect(hdc, &trackRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

        const int visibleHeight = trackRect.bottom - trackRect.top;
        const int thumbHeight = (std::max)(28, (visibleHeight * visibleHeight) / (visibleHeight + scrollMax));
        const int movable = (std::max)(1, visibleHeight - thumbHeight);
        const int thumbTop = trackRect.top + (scrollOffset * movable) / (std::max)(1, scrollMax);
        RECT thumbRect = { trackRect.left + 1, thumbTop, trackRect.right - 1, thumbTop + thumbHeight };
        if (thumbRect.bottom > trackRect.bottom - 1)
        {
            const int overflow = thumbRect.bottom - (trackRect.bottom - 1);
            thumbRect.top -= overflow;
            thumbRect.bottom -= overflow;
        }

        HBRUSH thumbBrush = CreateSolidBrush(thumbColor);
        FillRect(hdc, &thumbRect, thumbBrush);
        DeleteObject(thumbBrush);
        FrameRect(hdc, &thumbRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }

    void DrawAlphaOverlay(HDC hdc, const RECT& targetRect, COLORREF color, int alpha)
    {
        if (alpha <= 0 || targetRect.right <= targetRect.left || targetRect.bottom <= targetRect.top)
        {
            return;
        }

        HDC overlayDc = CreateCompatibleDC(hdc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(hdc, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top);
        HGDIOBJ oldBitmap = SelectObject(overlayDc, overlayBitmap);
        RECT overlayRect = { 0, 0, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top };
        HBRUSH overlayBrush = CreateSolidBrush(color);
        FillRect(overlayDc, &overlayRect, overlayBrush);
        DeleteObject(overlayBrush);
        BLENDFUNCTION overlayBlend = { AC_SRC_OVER, 0, static_cast<BYTE>((std::max)(0, (std::min)(255, alpha))), 0 };
        AlphaBlend(hdc, targetRect.left, targetRect.top, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top, overlayDc, 0, 0, targetRect.right - targetRect.left, targetRect.bottom - targetRect.top, overlayBlend);
        SelectObject(overlayDc, oldBitmap);
        DeleteObject(overlayBitmap);
        DeleteDC(overlayDc);
    }

    std::wstring GetLegacyAudioAlias(AudioChannel channel)
    {
        switch (channel)
        {
        case AudioChannel::Bgm: return L"kaktos_bgm";
        case AudioChannel::Se: return L"kaktos_se";
        case AudioChannel::Voice: return L"kaktos_voice";
        default: return L"kaktos_asset_preview";
        }
    }

    bool TryOpenAudioAlias(const std::wstring& fullPath, const std::wstring& alias)
    {
        const std::wstring closeCommand = L"close " + alias;
        mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);

        std::wstring lowerPath = fullPath;
        if (!lowerPath.empty())
        {
            CharLowerBuffW(&lowerPath[0], static_cast<DWORD>(lowerPath.size()));
        }

        std::vector<std::wstring> openCommands;
        if (lowerPath.size() >= 4 && lowerPath.substr(lowerPath.size() - 4) == L".wav")
        {
            openCommands.push_back(L"open \"" + fullPath + L"\" type waveaudio alias " + alias);
        }
        else
        {
            openCommands.push_back(L"open \"" + fullPath + L"\" type mpegvideo alias " + alias);
        }
        openCommands.push_back(L"open \"" + fullPath + L"\" alias " + alias);

        for (const std::wstring& openCommand : openCommands)
        {
            if (mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void ApplyAudioAliasVolume(const std::wstring& alias, int volumePercent)
    {
        volumePercent = (std::max)(0, (std::min)(100, volumePercent));
        const std::wstring setVolumeCommand = L"setaudio " + alias + L" volume to " + std::to_wstring(volumePercent * 10);
        mciSendStringW(setVolumeCommand.c_str(), nullptr, 0, nullptr);
    }

    bool EnsureDirectoryExists(const std::wstring& path)
    {
        if (path.empty())
        {
            return false;
        }
        const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
        return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
    }

    bool CopyDirectoryTree(const std::wstring& sourceDir, const std::wstring& targetDir)
    {
        if (!EnsureDirectoryExists(targetDir))
        {
            return false;
        }

        WIN32_FIND_DATAW data = {};
        HANDLE findHandle = FindFirstFileW((sourceDir + L"\\*").c_str(), &data);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            return true;
        }

        bool ok = true;
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }

            const std::wstring sourcePath = sourceDir + L"\\" + name;
            const std::wstring targetPath = targetDir + L"\\" + name;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (!CopyDirectoryTree(sourcePath, targetPath))
                {
                    ok = false;
                    break;
                }
            }
            else if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), TRUE))
            {
                ok = false;
                break;
            }
        } while (FindNextFileW(findHandle, &data));

        FindClose(findHandle);
        return ok;
    }
}

bool NovelRuntime::InitializeAudioEngine()
{
    if (xaudio2_ && masteringVoice_)
    {
        return true;
    }

    if (!comInitialized_)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(comResult))
        {
            comInitialized_ = true;
        }
        else if (comResult != RPC_E_CHANGED_MODE)
        {
            statusText_ = L"COM 初期化に失敗しました";
            return false;
        }
    }

    if (!mediaFoundationInitialized_)
    {
        const HRESULT mfResult = MFStartup(MF_VERSION);
        if (FAILED(mfResult))
        {
            statusText_ = L"Media Foundation の初期化に失敗しました";
            return false;
        }
        mediaFoundationInitialized_ = true;
    }

    HRESULT hr = XAudio2Create(xaudio2_.ReleaseAndGetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !xaudio2_)
    {
        statusText_ = L"XAudio2 の初期化に失敗しました";
        return false;
    }

    hr = xaudio2_->CreateMasteringVoice(&masteringVoice_);
    if (FAILED(hr) || !masteringVoice_)
    {
        xaudio2_.Reset();
        statusText_ = L"マスターボイスの作成に失敗しました";
        return false;
    }

    return true;
}

void NovelRuntime::ShutdownAudioEngine()
{
    StopAudioChannel(AudioChannel::Preview);
    StopAudioChannel(AudioChannel::Voice);
    StopAudioChannel(AudioChannel::Se);
    StopAudioChannel(AudioChannel::Bgm);

    if (masteringVoice_)
    {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }
    xaudio2_.Reset();

    if (mediaFoundationInitialized_)
    {
        MFShutdown();
        mediaFoundationInitialized_ = false;
    }
    if (comInitialized_)
    {
        CoUninitialize();
        comInitialized_ = false;
    }
}

AudioPlaybackState& NovelRuntime::GetAudioPlaybackState(AudioChannel channel)
{
    switch (channel)
    {
    case AudioChannel::Bgm: return bgmPlayback_;
    case AudioChannel::Se: return sePlayback_;
    case AudioChannel::Voice: return voicePlayback_;
    default: return previewPlayback_;
    }
}

const AudioPlaybackState& NovelRuntime::GetAudioPlaybackState(AudioChannel channel) const
{
    switch (channel)
    {
    case AudioChannel::Bgm: return bgmPlayback_;
    case AudioChannel::Se: return sePlayback_;
    case AudioChannel::Voice: return voicePlayback_;
    default: return previewPlayback_;
    }
}

void NovelRuntime::StopAudioChannel(AudioChannel channel)
{
    AudioPlaybackState& state = GetAudioPlaybackState(channel);
    if (state.voice)
    {
        state.voice->Stop(0);
        state.voice->FlushSourceBuffers();
        state.voice->DestroyVoice();
        state.voice = nullptr;
    }
    if (state.usesLegacyMci && !state.legacyAlias.empty())
    {
        const std::wstring closeCommand = L"close " + state.legacyAlias;
        mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
    }
    state.audioBytes.clear();
    state.formatBytes.clear();
    state.looping = false;
    state.usesLegacyMci = false;
    state.legacyAlias.clear();
    state.sourcePath.clear();
}

void NovelRuntime::SetAudioChannelVolume(AudioChannel channel, int volumePercent)
{
    volumePercent = (std::max)(0, (std::min)(100, volumePercent));
    AudioPlaybackState& state = GetAudioPlaybackState(channel);
    if (state.voice)
    {
        state.voice->SetVolume(static_cast<float>(volumePercent) / 100.0f);
    }
    if (state.usesLegacyMci && !state.legacyAlias.empty())
    {
        ApplyAudioAliasVolume(state.legacyAlias, volumePercent);
    }
}

bool NovelRuntime::DecodeAudioFile(const std::wstring& fullPath, std::vector<BYTE>& formatBytes, std::vector<BYTE>& audioBytes)
{
    formatBytes.clear();
    audioBytes.clear();
    bool decoded = false;

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(fullPath.c_str(), nullptr, reader.GetAddressOf());
    if (FAILED(hr) || !reader)
    {
        lastAudioDebugMessage_ = L"audio: MFCreateSourceReaderFromURL failed hr=" + std::to_wstring(static_cast<long long>(hr));
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
    }
    if (SUCCEEDED(hr) && reader)
    {
        Microsoft::WRL::ComPtr<IMFMediaType> outputType;
        hr = MFCreateMediaType(outputType.GetAddressOf());
        if (FAILED(hr) || !outputType)
        {
            lastAudioDebugMessage_ = L"audio: MFCreateMediaType failed hr=" + std::to_wstring(static_cast<long long>(hr));
            DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        }
        if (SUCCEEDED(hr) && outputType)
        {
            outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, outputType.Get());
            if (FAILED(hr))
            {
                lastAudioDebugMessage_ = L"audio: SetCurrentMediaType failed hr=" + std::to_wstring(static_cast<long long>(hr));
                DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
            }
            if (SUCCEEDED(hr))
            {
                Microsoft::WRL::ComPtr<IMFMediaType> nativeType;
                hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nativeType.GetAddressOf());
                if (FAILED(hr) || !nativeType)
                {
                    lastAudioDebugMessage_ = L"audio: GetCurrentMediaType failed hr=" + std::to_wstring(static_cast<long long>(hr));
                    DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
                }
                if (SUCCEEDED(hr) && nativeType)
                {
                    WAVEFORMATEX* waveFormat = nullptr;
                    UINT32 waveFormatSize = 0;
                    hr = MFCreateWaveFormatExFromMFMediaType(nativeType.Get(), &waveFormat, &waveFormatSize, MFWaveFormatExConvertFlag_Normal);
                    if (SUCCEEDED(hr) && waveFormat && waveFormatSize > 0)
                    {
                        formatBytes.assign(reinterpret_cast<const BYTE*>(waveFormat), reinterpret_cast<const BYTE*>(waveFormat) + waveFormatSize);
                        CoTaskMemFree(waveFormat);

                        while (true)
                        {
                            DWORD streamFlags = 0;
                            Microsoft::WRL::ComPtr<IMFSample> sample;
                            hr = reader->ReadSample(
                                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                0,
                                nullptr,
                                &streamFlags,
                                nullptr,
                                sample.GetAddressOf());
                            if (FAILED(hr))
                            {
                                formatBytes.clear();
                                audioBytes.clear();
                                lastAudioDebugMessage_ = L"audio: ReadSample failed hr=" + std::to_wstring(static_cast<long long>(hr));
                                DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
                                break;
                            }
                            if ((streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                            {
                                break;
                            }
                            if (!sample)
                            {
                                continue;
                            }

                            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                            hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
                            if (FAILED(hr) || !buffer)
                            {
                                formatBytes.clear();
                                audioBytes.clear();
                                lastAudioDebugMessage_ = L"audio: ConvertToContiguousBuffer failed hr=" + std::to_wstring(static_cast<long long>(hr));
                                DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
                                break;
                            }

                            BYTE* data = nullptr;
                            DWORD maxLength = 0;
                            DWORD currentLength = 0;
                            hr = buffer->Lock(&data, &maxLength, &currentLength);
                            if (FAILED(hr))
                            {
                                formatBytes.clear();
                                audioBytes.clear();
                                lastAudioDebugMessage_ = L"audio: IMFMediaBuffer::Lock failed hr=" + std::to_wstring(static_cast<long long>(hr));
                                DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
                                break;
                            }
                            audioBytes.insert(audioBytes.end(), data, data + currentLength);
                            buffer->Unlock();
                        }

                        decoded = !audioBytes.empty() && !formatBytes.empty();
                    }
                    else if (waveFormat)
                    {
                        CoTaskMemFree(waveFormat);
                    }
                    else
                    {
                        lastAudioDebugMessage_ = L"audio: MFCreateWaveFormatExFromMFMediaType failed hr=" + std::to_wstring(static_cast<long long>(hr));
                        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
                    }
                }
            }
        }
    }

    if (decoded)
    {
        lastAudioDebugMessage_ = L"audio: MediaFoundation decode OK";
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath + L" bytes=" + std::to_wstring(audioBytes.size()));
        return true;
    }

    formatBytes.clear();
    audioBytes.clear();

    HANDLE fileHandle = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        lastAudioDebugMessage_ = L"audio: CreateFileW failed code=" + std::to_wstring(static_cast<long long>(GetLastError()));
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart <= 0)
    {
        CloseHandle(fileHandle);
        lastAudioDebugMessage_ = L"audio: GetFileSizeEx failed code=" + std::to_wstring(static_cast<long long>(GetLastError()));
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }

    std::vector<BYTE> compressedBytes(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    if (!ReadFile(fileHandle, compressedBytes.data(), static_cast<DWORD>(compressedBytes.size()), &bytesRead, nullptr) || bytesRead != compressedBytes.size())
    {
        CloseHandle(fileHandle);
        lastAudioDebugMessage_ = L"audio: ReadFile failed code=" + std::to_wstring(static_cast<long long>(GetLastError()));
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }
    CloseHandle(fileHandle);

    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder decoder;
    const ma_result initResult = ma_decoder_init_memory(compressedBytes.data(), compressedBytes.size(), &config, &decoder);
    if (initResult != MA_SUCCESS)
    {
        lastAudioDebugMessage_ = L"audio: miniaudio decoder init failed code=" + std::to_wstring(static_cast<long long>(initResult));
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }

    const ma_uint32 channels = decoder.outputChannels;
    const ma_uint32 sampleRate = decoder.outputSampleRate;
    if (channels == 0 || sampleRate == 0)
    {
        ma_decoder_uninit(&decoder);
        lastAudioDebugMessage_ = L"audio: miniaudio invalid format";
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }

    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<ma_int16> chunk(static_cast<size_t>(kChunkFrames * channels));
    std::vector<ma_int16> pcmSamples;
    while (true)
    {
        ma_uint64 framesRead = 0;
        const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);
        if (readResult != MA_SUCCESS)
        {
            pcmSamples.clear();
            lastAudioDebugMessage_ = L"audio: miniaudio read failed code=" + std::to_wstring(static_cast<long long>(readResult));
            DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
            break;
        }
        if (framesRead == 0)
        {
            break;
        }
        const size_t samplesRead = static_cast<size_t>(framesRead * channels);
        pcmSamples.insert(pcmSamples.end(), chunk.begin(), chunk.begin() + samplesRead);
    }
    ma_decoder_uninit(&decoder);
    if (pcmSamples.empty())
    {
        lastAudioDebugMessage_ = L"audio: miniaudio decoded 0 samples";
        DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath);
        return false;
    }

    const size_t bytesToCopy = pcmSamples.size() * sizeof(ma_int16);
    audioBytes.resize(bytesToCopy);
    memcpy(audioBytes.data(), pcmSamples.data(), bytesToCopy);

    WAVEFORMATEX waveFormat = {};
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = static_cast<WORD>(channels);
    waveFormat.nSamplesPerSec = sampleRate;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = static_cast<WORD>(waveFormat.nChannels * (waveFormat.wBitsPerSample / 8));
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    waveFormat.cbSize = 0;

    formatBytes.resize(sizeof(WAVEFORMATEX));
    memcpy(formatBytes.data(), &waveFormat, sizeof(WAVEFORMATEX));
    lastAudioDebugMessage_ = L"audio: miniaudio decode OK";
    DebugTrace(lastAudioDebugMessage_ + L" path=" + fullPath + L" channels=" + std::to_wstring(channels) + L" rate=" + std::to_wstring(sampleRate) + L" bytes=" + std::to_wstring(audioBytes.size()));
    return true;
}

bool NovelRuntime::PlayAudioFile(AudioChannel channel, const std::wstring& fullPath, bool loop, int volumePercent)
{
    const std::wstring resolvedPath = NormalizeFullPath(fullPath);

    if (!InitializeAudioEngine())
    {
        const_cast<NovelRuntime*>(this)->lastAudioDebugMessage_ = L"audio: XAudio2/MediaFoundation 初期化失敗";
        DebugTrace(lastAudioDebugMessage_);
        return false;
    }

    AudioPlaybackState& state = GetAudioPlaybackState(channel);
    StopAudioChannel(channel);
    if (!DecodeAudioFile(resolvedPath, state.formatBytes, state.audioBytes))
    {
        const std::wstring alias = GetLegacyAudioAlias(channel);
        if (!TryOpenAudioAlias(resolvedPath, alias))
        {
            lastAudioDebugMessage_ = L"audio: デコード失敗 / MCIフォールバック失敗 path=" + resolvedPath;
            DebugTrace(lastAudioDebugMessage_);
            return false;
        }
        ApplyAudioAliasVolume(alias, volumePercent);
        const std::wstring playCommand = loop ? L"play " + alias + L" repeat" : L"play " + alias;
        if (mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr) != 0)
        {
            const std::wstring closeCommand = L"close " + alias;
            mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
            lastAudioDebugMessage_ = L"audio: MCI play 失敗 path=" + resolvedPath;
            DebugTrace(lastAudioDebugMessage_);
            return false;
        }
        state.usesLegacyMci = true;
        state.legacyAlias = alias;
        state.looping = loop;
        state.sourcePath = resolvedPath;
        lastAudioDebugMessage_ = L"audio: MCIフォールバック再生 path=" + resolvedPath;
        DebugTrace(lastAudioDebugMessage_);
        return true;
    }

    const WAVEFORMATEX* waveFormat = reinterpret_cast<const WAVEFORMATEX*>(state.formatBytes.data());
    HRESULT hr = xaudio2_->CreateSourceVoice(&state.voice, waveFormat);
    if (FAILED(hr) || !state.voice)
    {
        StopAudioChannel(channel);
        lastAudioDebugMessage_ = L"audio: CreateSourceVoice 失敗 hr=" + std::to_wstring(static_cast<long long>(hr));
        DebugTrace(lastAudioDebugMessage_);
        return false;
    }

    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = static_cast<UINT32>(state.audioBytes.size());
    buffer.pAudioData = state.audioBytes.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (loop)
    {
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    hr = state.voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr))
    {
        StopAudioChannel(channel);
        lastAudioDebugMessage_ = L"audio: SubmitSourceBuffer 失敗 hr=" + std::to_wstring(static_cast<long long>(hr));
        DebugTrace(lastAudioDebugMessage_);
        return false;
    }

    SetAudioChannelVolume(channel, volumePercent);
    hr = state.voice->Start(0);
    if (FAILED(hr))
    {
        StopAudioChannel(channel);
        lastAudioDebugMessage_ = L"audio: Start 失敗 hr=" + std::to_wstring(static_cast<long long>(hr));
        DebugTrace(lastAudioDebugMessage_);
        return false;
    }

    state.looping = loop;
    state.sourcePath = resolvedPath;
    lastAudioDebugMessage_ = L"audio: XAudio2再生開始 path=" + resolvedPath +
        L" channels=" + std::to_wstring(waveFormat->nChannels) +
        L" rate=" + std::to_wstring(waveFormat->nSamplesPerSec) +
        L" bytes=" + std::to_wstring(state.audioBytes.size());
    DebugTrace(lastAudioDebugMessage_);
    return true;
}

bool NovelRuntime::Initialize()
{
    if (gdiplusToken_ != 0)
    {
        return true;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Gdiplus::Ok)
    {
        return false;
    }
    RefreshAvailableFonts();
    LoadRecentProjects();
    return InitializeAudioEngine();
}

void NovelRuntime::Shutdown()
{
    DeleteAutosaveSnapshot();
    DestroyChildControls();
    if (hostWindow_)
    {
        KillTimer(hostWindow_, 1);
    }
    if (previewWindow_)
    {
        DestroyWindow(previewWindow_);
        previewWindow_ = nullptr;
    }

    ShutdownAudioEngine();
    backgroundImage_.reset();
    leftCharacter_.image.reset();
    centerCharacter_.image.reset();
    rightCharacter_.image.reset();
    for (ToolbarItem& item : toolbarItems_)
    {
        item.iconImage.reset();
    }
    toolbarItems_.clear();

    for (const std::wstring& path : loadedPrivateFontPaths_)
    {
        RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    }
    loadedPrivateFontPaths_.clear();
    availableFonts_.clear();

    if (gdiplusToken_ != 0)
    {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

const std::wstring& NovelRuntime::GetWindowTitle() const
{
    return storyTitle_;
}

void NovelRuntime::StopAssetPreviewAudio()
{
    StopAudioChannel(AudioChannel::Preview);
}

void NovelRuntime::StartAssetPreview(const AssetListItem& item)
{
    if (item.isDirectory)
    {
        selectedAssetPath_.clear();
        selectedAssetLabel_.clear();
        selectedAssetPreviewCategory_.clear();
        StopAssetPreviewAudio();
        return;
    }

    selectedAssetPath_ = item.path;
    selectedAssetLabel_ = item.label;
    selectedAssetPreviewCategory_ = item.category;

    if (item.category == L"bgm" || item.category == L"se")
    {
        StopAssetPreviewAudio();
        if (PlayAudioFile(AudioChannel::Preview, item.path, false, assetPreviewVolume_))
        {
            statusText_ = item.label + L" をプレビュー中";
        }
        else
        {
            statusText_ = item.label + L" のプレビュー再生に失敗しました";
        }
    }
    else
    {
        StopAssetPreviewAudio();
        statusText_ = item.label + L" を選択しました";
    }
}

void NovelRuntime::SetPlayerMode(bool enabled)
{
    playerMode_ = enabled;
}

bool NovelRuntime::IsPlayerMode() const
{
    return playerMode_;
}

void NovelRuntime::SetHostWindow(HWND hWnd)
{
    hostWindow_ = hWnd;
    SetTimer(hostWindow_, 1, 16, nullptr);
    if (!playerMode_)
    {
        EnsureChildControls();
    }
}

void NovelRuntime::NotifyPreviewWindowDestroyed()
{
    previewWindow_ = nullptr;
    previewVisible_ = false;
}

void NovelRuntime::RefreshPreviewWindow()
{
    if (previewWindow_ && IsWindow(previewWindow_) && IsWindowVisible(previewWindow_))
    {
        SetWindowTextW(previewWindow_, storyTitle_.c_str());
        InvalidateRect(previewWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::EnsureChildControls()
{
    if (!hostWindow_)
    {
        return;
    }

    if (!eventSearchEdit_)
    {
        eventSearchEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            eventFilterText_.c_str(),
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EVENT_SEARCH)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    if (!inspectorEdit_)
    {
        inspectorEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INSPECTOR_EDIT)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    if (!eventTextEdit_)
    {
        eventTextEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOVSCROLL | ES_MULTILINE | WS_VSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EVENT_TEXT_EDIT)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    if (!sceneNameEdit_)
    {
        sceneNameEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCENE_NAME_EDIT)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    if (!characterFieldEdit_)
    {
        characterFieldEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHARACTER_FIELD_EDIT)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    if (!variableFieldEdit_)
    {
        variableFieldEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hostWindow_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VARIABLE_FIELD_EDIT)),
            GetModuleHandleW(nullptr),
            nullptr);
    }
}

void NovelRuntime::DestroyChildControls()
{
    if (eventSearchEdit_)
    {
        DestroyWindow(eventSearchEdit_);
        eventSearchEdit_ = nullptr;
    }
    if (inspectorEdit_)
    {
        DestroyWindow(inspectorEdit_);
        inspectorEdit_ = nullptr;
    }
    if (eventTextEdit_)
    {
        DestroyWindow(eventTextEdit_);
        eventTextEdit_ = nullptr;
    }
    if (sceneNameEdit_)
    {
        DestroyWindow(sceneNameEdit_);
        sceneNameEdit_ = nullptr;
    }
    if (characterFieldEdit_)
    {
        DestroyWindow(characterFieldEdit_);
        characterFieldEdit_ = nullptr;
    }
    if (variableFieldEdit_)
    {
        DestroyWindow(variableFieldEdit_);
        variableFieldEdit_ = nullptr;
    }
}

RECT NovelRuntime::GetToolbarRect(const RECT& previewRect) const
{
    return RECT{ previewRect.left + 16, previewRect.top + 12, previewRect.right - 16, previewRect.top + 62 };
}

RECT NovelRuntime::GetLeftPanelRect(const RECT& clientRect) const
{
    if (!showComponents_)
    {
        return RECT{ clientRect.left, clientRect.top, clientRect.left, clientRect.bottom };
    }
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int maxWidth = clientWidth - rightPanelWidth_ - 340;
    const int width = (std::max)(220, (std::min)(leftPanelWidth_, maxWidth));
    return RECT{ clientRect.left, clientRect.top, clientRect.left + width, clientRect.bottom };
}

RECT NovelRuntime::GetRightPanelRect(const RECT& clientRect) const
{
    if (!showInspector_)
    {
        return RECT{ clientRect.right, clientRect.top, clientRect.right, clientRect.bottom };
    }
    const RECT leftRect = GetLeftPanelRect(clientRect);
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int maxWidth = clientWidth - static_cast<int>(leftRect.right) - 260;
    const int width = (std::max)(250, (std::min)(rightPanelWidth_, maxWidth));
    return RECT{ clientRect.right - width, clientRect.top, clientRect.right, clientRect.bottom };
}

RECT NovelRuntime::GetPreviewRect(const RECT& clientRect) const
{
    const RECT leftRect = GetLeftPanelRect(clientRect);
    const LONG left = showComponents_ ? leftRect.right + 8 : clientRect.left + 8;
    const LONG right = showInspector_ ? GetRightPanelRect(clientRect).left - 8 : clientRect.right - 8;
    return RECT{ left, clientRect.top, right, clientRect.bottom };
}

int NovelRuntime::GetPreviewContentTop(const RECT& previewRect) const
{
    int currentTop = GetToolbarRect(previewRect).bottom + 12;
    if (showFlowGraph_)
    {
        currentTop += graphHeight_ + 18;
    }
    if (showEventList_)
    {
        currentTop += eventListHeight_ + 18;
    }
    return currentTop;
}

bool NovelRuntime::HasVisibleArea(const RECT& rect) const
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

RECT NovelRuntime::GetGraphRect(const RECT& previewRect) const
{
    if (!showFlowGraph_)
    {
        const int top = GetToolbarRect(previewRect).bottom + 12;
        return RECT{ previewRect.left + 16, top, previewRect.right - 16, top };
    }
    const RECT toolbarRect = GetToolbarRect(previewRect);
    const int top = toolbarRect.bottom + 12;
    const int bottom = top + (std::max)(120, graphHeight_);
    return RECT{ previewRect.left + 16, top, previewRect.right - 16, bottom };
}

RECT NovelRuntime::GetStageRect(const RECT& previewRect) const
{
    if (!showPreviewPanel_)
    {
        const int top = GetPreviewContentTop(previewRect);
        return RECT{ previewRect.left, top, previewRect.right, top };
    }
    const int top = GetPreviewContentTop(previewRect);
    return RECT{ previewRect.left, top, previewRect.right, previewRect.bottom };
}

RECT NovelRuntime::GetMessageRect(const RECT& previewRect) const
{
    if (!showPreviewPanel_)
    {
        return RECT{ previewRect.left, previewRect.bottom, previewRect.right, previewRect.bottom };
    }
    return RECT{ previewRect.left, previewRect.bottom - 170, previewRect.right, previewRect.bottom };
}

RECT NovelRuntime::GetEventListRect(const RECT& previewRect) const
{
    if (!showEventList_)
    {
        const RECT graphRect = GetGraphRect(previewRect);
        const int top = graphRect.bottom + (showFlowGraph_ ? 18 : 0);
        return RECT{ previewRect.left + 16, top, previewRect.right - 16, top };
    }
    const RECT toolbarRect = GetToolbarRect(previewRect);
    const int top = toolbarRect.bottom + 12;
    const int bottom = previewRect.bottom - 16;
    return RECT{ previewRect.left + 16, top, previewRect.right - 16, bottom };
}

void NovelRuntime::InitializeToolbarItems()
{
    toolbarItems_.clear();
    toolbarItems_.push_back(ToolbarItem{ L"project", L"プロジェクト", L"ui\\project.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"preview", L"\u30d7\u30ec\u30d3\u30e5\u30fc", L"ui\\preview.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"save", L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u4fdd\u5b58", L"ui\\save.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"characters", L"\u30ad\u30e3\u30e9\u30af\u30bf\u30fc\u7ba1\u7406", L"ui\\material_character.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"config", L"\u8a2d\u5b9a", L"ui\\config.png", nullptr });
    toolbarItems_.push_back(ToolbarItem{ L"build", L"\u66f8\u304d\u51fa\u3057", L"ui\\build.png", nullptr });

    if (uiButtons_.empty())
    {
        uiButtons_.push_back(UiButtonDefinition{ L"save", L"SAVE", L"ui\\save.png", -348, -52, 36, 36, false, nullptr });
        uiButtons_.push_back(UiButtonDefinition{ L"load", L"LOAD", L"ui\\load.png", -296, -52, 36, 36, false, nullptr });
        uiButtons_.push_back(UiButtonDefinition{ L"log", L"LOG", L"ui\\log.png", -244, -52, 36, 36, false, nullptr });
        uiButtons_.push_back(UiButtonDefinition{ L"hide", L"HIDE", L"ui\\hide.png", -192, -52, 36, 36, false, nullptr });
        uiButtons_.push_back(UiButtonDefinition{ L"menu", L"MENU", L"ui\\menu_ui.png", -140, -52, 36, 36, true, nullptr });
    }
}

void NovelRuntime::LoadToolbarIcons()
{
    const std::wstring assetsRoot = GetAssetsRootDirectory();
    for (ToolbarItem& item : toolbarItems_)
    {
        item.iconImage.reset();

        std::vector<std::wstring> candidates;
        if (!item.iconPath.empty())
        {
            candidates.push_back(CombinePath(assetsRoot, item.iconPath));
            candidates.push_back(item.iconPath);

            const size_t dot = item.iconPath.find_last_of(L'.');
            if (dot != std::wstring::npos)
            {
                const std::wstring stem = item.iconPath.substr(0, dot);
                candidates.push_back(CombinePath(assetsRoot, stem + L".jpg"));
                candidates.push_back(stem + L".jpg");
                candidates.push_back(CombinePath(assetsRoot, stem + L".jpeg"));
                candidates.push_back(stem + L".jpeg");
                candidates.push_back(CombinePath(assetsRoot, stem + L".png"));
                candidates.push_back(CombinePath(assetsRoot, stem + L".bmp"));
            }
        }

        for (const std::wstring& candidate : candidates)
        {
            auto image = TryLoadImage(candidate);
            if (image)
            {
                item.iconImage = std::move(image);
                break;
            }
        }
    }
}

std::wstring NovelRuntime::GetCommandParameter(const ScriptCommand& command, const std::wstring& key) const
{
    const auto found = command.parameters.find(key);
    return found == command.parameters.end() ? L"" : found->second;
}

bool NovelRuntime::TryParseHexColor(const std::wstring& value, COLORREF& color) const
{
    const std::wstring text = Trim(value);
    if (text.size() == 7 && text[0] == L'#')
    {
        const int red = std::stoi(text.substr(1, 2), nullptr, 16);
        const int green = std::stoi(text.substr(3, 2), nullptr, 16);
        const int blue = std::stoi(text.substr(5, 2), nullptr, 16);
        color = RGB(red, green, blue);
        return true;
    }

    return false;
}

std::wstring NovelRuntime::FormatHexColor(COLORREF color) const
{
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return buffer;
}

COLORREF NovelRuntime::ShowColorPresetMenu(POINT point, COLORREF currentColor) const
{
    if (!hostWindow_)
    {
        return currentColor;
    }

    struct PresetColor
    {
        const wchar_t* label;
        COLORREF color;
    };

    const PresetColor presets[] =
    {
        { L"ダーク", RGB(8, 10, 14) },
        { L"スレート", RGB(24, 32, 44) },
        { L"ネイビー", RGB(18, 28, 52) },
        { L"グレー", RGB(56, 60, 68) },
        { L"シアン", RGB(80, 132, 180) },
        { L"ブルー", RGB(52, 122, 188) },
        { L"ゴールド", RGB(184, 150, 82) },
        { L"レッド", RGB(132, 58, 66) },
        { L"グリーン", RGB(76, 108, 76) },
        { L"ホワイト", RGB(230, 234, 240) },
    };

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return currentColor;
    }

    const UINT kBaseId = 52000;
    for (size_t i = 0; i < _countof(presets); ++i)
    {
        UINT flags = MF_STRING;
        if (presets[i].color == currentColor)
        {
            flags |= MF_CHECKED;
        }
        const std::wstring label = std::wstring(presets[i].label) + L"  " + FormatHexColor(presets[i].color);
        AppendMenuW(menu, flags, kBaseId + static_cast<UINT>(i), label.c_str());
    }

    POINT screenPoint = point;
    ClientToScreen(hostWindow_, &screenPoint);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hostWindow_, nullptr);
    DestroyMenu(menu);

    if (command >= kBaseId && command < kBaseId + _countof(presets))
    {
        return presets[command - kBaseId].color;
    }
    return currentColor;
}

bool NovelRuntime::TryGetNumber(const std::wstring& value, long long& number) const
{
    if (value.empty())
    {
        return false;
    }

    wchar_t* endPtr = nullptr;
    number = wcstoll(value.c_str(), &endPtr, 10);
    return endPtr != value.c_str() && endPtr != nullptr && *endPtr == L'\0';
}

std::unique_ptr<Gdiplus::Image> NovelRuntime::TryLoadImage(const std::wstring& fullPath) const
{
    if (fullPath.empty())
    {
        return nullptr;
    }

    auto image = std::make_unique<Gdiplus::Image>(fullPath.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok)
    {
        return nullptr;
    }

    return image;
}

std::wstring NovelRuntime::GetFontsDirectory() const
{
    return CombinePath(GetAssetsRootDirectory(), L"fonts");
}

void NovelRuntime::RefreshAvailableFonts()
{
    for (const std::wstring& path : loadedPrivateFontPaths_)
    {
        RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    }
    loadedPrivateFontPaths_.clear();
    availableFonts_.clear();

    if (gdiplusToken_ == 0)
    {
        return;
    }

    const std::wstring fontDirectory = GetFontsDirectory();
    const std::vector<std::wstring> fontFiles = EnumerateFiles(fontDirectory, L"*.*");
    for (const std::wstring& filePath : fontFiles)
    {
        std::wstring lower = filePath;
        if (!lower.empty())
        {
            CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
        }
        const bool supported =
            (lower.size() >= 4 && (lower.substr(lower.size() - 4) == L".ttf" || lower.substr(lower.size() - 4) == L".otf")) ||
            (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".ttc");
        if (!supported)
        {
            continue;
        }

        Gdiplus::PrivateFontCollection collection;
        if (collection.AddFontFile(filePath.c_str()) != Gdiplus::Ok)
        {
            continue;
        }

        const INT familyCount = collection.GetFamilyCount();
        if (familyCount <= 0)
        {
            continue;
        }

        std::vector<Gdiplus::FontFamily> families(static_cast<size_t>(familyCount));
        INT found = 0;
        if (collection.GetFamilies(familyCount, families.data(), &found) != Gdiplus::Ok || found <= 0)
        {
            continue;
        }

        WCHAR familyName[LF_FACESIZE] = {};
        if (families[0].GetFamilyName(familyName) != Gdiplus::Ok || familyName[0] == L'\0')
        {
            continue;
        }

        AddFontResourceExW(filePath.c_str(), FR_PRIVATE, nullptr);
        loadedPrivateFontPaths_.push_back(filePath);
        availableFonts_.push_back(FontAssetItem{ filePath, GetFileNamePart(filePath), familyName });
    }
}

std::wstring NovelRuntime::GetNextAvailableFont(const std::wstring& current) const
{
    if (availableFonts_.empty())
    {
        return current.empty() ? editorSettings_.defaultFont : current;
    }

    for (size_t i = 0; i < availableFonts_.size(); ++i)
    {
        if (availableFonts_[i].family == current)
        {
            return availableFonts_[(i + 1) % availableFonts_.size()].family;
        }
    }

    return availableFonts_.front().family;
}

CharacterSlot* NovelRuntime::GetCharacterSlot(const std::wstring& position)
{
    const std::wstring normalized = Trim(position);
    if (normalized == L"left")
    {
        return &leftCharacter_;
    }
    if (normalized == L"center")
    {
        return &centerCharacter_;
    }
    if (normalized == L"right")
    {
        return &rightCharacter_;
    }

    return nullptr;
}

void NovelRuntime::ApplyBackgroundCommand(const ScriptCommand& command)
{
    backgroundDisplayName_ = GetCommandParameter(command, L"name");
    backgroundVisible_ = ParseBoolValue(GetCommandParameter(command, L"visible"), true);
    backgroundOffsetX_ = ParseIntValue(GetCommandParameter(command, L"x"), 0);
    backgroundOffsetY_ = ParseIntValue(GetCommandParameter(command, L"y"), 0);
    backgroundScale_ = (std::max)(1, ParseIntValue(GetCommandParameter(command, L"scale"), 100));
    backgroundOpacity_ = ClampByteValue(ParseIntValue(GetCommandParameter(command, L"opacity"), 255));

    const std::wstring colorValue = GetCommandParameter(command, L"color");
    if (!colorValue.empty())
    {
        COLORREF color = backgroundColor_;
        try
        {
            if (TryParseHexColor(colorValue, color))
            {
                backgroundColor_ = color;
                backgroundImage_.reset();
                backgroundPath_ = L"solid";
                statusText_ = L"\u80cc\u666f\u8272\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
                return;
            }
        }
        catch (...)
        {
        }

        statusText_ = L"\u80cc\u666f\u8272\u304c\u4e0d\u6b63\u3067\u3059";
        return;
    }

    const std::wstring relativePath = GetCommandParameter(command, L"storage");
    if (!relativePath.empty())
    {
        std::wstring fullPath = CombinePath(scenarioBaseDir_, relativePath);
        auto image = TryLoadImage(fullPath);
        if (!image)
        {
            image = TryLoadImage(relativePath);
            fullPath = relativePath;
        }

        if (image)
        {
            backgroundImage_ = std::move(image);
            backgroundPath_ = fullPath;
            if (backgroundDisplayName_.empty())
            {
                backgroundDisplayName_ = GetFileStemPart(relativePath);
            }
            statusText_ = L"\u80cc\u666f\u3092\u8aad\u307f\u8fbc\u307f\u307e\u3057\u305f: " + relativePath;
        }
        else
        {
            statusText_ = L"\u80cc\u666f\u753b\u50cf\u304c\u898b\u3064\u304b\u308a\u307e\u305b\u3093: " + relativePath;
        }
    }
}

void NovelRuntime::ApplyCharacterCommand(const ScriptCommand& command)
{
    const std::wstring position = GetCommandParameter(command, L"pos");
    const std::wstring name = GetCommandParameter(command, L"name");
    const std::wstring face = GetCommandParameter(command, L"face");
    if (position.empty())
    {
        statusText_ = L"character command is missing pos";
        return;
    }

    CharacterSlot* slot = GetCharacterSlot(position);
    if (!slot)
    {
        statusText_ = L"unknown character slot: " + position;
        return;
    }

    const CharacterDefinition* definition = FindCharacterDefinition(name);
    slot->displayName = name;
    slot->imagePath.clear();
    slot->image.reset();
    slot->visible = ParseBoolValue(GetCommandParameter(command, L"visible"), true);
    slot->offsetX = ParseIntValue(GetCommandParameter(command, L"x"), 0);
    slot->offsetY = ParseIntValue(GetCommandParameter(command, L"y"), 0);
    slot->scale = (std::max)(1, ParseIntValue(GetCommandParameter(command, L"scale"), 100));
    slot->opacity = ClampByteValue(ParseIntValue(GetCommandParameter(command, L"opacity"), 255));
    if (definition && !definition->displayName.empty())
    {
        slot->displayName = definition->displayName;
    }

    std::wstring storage = GetCommandParameter(command, L"storage");
    if (storage.empty() && definition)
    {
        if (!face.empty())
        {
            for (const CharacterExpressionDefinition& expression : definition->expressions)
            {
                if (expression.name == face && !expression.imagePath.empty())
                {
                    storage = expression.imagePath;
                    break;
                }
            }
        }
        if (storage.empty())
        {
            storage = definition->baseImagePath;
        }
    }

    if (!storage.empty())
    {
        std::wstring fullPath = CombinePath(scenarioBaseDir_, storage);
        auto image = TryLoadImage(fullPath);
        if (!image)
        {
            image = TryLoadImage(storage);
            fullPath = storage;
        }

        if (image)
        {
            slot->imagePath = fullPath;
            slot->image = std::move(image);
            if (slot->displayName.empty())
            {
                slot->displayName = GetFileStemPart(storage);
            }
            statusText_ = L"\u7acb\u3061\u7d75\u3092\u8868\u793a\u3057\u307e\u3057\u305f: " + slot->slotName;
            return;
        }

        statusText_ = L"\u7acb\u3061\u7d75\u304c\u898b\u3064\u304b\u308a\u307e\u305b\u3093: " + storage;
        return;
    }

    statusText_ = L"\u7acb\u3061\u7d75\u306e\u30d7\u30ec\u30fc\u30b9\u30db\u30eb\u30c0\u3092\u8868\u793a\u3057\u307e\u3057\u305f: " + slot->slotName;
}

void NovelRuntime::ApplyHideCharacterCommand(const ScriptCommand& command)
{
    const std::wstring normalized = Trim(GetCommandParameter(command, L"pos"));
    if (normalized.empty() || normalized == L"all")
    {
        leftCharacter_ = { L"left" };
        centerCharacter_ = { L"center" };
        rightCharacter_ = { L"right" };
        statusText_ = L"all characters hidden";
        return;
    }

    CharacterSlot* slot = GetCharacterSlot(normalized);
    if (!slot)
    {
        statusText_ = L"unknown character slot: " + normalized;
        return;
    }

    const std::wstring slotName = slot->slotName;
    *slot = { slotName };
    statusText_ = L"character hidden: " + slot->slotName;
}

void NovelRuntime::ApplySetValueCommand(const ScriptCommand& command)
{
    const std::wstring name = GetCommandParameter(command, L"name");
    if (name.empty())
    {
        statusText_ = L"set requires name";
        return;
    }

    variables_[name] = GetCommandParameter(command, L"value");
    PushVariableHistory(L"SET " + name + L" = " + variables_[name]);
    statusText_ = L"set variable: " + name;
}

void NovelRuntime::ApplyAddValueCommand(const ScriptCommand& command)
{
    const std::wstring name = GetCommandParameter(command, L"name");
    if (name.empty())
    {
        statusText_ = L"add requires name";
        return;
    }

    long long current = 0;
    long long delta = 0;
    TryGetNumber(variables_[name], current);
    if (!TryGetNumber(GetCommandParameter(command, L"value"), delta))
    {
        statusText_ = L"add requires numeric value";
        return;
    }

    variables_[name] = std::to_wstring(current + delta);
    PushVariableHistory(L"ADD " + name + L" => " + variables_[name]);
    statusText_ = L"add variable: " + name;
}

void NovelRuntime::StopBgmPlayback()
{
    StopAudioChannel(AudioChannel::Bgm);
    currentBgmPath_.clear();
}

void NovelRuntime::ApplyBgmCommand(const ScriptCommand& command)
{
    const std::wstring relativePath = GetCommandParameter(command, L"storage");
    if (relativePath.empty())
    {
        statusText_ = L"BGM \u306b\u30d5\u30a1\u30a4\u30eb\u304c\u5fc5\u8981\u3067\u3059";
        return;
    }

    std::wstring fullPath = CombinePath(scenarioBaseDir_, relativePath);
    DWORD attributes = GetFileAttributesW(fullPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        fullPath = relativePath;
        attributes = GetFileAttributesW(fullPath.c_str());
    }
    fullPath = NormalizeFullPath(fullPath);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        statusText_ = L"BGM \u304c\u898b\u3064\u304b\u308a\u307e\u305b\u3093: " + relativePath;
        return;
    }

    int volume = ParseIntValue(GetCommandParameter(command, L"volume"), 100);
    volume = (std::max)(0, (std::min)(100, volume));
    volume = volume * editorSettings_.masterVolume * editorSettings_.bgmVolume / 10000;

    const bool loop = GetCommandParameter(command, L"loop") != L"false";
    if (!PlayAudioFile(AudioChannel::Bgm, fullPath, loop, volume))
    {
        statusText_ = L"BGM \u306e\u518d\u751f\u306b\u5931\u6557\u3057\u307e\u3057\u305f: " + relativePath + L" / " + lastAudioDebugMessage_;
        return;
    }

    currentBgmPath_ = fullPath;
    statusText_ = L"BGM \u3092\u518d\u751f\u3057\u307e\u3057\u305f: " + relativePath;
}

void NovelRuntime::ApplySeCommand(const ScriptCommand& command)
{
    const std::wstring relativePath = GetCommandParameter(command, L"storage");
    if (relativePath.empty())
    {
        statusText_ = L"SE \u306b\u30d5\u30a1\u30a4\u30eb\u304c\u5fc5\u8981\u3067\u3059";
        return;
    }

    std::wstring fullPath = CombinePath(scenarioBaseDir_, relativePath);
    if (GetFileAttributesW(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        fullPath = relativePath;
    }
    fullPath = NormalizeFullPath(fullPath);

    int volume = ParseIntValue(GetCommandParameter(command, L"volume"), 100);
    volume = (std::max)(0, (std::min)(100, volume));
    volume = volume * editorSettings_.masterVolume * editorSettings_.seVolume / 10000;

    const bool loop = ParseBoolValue(GetCommandParameter(command, L"loop"), false);
    if (!PlayAudioFile(AudioChannel::Se, fullPath, loop, volume))
    {
        statusText_ = L"SE \u306e\u518d\u751f\u306b\u5931\u6557\u3057\u307e\u3057\u305f: " + relativePath + L" / " + lastAudioDebugMessage_;
        return;
    }

    statusText_ = L"SE \u3092\u518d\u751f\u3057\u307e\u3057\u305f: " + relativePath;
}

void NovelRuntime::ApplyVoiceCommand(const ScriptCommand& command)
{
    const std::wstring relativePath = GetCommandParameter(command, L"storage");
    if (relativePath.empty())
    {
        statusText_ = L"\u30dc\u30a4\u30b9\u306b\u30d5\u30a1\u30a4\u30eb\u304c\u5fc5\u8981\u3067\u3059";
        return;
    }

    std::wstring fullPath = CombinePath(scenarioBaseDir_, relativePath);
    if (GetFileAttributesW(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        fullPath = relativePath;
    }
    fullPath = NormalizeFullPath(fullPath);

    int volume = ParseIntValue(GetCommandParameter(command, L"volume"), 100);
    volume = (std::max)(0, (std::min)(100, volume));
    volume = volume * editorSettings_.masterVolume * editorSettings_.voiceVolume / 10000;

    const bool loop = ParseBoolValue(GetCommandParameter(command, L"loop"), false);
    if (!PlayAudioFile(AudioChannel::Voice, fullPath, loop, volume))
    {
        statusText_ = L"\u30dc\u30a4\u30b9\u306e\u518d\u751f\u306b\u5931\u6557\u3057\u307e\u3057\u305f: " + relativePath + L" / " + lastAudioDebugMessage_;
        return;
    }

    statusText_ = L"\u30dc\u30a4\u30b9\u3092\u518d\u751f\u3057\u307e\u3057\u305f: " + relativePath;
}

void NovelRuntime::ApplyWaitCommand(const ScriptCommand& command)
{
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 0));
    waitUntilTick_ = duration > 0 ? GetTickCount() + static_cast<DWORD>(duration) : 0;
    statusText_ = L"ウェイト: " + std::to_wstring(duration) + L"ms";
}

void NovelRuntime::ApplyClearTextCommand()
{
    currentText_.clear();
    displayedText_.clear();
    textRevealActive_ = false;
    textRevealIndex_ = 0;
    nextTextRevealTick_ = 0;
    speakerName_.clear();
    statusText_ = L"メッセージを消去しました";
}

void NovelRuntime::ApplyMessageWindowCommand(const ScriptCommand& command)
{
    messageWindowVisible_ = ParseBoolValue(GetCommandParameter(command, L"visible"), true);
    statusText_ = messageWindowVisible_ ? L"メッセージ枠を表示しました" : L"メッセージ枠を非表示にしました";
}

void NovelRuntime::ApplyTextSpeedCommand(const ScriptCommand& command)
{
    textSpeedMs_ = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"value"), textSpeedMs_));
    statusText_ = L"テキスト速度: " + std::to_wstring(textSpeedMs_) + L"ms";
}

void NovelRuntime::ApplyMessageFontCommand(const ScriptCommand& command)
{
    const std::wstring face = Trim(GetCommandParameter(command, L"face"));
    if (!face.empty())
    {
        messageFontFace_ = face;
    }
    else if (!availableFonts_.empty())
    {
        messageFontFace_ = availableFonts_.front().family;
    }
    statusText_ = L"フォントを更新しました: " + messageFontFace_;
}

void NovelRuntime::ApplyMessageFontResetCommand()
{
    messageFontFace_ = editorSettings_.defaultFont;
    statusText_ = L"フォントを初期化しました";
}

void NovelRuntime::ApplyMessageStyleCommand(const ScriptCommand& command)
{
    COLORREF color = messageWindowColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        messageWindowColor_ = color;
    }

    COLORREF borderColor = messageWindowBorderColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"border"), borderColor))
    {
        messageWindowBorderColor_ = borderColor;
    }

    const std::wstring opacityText = Trim(GetCommandParameter(command, L"opacity"));
    if (!opacityText.empty())
    {
        int opacity = ParseIntValue(opacityText, messageWindowOpacity_);
        if (opacity <= 100)
        {
            opacity = (opacity * 255) / 100;
        }
        messageWindowOpacity_ = (std::max)(0, (std::min)(255, opacity));
    }

    const std::wstring paddingText = Trim(GetCommandParameter(command, L"padding"));
    if (!paddingText.empty())
    {
        messageWindowPadding_ = (std::max)(8, (std::min)(64, ParseIntValue(paddingText, messageWindowPadding_)));
    }

    const std::wstring imagePath = Trim(GetCommandParameter(command, L"image"));
    messageWindowImagePath_.clear();
    messageWindowImage_.reset();
    if (!imagePath.empty())
    {
        std::wstring fullPath = CombinePath(scenarioBaseDir_, imagePath);
        auto image = TryLoadImage(fullPath);
        if (!image)
        {
            image = TryLoadImage(imagePath);
            fullPath = imagePath;
        }
        if (image)
        {
            messageWindowImagePath_ = fullPath;
            messageWindowImage_ = std::move(image);
        }
    }

    statusText_ = L"メッセージUIを更新しました";
}

void NovelRuntime::ApplyTextColorCommand(const ScriptCommand& command)
{
    COLORREF color = messageTextColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        messageTextColor_ = color;
        statusText_ = L"本文色を更新しました";
    }
}

void NovelRuntime::ApplyNameColorCommand(const ScriptCommand& command)
{
    COLORREF color = nameTextColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        nameTextColor_ = color;
        statusText_ = L"名前色を更新しました";
    }
}

void NovelRuntime::ApplyNameWindowCommand(const ScriptCommand& command)
{
    nameBoxVisible_ = ParseBoolValue(GetCommandParameter(command, L"visible"), true);

    const std::wstring xText = Trim(GetCommandParameter(command, L"x"));
    if (!xText.empty())
    {
        nameWindowOffsetX_ = ParseIntValue(xText, nameWindowOffsetX_);
    }

    const std::wstring yText = Trim(GetCommandParameter(command, L"y"));
    if (!yText.empty())
    {
        nameWindowOffsetY_ = ParseIntValue(yText, nameWindowOffsetY_);
    }

    const std::wstring widthText = Trim(GetCommandParameter(command, L"width"));
    if (!widthText.empty())
    {
        nameWindowWidth_ = (std::max)(120, ParseIntValue(widthText, nameWindowWidth_));
    }

    const std::wstring heightText = Trim(GetCommandParameter(command, L"height"));
    if (!heightText.empty())
    {
        nameWindowHeight_ = (std::max)(28, ParseIntValue(heightText, nameWindowHeight_));
    }

    const std::wstring paddingText = Trim(GetCommandParameter(command, L"padding"));
    if (!paddingText.empty())
    {
        nameWindowPadding_ = (std::max)(4, ParseIntValue(paddingText, nameWindowPadding_));
    }

    COLORREF color = nameWindowColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        nameWindowColor_ = color;
    }

    COLORREF borderColor = nameWindowBorderColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"border"), borderColor))
    {
        nameWindowBorderColor_ = borderColor;
    }

    const std::wstring opacityText = Trim(GetCommandParameter(command, L"opacity"));
    if (!opacityText.empty())
    {
        int opacity = ParseIntValue(opacityText, nameWindowOpacity_);
        if (opacity <= 100)
        {
            opacity = (opacity * 255) / 100;
        }
        nameWindowOpacity_ = (std::max)(0, (std::min)(255, opacity));
    }

    const std::wstring imagePath = Trim(GetCommandParameter(command, L"image"));
    if (!imagePath.empty() || !GetCommandParameter(command, L"image").empty())
    {
        nameWindowImagePath_.clear();
        nameWindowImage_.reset();
        if (!imagePath.empty())
        {
            std::wstring fullPath = CombinePath(scenarioBaseDir_, imagePath);
            auto image = TryLoadImage(fullPath);
            if (!image)
            {
                image = TryLoadImage(imagePath);
                fullPath = imagePath;
            }
            if (image)
            {
                nameWindowImagePath_ = fullPath;
                nameWindowImage_ = std::move(image);
            }
        }
    }

    statusText_ = nameBoxVisible_ ? L"名前欄を更新しました" : L"名前欄を非表示にしました";
}

void NovelRuntime::ApplyVerticalTextCommand(const ScriptCommand& command)
{
    verticalTextEnabled_ = ParseBoolValue(GetCommandParameter(command, L"enabled"), true);
    statusText_ = verticalTextEnabled_ ? L"縦書きを有効にしました" : L"縦書きを無効にしました";
}

void NovelRuntime::ApplyPageBreakCommand()
{
    currentText_.clear();
    displayedText_.clear();
    textRevealActive_ = false;
    textRevealIndex_ = 0;
    nextTextRevealTick_ = 0;
    statusText_ = L"改ページしました";
}

void NovelRuntime::ApplyShakeCommand(const ScriptCommand& command)
{
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 500));
    shakePower_ = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"power"), 12));
    shakeEndTick_ = duration > 0 ? GetTickCount() + static_cast<DWORD>(duration) : 0;
    statusText_ = L"画面揺れ: " + std::to_wstring(duration) + L"ms";
    if (duration > 0 && !ParseBoolValue(GetCommandParameter(command, L"parallel"), false))
    {
        waitUntilTick_ = shakeEndTick_;
    }
}

void NovelRuntime::ApplyFadeCommand(const ScriptCommand& command)
{
    fadeStartTick_ = GetTickCount();
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 700));
    fadeEndTick_ = duration > 0 ? fadeStartTick_ + static_cast<DWORD>(duration) : 0;
    fadeOpacity_ = (std::max)(0, (std::min)(255, ParseIntValue(GetCommandParameter(command, L"opacity"), 255)));
    COLORREF color = fadeColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        fadeColor_ = color;
    }
    const std::wstring target = Trim(GetCommandParameter(command, L"target"));
    if (target == L"message" ||
        target == L"all" ||
        target == L"background" ||
        target == L"character:left" ||
        target == L"character:center" ||
        target == L"character:right")
    {
        fadeTarget_ = target;
    }
    else
    {
        fadeTarget_ = L"stage";
    }
    statusText_ = L"フェード: " + std::to_wstring(duration) + L"ms";
    if (duration > 0 && !ParseBoolValue(GetCommandParameter(command, L"parallel"), false))
    {
        waitUntilTick_ = fadeEndTick_;
    }
}

void NovelRuntime::ApplyTransitionCommand(const ScriptCommand& command)
{
    ApplyFadeCommand(command);
    const std::wstring style = GetCommandParameter(command, L"style");
    if (!style.empty())
    {
        statusText_ = L"トランジション: " + style;
    }
}

void NovelRuntime::ApplyZoomCommand(const ScriptCommand& command)
{
    zoomStartTick_ = GetTickCount();
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 500));
    zoomEndTick_ = duration > 0 ? zoomStartTick_ + static_cast<DWORD>(duration) : 0;
    zoomStartScale_ = stageScale_;
    zoomTargetScale_ = (std::max)(10, ParseIntValue(GetCommandParameter(command, L"scale"), stageScale_));
    if (duration == 0)
    {
        stageScale_ = zoomTargetScale_;
    }
    statusText_ = L"ズーム: " + std::to_wstring(zoomTargetScale_) + L"%";
    if (duration > 0 && !ParseBoolValue(GetCommandParameter(command, L"parallel"), false))
    {
        waitUntilTick_ = zoomEndTick_;
    }
}

void NovelRuntime::ApplyPanCommand(const ScriptCommand& command)
{
    panStartTick_ = GetTickCount();
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 500));
    panEndTick_ = duration > 0 ? panStartTick_ + static_cast<DWORD>(duration) : 0;
    panStartX_ = stageOffsetX_;
    panStartY_ = stageOffsetY_;
    panTargetX_ = ParseIntValue(GetCommandParameter(command, L"x"), stageOffsetX_);
    panTargetY_ = ParseIntValue(GetCommandParameter(command, L"y"), stageOffsetY_);
    if (duration == 0)
    {
        stageOffsetX_ = panTargetX_;
        stageOffsetY_ = panTargetY_;
    }
    statusText_ = L"パン: X=" + std::to_wstring(panTargetX_) + L" Y=" + std::to_wstring(panTargetY_);
    if (duration > 0 && !ParseBoolValue(GetCommandParameter(command, L"parallel"), false))
    {
        waitUntilTick_ = panEndTick_;
    }
}

void NovelRuntime::ApplyFlashCommand(const ScriptCommand& command)
{
    flashStartTick_ = GetTickCount();
    const int duration = (std::max)(0, ParseIntValue(GetCommandParameter(command, L"time"), 300));
    flashEndTick_ = duration > 0 ? flashStartTick_ + static_cast<DWORD>(duration) : 0;
    flashOpacity_ = (std::max)(0, (std::min)(255, ParseIntValue(GetCommandParameter(command, L"opacity"), 220)));
    COLORREF color = flashColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        flashColor_ = color;
    }
    statusText_ = L"フラッシュ: " + std::to_wstring(duration) + L"ms";
    if (duration > 0 && !ParseBoolValue(GetCommandParameter(command, L"parallel"), false))
    {
        waitUntilTick_ = flashEndTick_;
    }
}

void NovelRuntime::ApplyTintCommand(const ScriptCommand& command)
{
    COLORREF color = tintColor_;
    if (TryParseHexColor(GetCommandParameter(command, L"color"), color))
    {
        tintColor_ = color;
    }
    tintOpacity_ = (std::max)(0, (std::min)(255, ParseIntValue(GetCommandParameter(command, L"opacity"), tintOpacity_)));
    statusText_ = L"色調を更新しました";
}

bool NovelRuntime::EvaluateCondition(const ScriptCommand& command) const
{
    const std::wstring name = GetCommandParameter(command, L"name");
    const std::wstring op = GetCommandParameter(command, L"op");
    const std::wstring right = GetCommandParameter(command, L"value");
    const auto found = variables_.find(name);
    const std::wstring left = found == variables_.end() ? L"" : found->second;
    const std::wstring normalizedOp = op.empty() ? L"eq" : op;

    long long leftNumber = 0;
    long long rightNumber = 0;
    const bool numeric = TryGetNumber(left, leftNumber) && TryGetNumber(right, rightNumber);

    if (normalizedOp == L"eq")
    {
        return numeric ? leftNumber == rightNumber : left == right;
    }
    if (normalizedOp == L"ne")
    {
        return numeric ? leftNumber != rightNumber : left != right;
    }
    if (normalizedOp == L"gt" && numeric)
    {
        return leftNumber > rightNumber;
    }
    if (normalizedOp == L"ge" && numeric)
    {
        return leftNumber >= rightNumber;
    }
    if (normalizedOp == L"lt" && numeric)
    {
        return leftNumber < rightNumber;
    }
    if (normalizedOp == L"le" && numeric)
    {
        return leftNumber <= rightNumber;
    }

    return false;
}

bool NovelRuntime::JumpToLabel(const std::wstring& target)
{
    if (target == L"__load")
    {
        LoadRuntimeStateFromDialog();
        return false;
    }
    if (target == L"__options")
    {
        ShowSettingsDialog();
        return false;
    }
    if (target == L"__exit")
    {
        if (hostWindow_)
        {
            PostMessageW(hostWindow_, WM_CLOSE, 0, 0);
        }
        return false;
    }

    const auto found = scenario_.labels.find(target);
    if (found == scenario_.labels.end())
    {
        std::wstring normalizedTarget = target;
        if (!normalizedTarget.empty())
        {
            CharLowerBuffW(&normalizedTarget[0], static_cast<DWORD>(normalizedTarget.size()));
        }
        if (normalizedTarget.size() >= 3 && normalizedTarget.substr(normalizedTarget.size() - 3) == L".ks")
        {
            const std::wstring candidates[] =
            {
                target,
                CombinePath(scenarioBaseDir_, target),
                CombinePath(GetScenarioDirectory(), target),
                CombinePath(GetAssetsRootDirectory(), target),
            };
            for (const std::wstring& candidate : candidates)
            {
                if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    LoadScenario(candidate);
                    return false;
                }
            }
        }
        currentText_ = L"jump target not found: " + target;
        reachedEnd_ = true;
        return false;
    }

    currentCommandIndex_ = found->second;
    return true;
}

void NovelRuntime::ApplyIfJumpCommand(const ScriptCommand& command)
{
    if (EvaluateCondition(command))
    {
        JumpToLabel(GetCommandParameter(command, L"target"));
        PushVariableHistory(L"IF " + GetCommandParameter(command, L"name") + L" = true");
        statusText_ = L"condition: true";
    }
    else
    {
        PushVariableHistory(L"IF " + GetCommandParameter(command, L"name") + L" = false");
        statusText_ = L"condition: false";
    }
}

void NovelRuntime::PushVariableHistory(const std::wstring& entry)
{
    if (entry.empty())
    {
        return;
    }

    variableHistory_.push_back(entry);
    if (variableHistory_.size() > 48)
    {
        variableHistory_.erase(variableHistory_.begin(), variableHistory_.begin() + static_cast<std::ptrdiff_t>(variableHistory_.size() - 48));
    }
}

void NovelRuntime::ActivateChoice(const ScriptCommand& command)
{
    activeChoices_.clear();
    for (size_t i = 0; i < command.links.size(); ++i)
    {
        const std::wstring conditionName = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_name_", i));
        const std::wstring conditionOp = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_op_", i));
        const std::wstring conditionValue = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_value_", i));
        const bool showDisabled = ParseBoolValue(GetCommandParameter(command, GetChoiceParamKey(L"__choice_show_disabled_", i)), false);

        bool visible = true;
        bool enabled = true;
        if (!conditionName.empty())
        {
            ScriptCommand conditionCommand;
            conditionCommand.parameters[L"name"] = conditionName;
            conditionCommand.parameters[L"op"] = conditionOp.empty() ? L"eq" : conditionOp;
            conditionCommand.parameters[L"value"] = conditionValue;
            enabled = EvaluateCondition(conditionCommand);
            visible = enabled || showDisabled;
        }

        if (visible)
        {
            activeChoices_.push_back(ActiveChoiceItem{ command.links[i].first, command.links[i].second, enabled });
        }
    }
    activeChoiceRects_.assign(activeChoices_.size(), RECT{});
    waitingForChoice_ = !activeChoices_.empty();
    currentText_ = GetCommandParameter(command, L"prompt");
    if (currentText_.empty())
    {
        currentText_ = L"Choose an option.";
    }
    displayedText_ = currentText_;
    textRevealActive_ = false;
    textRevealIndex_ = currentText_.size();
    nextTextRevealTick_ = 0;
    statusText_ = waitingForChoice_ ? L"waiting for choice" : L"選択肢の表示条件に一致しませんでした";
}

void NovelRuntime::PushBacklogEntry(const std::wstring& speaker, const std::wstring& text)
{
    std::wstring entry = speaker.empty() ? text : speaker + L" : " + text;
    if (entry.empty())
    {
        return;
    }
    backlogEntries_.push_back(entry);
    if (backlogEntries_.size() > 200)
    {
        backlogEntries_.erase(backlogEntries_.begin(), backlogEntries_.begin() + static_cast<std::ptrdiff_t>(backlogEntries_.size() - 200));
    }
}

void NovelRuntime::LoadUiButtonIcons()
{
    const std::wstring assetsRoot = GetAssetsRootDirectory();
    for (UiButtonDefinition& button : uiButtons_)
    {
        button.iconImage.reset();

        std::vector<std::wstring> candidates;
        if (!button.iconPath.empty())
        {
            candidates.push_back(CombinePath(assetsRoot, button.iconPath));
            candidates.push_back(button.iconPath);

            const size_t dot = button.iconPath.find_last_of(L'.');
            if (dot != std::wstring::npos)
            {
                const std::wstring stem = button.iconPath.substr(0, dot);
                candidates.push_back(CombinePath(assetsRoot, stem + L".png"));
                candidates.push_back(CombinePath(assetsRoot, stem + L".jpg"));
                candidates.push_back(CombinePath(assetsRoot, stem + L".jpeg"));
                candidates.push_back(CombinePath(assetsRoot, stem + L".bmp"));
            }
        }

        if (candidates.empty() && !button.id.empty())
        {
            const std::wstring uiDir = CombinePath(assetsRoot, L"ui");
            if (button.id == L"menu")
            {
                candidates.push_back(CombinePath(uiDir, L"menu_ui.png"));
                candidates.push_back(CombinePath(uiDir, L"menu_ui.jpg"));
                candidates.push_back(CombinePath(uiDir, L"menu_ui.jpeg"));
                candidates.push_back(CombinePath(uiDir, L"menu_ui.bmp"));
            }
            candidates.push_back(CombinePath(uiDir, button.id + L".png"));
            candidates.push_back(CombinePath(uiDir, button.id + L".jpg"));
            candidates.push_back(CombinePath(uiDir, button.id + L".jpeg"));
            candidates.push_back(CombinePath(uiDir, button.id + L".bmp"));
        }

        for (const std::wstring& candidate : candidates)
        {
            auto image = TryLoadImage(candidate);
            if (image)
            {
                button.iconImage = std::move(image);
                break;
            }
        }
    }
}

void NovelRuntime::SelectChoice(size_t index)
{
    if (index >= activeChoices_.size())
    {
        return;
    }

    if (!activeChoices_[index].enabled)
    {
        statusText_ = L"この選択肢はまだ選べません";
        return;
    }

    const std::wstring target = activeChoices_[index].target;
    activeChoices_.clear();
    activeChoiceRects_.clear();
    waitingForChoice_ = false;
    statusText_ = L"choice: " + std::to_wstring(index + 1);
    if (JumpToLabel(target))
    {
        Advance();
    }
}

std::vector<ScenarioIssue> NovelRuntime::ValidateScenario() const
{
    std::vector<ScenarioIssue> issues;
    std::unordered_map<std::wstring, size_t> labelFirstIndex;
    std::unordered_set<std::wstring> knownVariables;

    auto addIssue = [&](size_t commandIndex, const std::wstring& message)
    {
        issues.push_back(ScenarioIssue{ commandIndex, message });
    };

    auto hasLabel = [&](const std::wstring& label)
    {
        return !label.empty() && scenario_.labels.find(label) != scenario_.labels.end();
    };
    auto isScenarioFileTarget = [&](const std::wstring& target)
    {
        std::wstring normalized = target;
        if (!normalized.empty())
        {
            CharLowerBuffW(&normalized[0], static_cast<DWORD>(normalized.size()));
        }
        if (normalized.size() < 3 || normalized.substr(normalized.size() - 3) != L".ks")
        {
            return false;
        }

        const std::wstring candidates[] =
        {
            target,
            CombinePath(scenarioBaseDir_, target),
            CombinePath(GetScenarioDirectory(), target),
            CombinePath(GetAssetsRootDirectory(), target),
        };
        for (const std::wstring& candidate : candidates)
        {
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return true;
            }
        }
        return false;
    };

    for (const VariableDefinition& variable : variableDefinitions_)
    {
        if (!variable.name.empty())
        {
            knownVariables.insert(variable.name);
        }
    }

    for (size_t i = 0; i < scenario_.commands.size(); ++i)
    {
        const ScriptCommand& command = scenario_.commands[i];
        if (command.type != ScriptCommand::Type::Label)
        {
            continue;
        }

        const std::wstring label = GetCommandParameter(command, L"name");
        if (label.empty())
        {
            addIssue(i, L"ラベル名が空です");
            continue;
        }

        const auto inserted = labelFirstIndex.insert({ label, i });
        if (!inserted.second)
        {
            addIssue(i, L"ラベルが重複しています: " + label);
        }
    }

    std::vector<bool> reachable(scenario_.commands.size(), false);
    std::vector<size_t> stack;
    if (!scenario_.commands.empty())
    {
        stack.push_back(0);
    }
    while (!stack.empty())
    {
        const size_t index = stack.back();
        stack.pop_back();
        if (index >= scenario_.commands.size() || reachable[index])
        {
            continue;
        }

        reachable[index] = true;
        const ScriptCommand& command = scenario_.commands[index];
        auto pushNext = [&]()
        {
            if (index + 1 < scenario_.commands.size())
            {
                stack.push_back(index + 1);
            }
        };
        auto pushLabel = [&](const std::wstring& label)
        {
            const auto found = scenario_.labels.find(label);
            if (found != scenario_.labels.end())
            {
                stack.push_back(found->second);
            }
        };

        if (command.type == ScriptCommand::Type::Jump)
        {
            pushLabel(GetCommandParameter(command, L"target"));
        }
        else if (command.type == ScriptCommand::Type::Choice)
        {
            for (const auto& link : command.links)
            {
                pushLabel(link.second);
            }
        }
        else if (command.type == ScriptCommand::Type::IfJump)
        {
            pushLabel(GetCommandParameter(command, L"target"));
            pushNext();
        }
        else
        {
            pushNext();
        }
    }

    for (size_t i = 0; i < scenario_.commands.size(); ++i)
    {
        const ScriptCommand& command = scenario_.commands[i];
        if (!reachable.empty() && !reachable[i])
        {
            addIssue(i, L"このイベントには到達できません");
        }

        if (command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::IfJump)
        {
            const std::wstring target = GetCommandParameter(command, L"target");
            if (target.empty())
            {
                addIssue(i, L"遷移先ラベルが空です");
            }
            else if (!hasLabel(target) && !isScenarioFileTarget(target))
            {
                addIssue(i, L"遷移先ラベルが見つかりません: " + target);
            }
        }

        if (command.type == ScriptCommand::Type::Choice)
        {
            if (command.links.empty())
            {
                addIssue(i, L"選択肢の枝がありません");
            }
            for (size_t linkIndex = 0; linkIndex < command.links.size(); ++linkIndex)
            {
                const auto& link = command.links[linkIndex];
                if (link.first.empty())
                {
                    addIssue(i, L"選択肢文が空です: " + std::to_wstring(linkIndex + 1));
                }
                if (link.second.empty())
                {
                    addIssue(i, L"選択肢の遷移先が空です: " + std::to_wstring(linkIndex + 1));
                }
                else if (!hasLabel(link.second) && !isScenarioFileTarget(link.second))
                {
                    addIssue(i, L"選択肢の遷移先が見つかりません: " + link.second);
                }

                const std::wstring conditionName = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_name_", linkIndex));
                if (!conditionName.empty() && knownVariables.find(conditionName) == knownVariables.end())
                {
                    addIssue(i, L"選択肢条件の変数が未定義です: " + conditionName);
                }
            }
        }

        if (command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue || command.type == ScriptCommand::Type::IfJump)
        {
            const std::wstring name = GetCommandParameter(command, L"name");
            if (name.empty())
            {
                addIssue(i, L"変数名が空です");
            }
            else
            {
                if (command.type == ScriptCommand::Type::IfJump && knownVariables.find(name) == knownVariables.end())
                {
                    addIssue(i, L"条件分岐の変数が未定義です: " + name);
                }
                knownVariables.insert(name);
            }
        }

        if (command.type == ScriptCommand::Type::AddValue)
        {
            long long number = 0;
            if (!TryGetNumber(GetCommandParameter(command, L"value"), number))
            {
                addIssue(i, L"加算値が数値ではありません");
            }
        }

        if (command.type == ScriptCommand::Type::Background ||
            command.type == ScriptCommand::Type::Character ||
            command.type == ScriptCommand::Type::Bgm ||
            command.type == ScriptCommand::Type::Se ||
            command.type == ScriptCommand::Type::Voice)
        {
            const std::wstring storage = GetCommandParameter(command, L"storage");
            if (!storage.empty() && !FileExistsForScenario(scenarioBaseDir_, storage))
            {
                addIssue(i, L"素材ファイルが見つかりません: " + storage);
            }
        }

        const wchar_t* numericKeys[] = { L"time", L"x", L"y", L"scale", L"opacity", L"volume", L"fadein", L"fadeout", L"power" };
        for (const wchar_t* key : numericKeys)
        {
            const std::wstring value = GetCommandParameter(command, key);
            long long number = 0;
            if (!value.empty() && !TryGetNumber(value, number))
            {
                addIssue(i, std::wstring(L"数値欄が不正です: ") + key);
            }
        }
    }

    return issues;
}

std::wstring NovelRuntime::GetFirstIssueForCommand(size_t commandIndex) const
{
    const std::vector<ScenarioIssue> issues = ValidateScenario();
    for (const ScenarioIssue& issue : issues)
    {
        if (issue.commandIndex == commandIndex)
        {
            return issue.message;
        }
    }
    return L"";
}

bool NovelRuntime::SelectFirstScenarioIssue()
{
    const std::vector<ScenarioIssue> issues = ValidateScenario();
    if (issues.empty())
    {
        statusText_ = L"検証完了: 問題は見つかりませんでした";
        return true;
    }

    selectedCommandIndex_ = (std::min)(issues.front().commandIndex, scenario_.commands.empty() ? 0 : scenario_.commands.size() - 1);
    selectedChoiceLinkIndex_ = 0;
    statusText_ = L"検証: " + std::to_wstring(issues.size()) + L" 件 / " + issues.front().message;
    return true;
}

void NovelRuntime::LoadScenario(const std::wstring& requestedPath)
{
    StopBgmPlayback();
    StopAssetPreviewAudio();
    currentBgmPath_.clear();
    messageFontFace_ = editorSettings_.defaultFont;
    backgroundPath_.clear();
    backgroundImage_.reset();
    backgroundColor_ = RGB(28, 36, 48);
    leftCharacter_ = { L"left" };
    centerCharacter_ = { L"center" };
    rightCharacter_ = { L"right" };
    storyTitle_ = L"Kaktos Engine";
    speakerName_.clear();
    currentText_ = L"Loading scenario...";
    displayedText_ = currentText_;
    statusText_.clear();
    scenarioBaseDir_.clear();
    scenarioPath_.clear();
    projectPath_.clear();
    selectedScenePath_.clear();
    scenario_ = ScenarioDocument{};
    variables_.clear();
    currentCommandIndex_ = 0;
    activeChoices_.clear();
    activeChoiceRects_.clear();
    backlogEntries_.clear();
    commandRowRects_.clear();
    commandRowIndices_.clear();
    graphNodeRects_.clear();
    graphNodeIndices_.clear();
    eventRowRects_.clear();
    eventRowIndices_.clear();
    currentEventListRect_ = {};
    toolbarButtonRects_.clear();
    inspectorEditTargets_.clear();
    eventAddTextRect_ = {};
    eventAddChoiceRect_ = {};
    eventValidateRect_ = {};
    eventDeleteRect_ = {};
    eventDuplicateRect_ = {};
    inspectorCommitRect_ = {};
    inspectorCancelRect_ = {};
    selectedCommandIndex_ = 0;
    selectedChoiceLinkIndex_ = 0;
    verticalTextEnabled_ = false;
    textSpeedMs_ = editorSettings_.defaultTextSpeed;
    messageTextColor_ = RGB(242, 244, 247);
    nameTextColor_ = RGB(123, 203, 255);
    ApplyEditorUiDefaults();
    selectedAssetPath_.clear();
    selectedAssetLabel_.clear();
    selectedAssetPreviewCategory_.clear();
    previewVisible_ = false;
    previewMenuVisible_ = false;
    previewLogVisible_ = false;
    previewSkipMode_ = false;
    previewAutoMode_ = false;
    autoAdvanceTick_ = 0;
    leftPanelWidth_ = 280;
    rightPanelWidth_ = 320;
    graphHeight_ = 162;
    eventListHeight_ = 208;
    eventListScrollOffset_ = 0;
    activeDragHandle_ = DragHandle::None;
    waitingForChoice_ = false;
    reachedEnd_ = false;
    variableHistory_.clear();
    textRevealActive_ = false;
    textRevealIndex_ = displayedText_.size();
    nextTextRevealTick_ = 0;
    InitializeToolbarItems();

    std::wstring scenarioText;
    std::wstring loadedPath;
    if (!requestedPath.empty() && TryReadTextFile(requestedPath, scenarioText))
    {
        loadedPath = requestedPath;
    }
    else
    {
        for (const std::wstring& candidate : GetScenarioCandidates())
        {
            if (TryReadTextFile(candidate, scenarioText))
            {
                loadedPath = candidate;
                break;
            }
        }
    }

    if (loadedPath.empty())
    {
        statusText_ = L"assets/main.ks not found";
        currentText_ = L"Place a scenario file in assets/main.ks.";
        reachedEnd_ = true;
        return;
    }

    ScenarioDocument document;
    std::wstring parseError;
    if (!ParseScenario(scenarioText, document, parseError))
    {
        statusText_ = L"load failed: " + loadedPath;
        currentText_ = parseError;
        reachedEnd_ = true;
        return;
    }

    scenario_ = std::move(document);
    storyTitle_ = scenario_.title;
    statusText_ = L"loaded: " + loadedPath;
    scenarioPath_ = loadedPath;
    selectedScenePath_ = loadedPath;
    scenarioBaseDir_ = GetDirectoryPath(loadedPath);
    projectPath_ = CombinePath(GetAssetsRootDirectory(), L"project.kproj");
    LoadProjectSettings(projectPath_);
    if (!autosaveRestoreChecked_ && !restoringAutosave_)
    {
        autosaveRestoreChecked_ = true;
        std::wstring autosaveContent;
        if (TryReadTextFile(GetAutosavePath(), autosaveContent))
        {
            if (MessageBoxW(hostWindow_, L"前回の編集中データが見つかりました。復元しますか?", L"オートセーブ復元", MB_ICONQUESTION | MB_YESNO) == IDYES)
            {
                restoringAutosave_ = true;
                const bool restored = RestoreAutosaveSnapshot(false);
                restoringAutosave_ = false;
                if (restored)
                {
                    return;
                }
            }
        }
    }
    RefreshSceneList();
    RefreshAssetList();
    LoadToolbarIcons();
    LoadUiButtonIcons();
    selectedCommandIndex_ = 0;
    selectedChoiceLinkIndex_ = 0;
    MarkCurrentStateSaved();
    Advance();
}

void NovelRuntime::Advance()
{
    if (waitingForChoice_)
    {
        return;
    }

    while (currentCommandIndex_ < scenario_.commands.size())
    {
        const ScriptCommand& command = scenario_.commands[currentCommandIndex_++];
        if (ParseBoolValue(GetCommandParameter(command, L"disabled"), false))
        {
            continue;
        }
        switch (command.type)
        {
        case ScriptCommand::Type::Title:
        {
            const std::wstring title = GetCommandParameter(command, L"name");
            if (!title.empty())
            {
                storyTitle_ = title;
            }
            break;
        }
        case ScriptCommand::Type::Background:
            ApplyBackgroundCommand(command);
            break;
        case ScriptCommand::Type::Character:
            ApplyCharacterCommand(command);
            break;
        case ScriptCommand::Type::HideCharacter:
            ApplyHideCharacterCommand(command);
            break;
        case ScriptCommand::Type::Speaker:
            speakerName_ = GetCommandParameter(command, L"name");
            break;
        case ScriptCommand::Type::ClearSpeaker:
            speakerName_.clear();
            break;
        case ScriptCommand::Type::Text:
            currentText_ = GetCommandParameter(command, L"value");
            if (currentText_.empty())
            {
                currentText_ = L" ";
            }
            if (textSpeedMs_ <= 0)
            {
                displayedText_ = currentText_;
                textRevealIndex_ = currentText_.size();
                textRevealActive_ = false;
                nextTextRevealTick_ = 0;
            }
            else
            {
                displayedText_.clear();
                textRevealIndex_ = 0;
                textRevealActive_ = true;
                nextTextRevealTick_ = GetTickCount() + static_cast<DWORD>(textSpeedMs_);
            }
            PushBacklogEntry(speakerName_, currentText_);
            return;
        case ScriptCommand::Type::Choice:
            ActivateChoice(command);
            if (waitingForChoice_)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Bgm:
            ApplyBgmCommand(command);
            break;
        case ScriptCommand::Type::StopBgm:
            StopBgmPlayback();
            statusText_ = L"bgm stopped";
            break;
        case ScriptCommand::Type::Se:
            ApplySeCommand(command);
            break;
        case ScriptCommand::Type::Voice:
            ApplyVoiceCommand(command);
            break;
        case ScriptCommand::Type::Wait:
            ApplyWaitCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::ClearText:
            ApplyClearTextCommand();
            break;
        case ScriptCommand::Type::MessageWindow:
            ApplyMessageWindowCommand(command);
            break;
        case ScriptCommand::Type::TextSpeed:
            ApplyTextSpeedCommand(command);
            break;
        case ScriptCommand::Type::MessageFont:
            ApplyMessageFontCommand(command);
            break;
        case ScriptCommand::Type::MessageFontReset:
            ApplyMessageFontResetCommand();
            break;
        case ScriptCommand::Type::MessageStyle:
            ApplyMessageStyleCommand(command);
            break;
        case ScriptCommand::Type::TextColor:
            ApplyTextColorCommand(command);
            break;
        case ScriptCommand::Type::NameColor:
            ApplyNameColorCommand(command);
            break;
        case ScriptCommand::Type::NameWindow:
            ApplyNameWindowCommand(command);
            break;
        case ScriptCommand::Type::VerticalText:
            ApplyVerticalTextCommand(command);
            break;
        case ScriptCommand::Type::PageBreak:
            ApplyPageBreakCommand();
            break;
        case ScriptCommand::Type::Shake:
            ApplyShakeCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Fade:
            ApplyFadeCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Transition:
            ApplyTransitionCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Zoom:
            ApplyZoomCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Pan:
            ApplyPanCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Flash:
            ApplyFlashCommand(command);
            if (waitUntilTick_ != 0)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Tint:
            ApplyTintCommand(command);
            break;
        case ScriptCommand::Type::SetValue:
            ApplySetValueCommand(command);
            break;
        case ScriptCommand::Type::AddValue:
            ApplyAddValueCommand(command);
            break;
        case ScriptCommand::Type::IfJump:
            ApplyIfJumpCommand(command);
            if (reachedEnd_)
            {
                return;
            }
            break;
        case ScriptCommand::Type::Jump:
            if (!JumpToLabel(GetCommandParameter(command, L"target")))
            {
                return;
            }
            break;
        case ScriptCommand::Type::Label:
            break;
        }
    }

    reachedEnd_ = true;
    currentText_ = L"End of script.";
}

bool NovelRuntime::HandleClick(POINT point)
{
    if (projectDialogVisible_)
    {
        if (PtInRect(&projectDialogCreateRect_, point))
        {
            return CreateProjectFromDialog();
        }
        if (PtInRect(&projectDialogOpenRect_, point))
        {
            return LoadProjectFromDialog();
        }
        if (PtInRect(&projectDialogCancelRect_, point) || !PtInRect(&projectDialogRect_, point))
        {
            HideProjectDialog();
            statusText_ = L"プロジェクト操作を閉じました";
            return true;
        }
        return true;
    }

    if (projectLauncherVisible_)
    {
        return HandleProjectLauncherClick(point);
    }

    if (variableManagerVisible_)
    {
        if (variableFieldDialogVisible_)
        {
            if (PtInRect(&variableFieldOkRect_, point))
            {
                CommitVariableFieldEdit();
                return true;
            }
            if (PtInRect(&variableFieldCancelRect_, point) || !PtInRect(&variableFieldDialogRect_, point))
            {
                CancelVariableFieldEdit();
                return true;
            }
            return true;
        }

        if (PtInRect(&variableDialogAddRect_, point))
        {
            if (sceneNameEdit_)
            {
                const int length = GetWindowTextLengthW(sceneNameEdit_);
                std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(sceneNameEdit_, &value[0], length + 1);
                value.resize(length);
                value = Trim(value);
                if (!value.empty())
                {
                    bool exists = false;
                    for (const VariableDefinition& definition : variableDefinitions_)
                    {
                        if (definition.name == value)
                        {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists)
                    {
                        VariableDefinition definition;
                        definition.name = value;
                        definition.type = VariableType::Integer;
                        definition.initialValue = L"0";
                        variableDefinitions_.push_back(definition);
                        variables_[value] = definition.initialValue;
                        selectedVariableDefinitionIndex_ = variableDefinitions_.size() - 1;
                        SetWindowTextW(sceneNameEdit_, L"");
                        SaveProject();
                        statusText_ = L"変数を追加しました";
                    }
                }
            }
            return true;
        }
        if (PtInRect(&variableDialogDeleteRect_, point))
        {
            if (selectedVariableDefinitionIndex_ < variableDefinitions_.size())
            {
                const std::wstring name = variableDefinitions_[selectedVariableDefinitionIndex_].name;
                variableDefinitions_.erase(variableDefinitions_.begin() + static_cast<std::ptrdiff_t>(selectedVariableDefinitionIndex_));
                selectedVariableDefinitionIndex_ = variableDefinitions_.empty() ? static_cast<size_t>(-1) : 0;
                variables_.erase(name);
                SaveProject();
                statusText_ = L"変数定義を削除しました";
            }
            return true;
        }
        if (PtInRect(&variableDialogCloseRect_, point) || !PtInRect(&variableDialogRect_, point))
        {
            HideVariableManagerDialog();
            return true;
        }
        for (size_t i = 0; i < variableDefinitionRects_.size() && i < variableDefinitions_.size(); ++i)
        {
            if (PtInRect(&variableDefinitionRects_[i], point))
            {
                selectedVariableDefinitionIndex_ = i;
                return true;
            }
        }
        for (const VariableManagerActionTarget& target : variableManagerActionTargets_)
        {
            if (!PtInRect(&target.buttonRect, point) || target.variableIndex >= variableDefinitions_.size())
            {
                continue;
            }

            selectedVariableDefinitionIndex_ = target.variableIndex;
            VariableDefinition& definition = variableDefinitions_[target.variableIndex];
            if (target.action == L"edit_name")
            {
                BeginVariableFieldEdit(L"変数名", L"edit_name", target.variableIndex, definition.name);
                return true;
            }
            if (target.action == L"cycle_type")
            {
                definition.type = definition.type == VariableType::Bool ? VariableType::Integer : (definition.type == VariableType::Integer ? VariableType::String : VariableType::Bool);
                if (definition.type == VariableType::Bool && definition.initialValue.empty()) definition.initialValue = L"false";
                else if (definition.type == VariableType::Integer && definition.initialValue.empty()) definition.initialValue = L"0";
                variables_[definition.name] = definition.initialValue;
                SaveProject();
                statusText_ = L"変数型を切り替えました";
                return true;
            }
            if (target.action == L"edit_initial")
            {
                BeginVariableFieldEdit(L"初期値", L"edit_initial", target.variableIndex, definition.initialValue);
                return true;
            }
            if (target.action == L"edit_current")
            {
                BeginVariableFieldEdit(L"現在値", L"edit_current", target.variableIndex, variables_[definition.name]);
                return true;
            }
            if (target.action == L"edit_description")
            {
                BeginVariableFieldEdit(L"説明", L"edit_description", target.variableIndex, definition.description);
                return true;
            }
        }
        return true;
    }

    if (settingsDialogVisible_)
    {
        if (PtInRect(&settingsDialogCloseRect_, point) || !PtInRect(&settingsDialogRect_, point))
        {
            HideSettingsDialog();
            return true;
        }
        if (PtInRect(&settingsDialogRestoreRect_, point))
        {
            RestoreAutosaveSnapshot(true);
            return true;
        }
        for (const SettingsActionTarget& target : settingsActionTargets_)
        {
            if (!PtInRect(&target.buttonRect, point))
            {
                continue;
            }

            if (target.action == L"cycle_window")
            {
                const std::pair<int, int> presets[] = { {1280, 720}, {1600, 900}, {1920, 1080} };
                size_t current = 0;
                for (size_t i = 0; i < _countof(presets); ++i)
                {
                    if (editorSettings_.windowWidth == presets[i].first && editorSettings_.windowHeight == presets[i].second)
                    {
                        current = i;
                        break;
                    }
                }
                current = (current + 1) % _countof(presets);
                editorSettings_.windowWidth = presets[current].first;
                editorSettings_.windowHeight = presets[current].second;
                SetWindowPos(hostWindow_, nullptr, 0, 0, editorSettings_.windowWidth, editorSettings_.windowHeight, SWP_NOMOVE | SWP_NOZORDER);
                SaveProject();
                return true;
            }
            if (StartsWithText(target.action, L"settings_tab:"))
            {
                const size_t index = static_cast<size_t>(_wtoi(target.action.substr(13).c_str()));
                selectedSettingsCategoryIndex_ = index;
                settingsScrollOffset_ = 0;
                return true;
            }
            if (target.action == L"cycle_font")
            {
                RefreshAvailableFonts();
                const std::wstring selectedFont = ShowFontSelectionMenu(point, editorSettings_.defaultFont);
                if (!selectedFont.empty() && selectedFont != editorSettings_.defaultFont)
                {
                    editorSettings_.defaultFont = selectedFont;
                    statusText_ = L"既定メッセージフォントを更新しました: " + editorSettings_.defaultFont;
                    SaveProject();
                    RefreshPreviewIfActive();
                }
                return true;
            }
            auto adjustVolume = [&](int& value, int delta, const std::wstring& text)
            {
                value = (std::max)(0, (std::min)(100, value + delta));
                statusText_ = text + L": " + std::to_wstring(value);
                SaveProject();
            };
            auto applyUiDefaults = [&]()
            {
                ApplyEditorUiDefaults();
                SaveProject();
                RefreshPreviewIfActive();
            };
            if (target.action == L"text_speed_minus") { editorSettings_.defaultTextSpeed = (std::max)(0, editorSettings_.defaultTextSpeed - 10); textSpeedMs_ = editorSettings_.defaultTextSpeed; SaveProject(); RefreshPreviewIfActive(); return true; }
            if (target.action == L"text_speed_plus") { editorSettings_.defaultTextSpeed = (std::min)(500, editorSettings_.defaultTextSpeed + 10); textSpeedMs_ = editorSettings_.defaultTextSpeed; SaveProject(); RefreshPreviewIfActive(); return true; }
            if (target.action == L"master_minus") { adjustVolume(editorSettings_.masterVolume, -10, L"マスター音量"); return true; }
            if (target.action == L"master_plus") { adjustVolume(editorSettings_.masterVolume, 10, L"マスター音量"); return true; }
            if (target.action == L"bgm_minus") { adjustVolume(editorSettings_.bgmVolume, -10, L"BGM音量"); return true; }
            if (target.action == L"bgm_plus") { adjustVolume(editorSettings_.bgmVolume, 10, L"BGM音量"); return true; }
            if (target.action == L"se_minus") { adjustVolume(editorSettings_.seVolume, -10, L"SE音量"); return true; }
            if (target.action == L"se_plus") { adjustVolume(editorSettings_.seVolume, 10, L"SE音量"); return true; }
            if (target.action == L"voice_minus") { adjustVolume(editorSettings_.voiceVolume, -10, L"ボイス音量"); return true; }
            if (target.action == L"voice_plus") { adjustVolume(editorSettings_.voiceVolume, 10, L"ボイス音量"); return true; }
            if (target.action == L"browse_save_dir")
            {
                editorSettings_.saveDirectory = BrowseForFolder(L"セーブ保存先を選択", editorSettings_.saveDirectory);
                SaveProject();
                return true;
            }
            if (target.action == L"toggle_autosave")
            {
                editorSettings_.autosaveEnabled = !editorSettings_.autosaveEnabled;
                if (!editorSettings_.autosaveEnabled)
                {
                    DeleteAutosaveSnapshot();
                }
                else
                {
                    SaveAutosaveSnapshot();
                }
                SaveProject();
                return true;
            }
            auto toggleTitleMenu = [&](bool& value, const std::wstring& label)
            {
                value = !value;
                statusText_ = label + (value ? L" を表示します" : L" を非表示にしました");
                SaveProject();
                RefreshPreviewIfActive();
            };
            if (target.action == L"title_start_toggle")
            {
                toggleTitleMenu(editorSettings_.titleMenuStartEnabled, L"はじめる");
                return true;
            }
            if (target.action == L"title_load_toggle")
            {
                toggleTitleMenu(editorSettings_.titleMenuLoadEnabled, L"ロード");
                return true;
            }
            if (target.action == L"title_options_toggle")
            {
                toggleTitleMenu(editorSettings_.titleMenuOptionsEnabled, L"オプション");
                return true;
            }
            if (target.action == L"title_exit_toggle")
            {
                toggleTitleMenu(editorSettings_.titleMenuExitEnabled, L"終了");
                return true;
            }
            if (target.action == L"msg_toggle")
            {
                editorSettings_.defaultMessageWindowVisible = !editorSettings_.defaultMessageWindowVisible;
                statusText_ = editorSettings_.defaultMessageWindowVisible ? L"既定メッセージ枠を表示します" : L"既定メッセージ枠を非表示にしました";
                applyUiDefaults();
                return true;
            }
            if (target.action == L"msg_opacity_minus") { editorSettings_.defaultMessageWindowOpacity = (std::max)(0, editorSettings_.defaultMessageWindowOpacity - 16); applyUiDefaults(); return true; }
            if (target.action == L"msg_opacity_plus") { editorSettings_.defaultMessageWindowOpacity = (std::min)(255, editorSettings_.defaultMessageWindowOpacity + 16); applyUiDefaults(); return true; }
            if (target.action == L"msg_padding_minus") { editorSettings_.defaultMessageWindowPadding = (std::max)(8, editorSettings_.defaultMessageWindowPadding - 2); applyUiDefaults(); return true; }
            if (target.action == L"msg_padding_plus") { editorSettings_.defaultMessageWindowPadding = (std::min)(64, editorSettings_.defaultMessageWindowPadding + 2); applyUiDefaults(); return true; }
            if (target.action == L"name_toggle")
            {
                editorSettings_.defaultNameWindowVisible = !editorSettings_.defaultNameWindowVisible;
                statusText_ = editorSettings_.defaultNameWindowVisible ? L"既定名前欄を表示します" : L"既定名前欄を非表示にしました";
                applyUiDefaults();
                return true;
            }
            if (target.action == L"name_x_minus") { editorSettings_.defaultNameWindowOffsetX -= 8; applyUiDefaults(); return true; }
            if (target.action == L"name_x_plus") { editorSettings_.defaultNameWindowOffsetX += 8; applyUiDefaults(); return true; }
            if (target.action == L"name_y_minus") { editorSettings_.defaultNameWindowOffsetY -= 8; applyUiDefaults(); return true; }
            if (target.action == L"name_y_plus") { editorSettings_.defaultNameWindowOffsetY += 8; applyUiDefaults(); return true; }
            if (target.action == L"name_width_minus") { editorSettings_.defaultNameWindowWidth = (std::max)(120, editorSettings_.defaultNameWindowWidth - 20); applyUiDefaults(); return true; }
            if (target.action == L"name_width_plus") { editorSettings_.defaultNameWindowWidth = (std::min)(640, editorSettings_.defaultNameWindowWidth + 20); applyUiDefaults(); return true; }
            if (target.action == L"name_height_minus") { editorSettings_.defaultNameWindowHeight = (std::max)(28, editorSettings_.defaultNameWindowHeight - 4); applyUiDefaults(); return true; }
            if (target.action == L"name_height_plus") { editorSettings_.defaultNameWindowHeight = (std::min)(160, editorSettings_.defaultNameWindowHeight + 4); applyUiDefaults(); return true; }
            if (target.action == L"name_opacity_minus") { editorSettings_.defaultNameWindowOpacity = (std::max)(0, editorSettings_.defaultNameWindowOpacity - 16); applyUiDefaults(); return true; }
            if (target.action == L"name_opacity_plus") { editorSettings_.defaultNameWindowOpacity = (std::min)(255, editorSettings_.defaultNameWindowOpacity + 16); applyUiDefaults(); return true; }
            if (target.action == L"msg_browse_image" || target.action == L"name_browse_image")
            {
                OPENFILENAMEW ofn = {};
                WCHAR fileBuffer[MAX_PATH] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hostWindow_;
                ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = fileBuffer;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    if (target.action == L"msg_browse_image")
                    {
                        editorSettings_.defaultMessageWindowImage = MakeProjectRelativeAssetPath(fileBuffer);
                        statusText_ = L"既定メッセージ画像を更新しました";
                    }
                    else
                    {
                        editorSettings_.defaultNameWindowImage = MakeProjectRelativeAssetPath(fileBuffer);
                        statusText_ = L"既定名前欄画像を更新しました";
                    }
                    applyUiDefaults();
                }
                return true;
            }
            if (target.action == L"msg_color" || target.action == L"msg_border" || target.action == L"name_color_setting" || target.action == L"name_border_setting")
            {
                COLORREF currentColor = RGB(255, 255, 255);
                if (target.action == L"msg_color") currentColor = editorSettings_.defaultMessageWindowColor;
                else if (target.action == L"msg_border") currentColor = editorSettings_.defaultMessageWindowBorderColor;
                else if (target.action == L"name_color_setting") currentColor = editorSettings_.defaultNameWindowColor;
                else if (target.action == L"name_border_setting") currentColor = editorSettings_.defaultNameWindowBorderColor;

                const COLORREF selectedColor = ShowColorPresetMenu(point, currentColor);
                if (selectedColor != currentColor)
                {
                    if (target.action == L"msg_color") editorSettings_.defaultMessageWindowColor = selectedColor;
                    else if (target.action == L"msg_border") editorSettings_.defaultMessageWindowBorderColor = selectedColor;
                    else if (target.action == L"name_color_setting") editorSettings_.defaultNameWindowColor = selectedColor;
                    else if (target.action == L"name_border_setting") editorSettings_.defaultNameWindowBorderColor = selectedColor;
                    applyUiDefaults();
                }
                return true;
            }
            if (target.action == L"msg_clear_image")
            {
                editorSettings_.defaultMessageWindowImage.clear();
                statusText_ = L"既定メッセージ画像を解除しました";
                applyUiDefaults();
                return true;
            }
            if (target.action == L"name_clear_image")
            {
                editorSettings_.defaultNameWindowImage.clear();
                statusText_ = L"既定名前欄画像を解除しました";
                applyUiDefaults();
                return true;
            }
            if (StartsWithText(target.action, L"ui_toggle:") || StartsWithText(target.action, L"ui_left:") || StartsWithText(target.action, L"ui_right:") || StartsWithText(target.action, L"ui_up:") || StartsWithText(target.action, L"ui_down:"))
            {
                const size_t split = target.action.find(L':');
                if (split != std::wstring::npos)
                {
                    const std::wstring action = target.action.substr(0, split);
                    const size_t buttonIndex = static_cast<size_t>(_wtoi(target.action.substr(split + 1).c_str()));
                    if (buttonIndex < uiButtons_.size())
                    {
                        UiButtonDefinition& button = uiButtons_[buttonIndex];
                        if (action == L"ui_toggle")
                        {
                            button.visible = !button.visible;
                            statusText_ = button.label + (button.visible ? L" を表示します" : L" を非表示にしました");
                        }
                        else if (action == L"ui_left")
                        {
                            button.x -= 8;
                        }
                        else if (action == L"ui_right")
                        {
                            button.x += 8;
                        }
                        else if (action == L"ui_up")
                        {
                            button.y -= 8;
                        }
                        else if (action == L"ui_down")
                        {
                            button.y += 8;
                        }
                        SaveProject();
                        RefreshPreviewIfActive();
                    }
                    return true;
                }
            }
            if (StartsWithText(target.action, L"ui_browse_icon:") || StartsWithText(target.action, L"ui_clear_icon:"))
            {
                const size_t split = target.action.find(L':');
                if (split != std::wstring::npos)
                {
                    const std::wstring action = target.action.substr(0, split);
                    const size_t buttonIndex = static_cast<size_t>(_wtoi(target.action.substr(split + 1).c_str()));
                    if (buttonIndex < uiButtons_.size())
                    {
                        UiButtonDefinition& button = uiButtons_[buttonIndex];
                        if (action == L"ui_browse_icon")
                        {
                            OPENFILENAMEW ofn = {};
                            WCHAR fileBuffer[MAX_PATH] = {};
                            ofn.lStructSize = sizeof(ofn);
                            ofn.hwndOwner = hostWindow_;
                            ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
                            ofn.lpstrFile = fileBuffer;
                            ofn.nMaxFile = MAX_PATH;
                            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                            if (GetOpenFileNameW(&ofn))
                            {
                                button.iconPath = fileBuffer;
                                LoadUiButtonIcons();
                                SaveProject();
                                RefreshPreviewIfActive();
                                statusText_ = button.label + L" の画像を更新しました";
                            }
                        }
                        else
                        {
                            button.iconPath.clear();
                            button.iconImage.reset();
                            LoadUiButtonIcons();
                            SaveProject();
                            RefreshPreviewIfActive();
                            statusText_ = button.label + L" の画像を解除しました";
                        }
                    }
                    return true;
                }
            }
        }
        return true;
    }

    if (characterManagerVisible_)
    {
        if (characterFieldDialogVisible_)
        {
            if (PtInRect(&characterDialogFieldOkRect_, point))
            {
                CommitCharacterFieldEdit();
                return true;
            }
            if (PtInRect(&characterDialogFieldCancelRect_, point) || !PtInRect(&characterDialogFieldDialogRect_, point))
            {
                CancelCharacterFieldEdit();
                return true;
            }
            return true;
        }

        if (PtInRect(&characterDialogAddRect_, point))
        {
            if (sceneNameEdit_)
            {
                const int length = GetWindowTextLengthW(sceneNameEdit_);
                std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(sceneNameEdit_, &value[0], length + 1);
                value.resize(length);
                value = Trim(value);
                bool exists = false;
                for (const CharacterDefinition& definition : characterDefinitions_)
                {
                    if (definition.id == value)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!value.empty() && !exists)
                {
                    CharacterDefinition definition;
                    definition.id = value;
                    definition.displayName = value;
                    characterDefinitions_.push_back(definition);
                    selectedCharacterDefinitionIndex_ = characterDefinitions_.size() - 1;
                    SetWindowTextW(sceneNameEdit_, L"");
                    SaveProject();
                    statusText_ = L"キャラクターを追加しました";
                }
            }
            return true;
        }
        if (PtInRect(&characterDialogDeleteRect_, point))
        {
            if (selectedCharacterDefinitionIndex_ < characterDefinitions_.size())
            {
                const std::wstring deletedId = characterDefinitions_[selectedCharacterDefinitionIndex_].id;
                characterDefinitions_.erase(characterDefinitions_.begin() + static_cast<std::ptrdiff_t>(selectedCharacterDefinitionIndex_));
                selectedCharacterDefinitionIndex_ = characterDefinitions_.empty() ? static_cast<size_t>(-1) : 0;
                for (ScriptCommand& command : scenario_.commands)
                {
                    if (command.type == ScriptCommand::Type::Character && GetCommandParameter(command, L"name") == deletedId)
                    {
                        command.parameters[L"name"].clear();
                    }
                }
                SaveProject();
                statusText_ = L"キャラクターを削除しました";
            }
            return true;
        }
        if (PtInRect(&characterDialogCloseRect_, point) || !PtInRect(&characterDialogRect_, point))
        {
            HideCharacterManagerDialog();
            return true;
        }
        for (size_t i = 0; i < characterDefinitionRects_.size() && i < characterDefinitions_.size(); ++i)
        {
            if (PtInRect(&characterDefinitionRects_[i], point))
            {
                selectedCharacterDefinitionIndex_ = i;
                statusText_ = GetCharacterDefinitionLabel(characterDefinitions_[i]) + L" を選択しました";
                return true;
            }
        }
        for (const CharacterManagerActionTarget& target : characterManagerActionTargets_)
        {
            if (!PtInRect(&target.buttonRect, point) || target.characterIndex >= characterDefinitions_.size())
            {
                continue;
            }

            selectedCharacterDefinitionIndex_ = target.characterIndex;
            CharacterDefinition& definition = characterDefinitions_[target.characterIndex];
            if (target.action == L"edit_id")
            {
                BeginCharacterFieldEdit(L"キャラクターID", L"edit_id", target.characterIndex, static_cast<size_t>(-1), definition.id);
                return true;
            }
            if (target.action == L"edit_display_name")
            {
                BeginCharacterFieldEdit(L"表示名", L"edit_display_name", target.characterIndex, static_cast<size_t>(-1), definition.displayName);
                return true;
            }
            if (target.action == L"edit_color")
            {
                BeginCharacterFieldEdit(L"テーマ色", L"edit_color", target.characterIndex, static_cast<size_t>(-1), definition.color);
                return true;
            }
            if (target.action == L"browse_base")
            {
                OPENFILENAMEW ofn = {};
                WCHAR fileBuffer[MAX_PATH] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hostWindow_;
                ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = fileBuffer;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    definition.baseImagePath = fileBuffer;
                    SaveProject();
                    statusText_ = L"基準画像を更新しました";
                    RefreshPreviewIfActive();
                }
                return true;
            }
            if (target.action == L"clear_base")
            {
                definition.baseImagePath.clear();
                SaveProject();
                statusText_ = L"基準画像を解除しました";
                RefreshPreviewIfActive();
                return true;
            }
            if (target.action == L"add_expression")
            {
                definition.expressions.push_back(CharacterExpressionDefinition{ L"新しい差分", L"" });
                SaveProject();
                statusText_ = L"表情差分を追加しました";
                return true;
            }
            if (target.action == L"remove_expression" && target.expressionIndex < definition.expressions.size())
            {
                definition.expressions.erase(definition.expressions.begin() + static_cast<std::ptrdiff_t>(target.expressionIndex));
                SaveProject();
                statusText_ = L"表情差分を削除しました";
                return true;
            }
            if (target.action == L"browse_expression" && target.expressionIndex < definition.expressions.size())
            {
                OPENFILENAMEW ofn = {};
                WCHAR fileBuffer[MAX_PATH] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hostWindow_;
                ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = fileBuffer;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn))
                {
                    definition.expressions[target.expressionIndex].imagePath = fileBuffer;
                    SaveProject();
                    statusText_ = L"表情画像を更新しました";
                    RefreshPreviewIfActive();
                }
                return true;
            }
            if (target.action == L"clear_expression" && target.expressionIndex < definition.expressions.size())
            {
                definition.expressions[target.expressionIndex].imagePath.clear();
                SaveProject();
                statusText_ = L"表情画像を解除しました";
                RefreshPreviewIfActive();
                return true;
            }
            if (target.action == L"edit_expression_name" && target.expressionIndex < definition.expressions.size())
            {
                BeginCharacterFieldEdit(L"表情名", L"edit_expression_name:" + std::to_wstring(target.expressionIndex), target.characterIndex, target.expressionIndex, definition.expressions[target.expressionIndex].name);
                return true;
            }
        }
        return true;
    }

    if (HandleToolbarClick(point))
    {
        return true;
    }
    if (HandleSceneClick(point))
    {
        return true;
    }
    if (HandleAssetClick(point))
    {
        return true;
    }
    if (HandleEventActionClick(point))
    {
        return true;
    }
    if (HandleInspectorClick(point))
    {
        return true;
    }
    for (size_t i = 0; i < eventExpandRects_.size() && i < eventExpandIndices_.size(); ++i)
    {
        if (!PtInRect(&eventExpandRects_[i], point))
        {
            continue;
        }

        const size_t commandIndex = eventExpandIndices_[i];
        selectedCommandIndex_ = commandIndex;
        if (expandedTextCommandIndex_ == commandIndex)
        {
            expandedTextCommandIndex_ = static_cast<size_t>(-1);
            statusText_ = L"\u672c\u6587\u5165\u529b\u30d1\u30cd\u30eb\u3092\u9589\u3058\u307e\u3057\u305f";
        }
        else
        {
            PushUndoSnapshot();
            expandedTextCommandIndex_ = commandIndex;
            statusText_ = L"\u672c\u6587\u5165\u529b\u30d1\u30cd\u30eb\u3092\u958b\u304d\u307e\u3057\u305f";
        }
        return true;
    }
    if (TrySelectCommandFromPoint(point, lastClientRect_))
    {
        return true;
    }

    if (waitingForChoice_)
    {
        for (size_t index = 0; index < activeChoiceRects_.size(); ++index)
        {
            if (PtInRect(&activeChoiceRects_[index], point))
            {
                SelectChoice(index);
                return true;
            }
        }
        return false;
    }

    if (!reachedEnd_)
    {
        Advance();
        return true;
    }

    return false;
}

bool NovelRuntime::HandleDoubleClick(POINT point)
{
    if (projectLauncherVisible_)
    {
        return HandleClick(point);
    }
    if (sceneDialogVisible_)
    {
        return true;
    }
    return HandleClick(point);
}

bool NovelRuntime::HandleRightClick(POINT clientPoint, POINT screenPoint)
{
    if (projectLauncherVisible_)
    {
        return true;
    }

    static constexpr UINT kMenuPreviewFromScene = 61001;
    static constexpr UINT kMenuPreviewFromHere = 61002;

    if (leftPanelTab_ == LeftPanelTab::Scenario)
    {
        const std::wstring scenePath = FindScenePathFromPoint(clientPoint);
        if (!scenePath.empty())
        {
            if (!SwitchScenarioFile(scenePath))
            {
                return true;
            }
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kMenuPreviewFromScene, L"このシナリオからプレビュー");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_SCENE_RENAME, L"名前変更");
            AppendMenuW(menu, MF_STRING, IDM_SCENE_DUPLICATE, L"複製");
            AppendMenuW(menu, MF_STRING, IDM_SCENE_DELETE, L"削除");
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hostWindow_, nullptr);
            DestroyMenu(menu);
            if (command == kMenuPreviewFromScene)
            {
                selectedCommandIndex_ = 0;
                if (!previewWindow_ || !IsWindowVisible(previewWindow_))
                {
                    TogglePreviewWindow();
                }
                else
                {
                    StartPreviewFromIndex(0);
                }
                statusText_ = L"シナリオ先頭からプレビューします";
                return true;
            }
            return command != 0 && ExecuteEditorCommand(command);
        }
    }

    const size_t commandIndex = FindEventIndexFromPoint(clientPoint);
    if (commandIndex != static_cast<size_t>(-1) && commandIndex < scenario_.commands.size())
    {
        selectedCommandIndex_ = commandIndex;
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_EVENT_ADD_TEXT, L"追加");
        AppendMenuW(menu, MF_STRING, IDM_EVENT_DUPLICATE, L"複製");
        AppendMenuW(menu, MF_STRING, IDM_EVENT_DELETE, L"削除");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuPreviewFromHere, L"ここからプレビュー");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const bool disabled = ParseBoolValue(GetCommandParameter(scenario_.commands[commandIndex], L"disabled"), false);
        AppendMenuW(menu, MF_STRING, IDM_EVENT_TOGGLE_ENABLE, disabled ? L"有効化" : L"無効化");
        AppendMenuW(menu, MF_STRING, IDM_EVENT_MOVE_UP, L"上へ移動");
        AppendMenuW(menu, MF_STRING, IDM_EVENT_MOVE_DOWN, L"下へ移動");
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hostWindow_, nullptr);
        DestroyMenu(menu);
        if (command == kMenuPreviewFromHere)
        {
            if (!previewWindow_ || !IsWindowVisible(previewWindow_))
            {
                TogglePreviewWindow();
            }
            else
            {
                StartPreviewFromIndex(commandIndex);
            }
            statusText_ = L"選択位置からプレビューします";
            return true;
        }
        return command != 0 && ExecuteEditorCommand(command);
    }

    return false;
}

bool NovelRuntime::HandleFileDrop(POINT clientPoint, const std::vector<std::wstring>& paths)
{
    if (projectLauncherVisible_)
    {
        return false;
    }

    if (paths.empty())
    {
        return false;
    }

    const RECT leftPanelRect = GetLeftPanelRect(lastClientRect_);
    if (leftPanelTab_ != LeftPanelTab::Materials || !HasVisibleArea(leftPanelRect) || !PtInRect(&leftPanelRect, clientPoint))
    {
        statusText_ = L"素材タブ上にドロップしてください";
        return false;
    }

    return ImportMaterialFiles(paths, selectedAssetCategory_);
}

size_t NovelRuntime::FindEventIndexFromPoint(POINT point) const
{
    for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
    {
        if (PtInRect(&eventRowRects_[i], point))
        {
            return eventRowIndices_[i];
        }
    }
    return static_cast<size_t>(-1);
}

std::wstring NovelRuntime::FindScenePathFromPoint(POINT point) const
{
    for (const SceneListItem& item : sceneItems_)
    {
        if (PtInRect(&item.rect, point))
        {
            return item.path;
        }
    }
    return L"";
}

NovelRuntime::DragHandle NovelRuntime::HitTestDragHandle(POINT point) const
{
    if (PtInRect(&leftSplitterRect_, point))
    {
        return DragHandle::LeftPanel;
    }
    if (PtInRect(&rightSplitterRect_, point))
    {
        return DragHandle::RightPanel;
    }
    if (PtInRect(&graphSplitterRect_, point))
    {
        return DragHandle::GraphHeight;
    }
    if (PtInRect(&eventSplitterRect_, point))
    {
        return DragHandle::EventListHeight;
    }
    return DragHandle::None;
}

bool NovelRuntime::HandleMouseDown(POINT point)
{
    if (projectLauncherVisible_)
    {
        return true;
    }

    if (leftPanelTab_ == LeftPanelTab::Materials)
    {
        for (size_t i = 0; i < assetItems_.size(); ++i)
        {
            const AssetListItem& item = assetItems_[i];
            if (!item.isDirectory && PtInRect(&item.rect, point))
            {
                assetDragActive_ = true;
                assetDragMoved_ = false;
                assetDragSourceIndex_ = i;
                assetDropTargetIndex_ = static_cast<size_t>(-1);
                eventDragStartPoint_ = point;
                dragPoint_ = point;
                return true;
            }
        }
    }

    for (const PaletteButtonItem& item : paletteButtons_)
    {
        if (PtInRect(&item.rect, point))
        {
            paletteDragActive_ = true;
            paletteDropValid_ = false;
            draggedPaletteType_ = item.type;
            draggedPaletteLabel_ = item.label;
            dragPoint_ = point;
            statusText_ = item.label + L"\u3092\u30c9\u30e9\u30c3\u30b0\u4e2d\u3067\u3059";
            return true;
        }
    }

    if (showEventList_ && HasVisibleArea(currentEventListRect_))
    {
        for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
        {
            if (PtInRect(&eventRowRects_[i], point))
            {
                eventReorderDragActive_ = true;
                eventReorderMoved_ = false;
                eventDragSourceIndex_ = eventRowIndices_[i];
                eventDragInsertIndex_ = eventRowIndices_[i];
                eventDragStartPoint_ = point;
                dragPoint_ = point;
                return true;
            }
        }
    }

    activeDragHandle_ = HitTestDragHandle(point);
    if (activeDragHandle_ != DragHandle::None)
    {
        statusText_ = L"\u5883\u754c\u7dda\u3092\u30c9\u30e9\u30c3\u30b0\u3057\u3066\u30ec\u30a4\u30a2\u30a6\u30c8\u3092\u8abf\u6574\u3067\u304d\u307e\u3059";
        return true;
    }
    return false;
}

bool NovelRuntime::HandleMouseMove(POINT point)
{
    lastMousePoint_ = point;
    if (projectLauncherVisible_)
    {
        return false;
    }

    if (assetDragActive_)
    {
        dragPoint_ = point;
        const int deltaX = point.x - eventDragStartPoint_.x;
        const int deltaY = point.y - eventDragStartPoint_.y;
        if (!assetDragMoved_ && (std::abs(deltaX) > 4 || std::abs(deltaY) > 4))
        {
            assetDragMoved_ = true;
            statusText_ = L"素材をドラッグ中です";
        }

        assetDropTargetIndex_ = static_cast<size_t>(-1);
        if (assetDragMoved_ && showEventList_ && HasVisibleArea(currentEventListRect_) && PtInRect(&currentEventListRect_, point))
        {
            assetDropTargetIndex_ = FindEventIndexFromPoint(point);
        }
        return true;
    }

    if (paletteDragActive_)
    {
        dragPoint_ = point;
        paletteDropValid_ = false;
        dragInsertIndex_ = scenario_.commands.size();
        if (showEventList_ && HasVisibleArea(currentEventListRect_) && PtInRect(&currentEventListRect_, point))
        {
            paletteDropValid_ = true;
            if (!eventRowRects_.empty())
            {
                dragInsertIndex_ = scenario_.commands.size();
                for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
                {
                    const RECT& rowRect = eventRowRects_[i];
                    if (point.y < (rowRect.top + rowRect.bottom) / 2)
                    {
                        dragInsertIndex_ = eventRowIndices_[i];
                        break;
                    }
                    dragInsertIndex_ = eventRowIndices_[i] + 1;
                }
            }
            else
            {
                dragInsertIndex_ = 0;
            }
        }
        return true;
    }

    if (eventReorderDragActive_)
    {
        dragPoint_ = point;
        const int deltaX = point.x - eventDragStartPoint_.x;
        const int deltaY = point.y - eventDragStartPoint_.y;
        if (!eventReorderMoved_ && (std::abs(deltaX) > 4 || std::abs(deltaY) > 4))
        {
            eventReorderMoved_ = true;
            statusText_ = L"イベントを並び替え中です";
        }

        if (eventReorderMoved_)
        {
            eventDragInsertIndex_ = scenario_.commands.size();
            eventEffectDropTargetIndex_ = static_cast<size_t>(-1);
            if (showEventList_ && HasVisibleArea(currentEventListRect_) && PtInRect(&currentEventListRect_, point))
            {
                const size_t hoverIndex = FindEventIndexFromPoint(point);
                if (eventDragSourceIndex_ < scenario_.commands.size() &&
                    hoverIndex < scenario_.commands.size() &&
                    hoverIndex != eventDragSourceIndex_ &&
                    (scenario_.commands[eventDragSourceIndex_].type == ScriptCommand::Type::Fade || scenario_.commands[eventDragSourceIndex_].type == ScriptCommand::Type::Transition) &&
                    scenario_.commands[hoverIndex].type == ScriptCommand::Type::Character)
                {
                    eventEffectDropTargetIndex_ = hoverIndex;
                    statusText_ = L"キャラへドロップすると演出対象にできます";
                }
                if (!eventRowRects_.empty())
                {
                    for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
                    {
                        const RECT& rowRect = eventRowRects_[i];
                        if (eventRowIndices_[i] == eventDragSourceIndex_)
                        {
                            continue;
                        }
                        if (point.y < (rowRect.top + rowRect.bottom) / 2)
                        {
                            eventDragInsertIndex_ = eventRowIndices_[i];
                            break;
                        }
                        eventDragInsertIndex_ = eventRowIndices_[i] + 1;
                    }
                }
                else
                {
                    eventDragInsertIndex_ = 0;
                }
            }
        }
        return true;
    }

    int newHoveredToolbarIndex = -1;
    for (size_t i = 0; i < toolbarButtonRects_.size(); ++i)
    {
        if (PtInRect(&toolbarButtonRects_[i], point))
        {
            newHoveredToolbarIndex = static_cast<int>(i);
            break;
        }
    }
    if (newHoveredToolbarIndex != hoveredToolbarIndex_)
    {
        hoveredToolbarIndex_ = newHoveredToolbarIndex;
        return true;
    }

    if (activeDragHandle_ == DragHandle::None)
    {
        return false;
    }

    const int clientWidth = lastClientRect_.right - lastClientRect_.left;
    const int clientHeight = lastClientRect_.bottom - lastClientRect_.top;
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return false;
    }

    switch (activeDragHandle_)
    {
    case DragHandle::LeftPanel:
        leftPanelWidth_ = (std::max)(220, (std::min)(static_cast<int>(point.x), clientWidth - 340));
        statusText_ = L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u5e45: " + std::to_wstring(leftPanelWidth_);
        return true;
    case DragHandle::RightPanel:
        rightPanelWidth_ = (std::max)(250, (std::min)(clientWidth - static_cast<int>(point.x), clientWidth - 280));
        statusText_ = L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u5e45: " + std::to_wstring(rightPanelWidth_);
        return true;
    case DragHandle::GraphHeight:
    {
        const RECT previewRect = GetPreviewRect(lastClientRect_);
        const RECT toolbarRect = GetToolbarRect(previewRect);
        graphHeight_ = (std::max)(120, (std::min)(static_cast<int>(point.y) - (static_cast<int>(toolbarRect.bottom) + 12), 260));
        statusText_ = L"\u30d5\u30ed\u30fc\u9818\u57df: " + std::to_wstring(graphHeight_);
        return true;
    }
    case DragHandle::EventListHeight:
    {
        const RECT previewRect = GetPreviewRect(lastClientRect_);
        const RECT graphRect = GetGraphRect(previewRect);
        eventListHeight_ = (std::max)(140, (std::min)(static_cast<int>(point.y) - (static_cast<int>(graphRect.bottom) + 18), 280));
        statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7: " + std::to_wstring(eventListHeight_);
        return true;
    }
    case DragHandle::None:
        break;
    }

    return false;
}

bool NovelRuntime::HandleMouseUp(POINT point)
{
    if (projectLauncherVisible_)
    {
        assetDragActive_ = false;
        paletteDragActive_ = false;
        eventReorderDragActive_ = false;
        eventEffectDropTargetIndex_ = static_cast<size_t>(-1);
        activeDragHandle_ = DragHandle::None;
        return false;
    }

    if (assetDragActive_)
    {
        dragPoint_ = point;
        const bool moved = assetDragMoved_;
        const size_t sourceIndex = assetDragSourceIndex_;
        const size_t targetIndex = assetDropTargetIndex_;
        assetDragActive_ = false;
        assetDragMoved_ = false;
        assetDragSourceIndex_ = static_cast<size_t>(-1);
        assetDropTargetIndex_ = static_cast<size_t>(-1);

        if (!moved)
        {
            return false;
        }

        if (sourceIndex < assetItems_.size() && targetIndex < scenario_.commands.size())
        {
            PushUndoSnapshot();
            if (ApplyAssetToCommand(targetIndex, assetItems_[sourceIndex]))
            {
                selectedCommandIndex_ = targetIndex;
                RefreshPreviewIfActive();
                return true;
            }
            undoStack_.pop_back();
        }

        statusText_ = L"素材の差し込み先が見つかりませんでした";
        return true;
    }

    if (paletteDragActive_)
    {
        dragPoint_ = point;
        if (paletteDropValid_)
        {
            InsertCommandAtIndex(draggedPaletteType_, dragInsertIndex_);
        }
        else
        {
            statusText_ = L"\u30c9\u30ed\u30c3\u30d7\u5148\u304c\u7121\u3044\u305f\u3081\u8ffd\u52a0\u3057\u307e\u305b\u3093\u3067\u3057\u305f";
        }
        paletteDragActive_ = false;
        paletteDropValid_ = false;
        draggedPaletteLabel_.clear();
        return true;
    }

    if (eventReorderDragActive_)
    {
        dragPoint_ = point;
        const bool moved = eventReorderMoved_;
        const size_t sourceIndex = eventDragSourceIndex_;
        size_t insertIndex = eventDragInsertIndex_;
        const size_t effectDropTargetIndex = eventEffectDropTargetIndex_;
        eventReorderDragActive_ = false;
        eventReorderMoved_ = false;
        eventDragSourceIndex_ = static_cast<size_t>(-1);
        eventDragInsertIndex_ = static_cast<size_t>(-1);
        eventEffectDropTargetIndex_ = static_cast<size_t>(-1);

        if (!moved || sourceIndex >= scenario_.commands.size())
        {
            return false;
        }

        if (effectDropTargetIndex < scenario_.commands.size())
        {
            PushUndoSnapshot();
            if (ApplyEffectToCharacterDrop(sourceIndex, effectDropTargetIndex))
            {
                return true;
            }
            undoStack_.pop_back();
        }

        if (insertIndex > scenario_.commands.size())
        {
            insertIndex = scenario_.commands.size();
        }

        if (insertIndex > sourceIndex)
        {
            --insertIndex;
        }

        if (insertIndex == sourceIndex || insertIndex > scenario_.commands.size())
        {
            statusText_ = L"イベントの位置は変わっていません";
            return true;
        }

        PushUndoSnapshot();
        ScriptCommand movedCommand = scenario_.commands[sourceIndex];
        scenario_.commands.erase(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
        scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(movedCommand));

        auto remapIndex = [&](size_t& index)
        {
            if (index == static_cast<size_t>(-1) || index >= scenario_.commands.size() + 1)
            {
                return;
            }
            if (index == sourceIndex)
            {
                index = insertIndex;
            }
            else if (sourceIndex < insertIndex && index > sourceIndex && index <= insertIndex)
            {
                --index;
            }
            else if (insertIndex < sourceIndex && index >= insertIndex && index < sourceIndex)
            {
                ++index;
            }
        };

        remapIndex(selectedCommandIndex_);
        remapIndex(expandedTextCommandIndex_);
        remapIndex(currentCommandIndex_);
        SyncDocumentMetadata();
        statusText_ = L"イベントの順番を変更しました";
        RefreshPreviewIfActive();
        return true;
    }

    UNREFERENCED_PARAMETER(point);
    const bool wasDragging = activeDragHandle_ != DragHandle::None;
    activeDragHandle_ = DragHandle::None;
    return wasDragging;
}

bool NovelRuntime::HandleMouseWheel(short delta, POINT point)
{
    if (projectLauncherVisible_)
    {
        return false;
    }

    if (settingsDialogVisible_ && PtInRect(&settingsDialogRect_, point))
    {
        const int step = delta > 0 ? -48 : 48;
        settingsScrollOffset_ = (std::max)(0, (std::min)(settingsScrollOffset_ + step, settingsScrollMax_));
        return true;
    }

    const RECT leftPanelRect = GetLeftPanelRect(lastClientRect_);
    if (leftPanelTab_ == LeftPanelTab::Components && PtInRect(&leftPanelRect, point))
    {
        const int step = delta > 0 ? -48 : 48;
        componentScrollOffset_ = (std::max)(0, (std::min)(componentScrollOffset_ + step, componentScrollMax_));
        return true;
    }

    if (HasVisibleArea(currentInspectorRect_) && PtInRect(&currentInspectorRect_, point))
    {
        const int step = delta > 0 ? -48 : 48;
        inspectorScrollOffset_ = (std::max)(0, (std::min)(inspectorScrollOffset_ + step, inspectorScrollMax_));
        return true;
    }

    if (!showEventList_ || !HasVisibleArea(currentEventListRect_) || !PtInRect(&currentEventListRect_, point))
    {
        return false;
    }

    const int rowHeight = 28;
    const int startY = currentEventListRect_.top + 44;
    const int visibleRows = (currentEventListRect_.bottom - startY - 8) / rowHeight;
    const int total = static_cast<int>(BuildFilteredEventIndices().size());
    const int maxOffset = (std::max)(0, total - (std::max)(visibleRows, 1));
    const int step = delta > 0 ? -3 : 3;
    eventListScrollOffset_ = (std::max)(0, (std::min)(eventListScrollOffset_ + step, maxOffset));
    return true;
}

ScriptCommand* NovelRuntime::GetSelectedCommand()
{
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        return nullptr;
    }
    return &scenario_.commands[selectedCommandIndex_];
}

const ScriptCommand* NovelRuntime::GetSelectedCommand() const
{
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        return nullptr;
    }
    return &scenario_.commands[selectedCommandIndex_];
}

void NovelRuntime::SyncDocumentMetadata()
{
    RebuildScenarioLabels(scenario_);
    NormalizePlaybackStateAfterScenarioMutation();
    SyncVariableDefinitions();
    scenario_.title = storyTitle_;
    for (const ScriptCommand& command : scenario_.commands)
    {
        if (command.type == ScriptCommand::Type::Title)
        {
            const auto found = command.parameters.find(L"name");
            if (found != command.parameters.end() && !found->second.empty())
            {
                scenario_.title = found->second;
                storyTitle_ = found->second;
                break;
            }
        }
    }
    SaveAutosaveSnapshot();
}

void NovelRuntime::NormalizePlaybackStateAfterScenarioMutation()
{
    const size_t commandCount = scenario_.commands.size();

    if (commandCount == 0)
    {
        selectedCommandIndex_ = 0;
        expandedTextCommandIndex_ = static_cast<size_t>(-1);
        currentCommandIndex_ = 0;
        waitingForChoice_ = false;
        activeChoices_.clear();
        activeChoiceRects_.clear();
        previewSkipMode_ = false;
        previewAutoMode_ = false;
        autoAdvanceTick_ = 0;
        return;
    }

    if (selectedCommandIndex_ >= commandCount)
    {
        selectedCommandIndex_ = commandCount - 1;
    }
    if (expandedTextCommandIndex_ != static_cast<size_t>(-1) && expandedTextCommandIndex_ >= commandCount)
    {
        expandedTextCommandIndex_ = static_cast<size_t>(-1);
    }
    if (currentCommandIndex_ > commandCount)
    {
        currentCommandIndex_ = commandCount;
    }
    if (editingCommandIndex_ >= commandCount)
    {
        CancelInspectorEdit();
    }
    if (adjustCharacterCommandIndex_ >= commandCount)
    {
        characterAdjustMode_ = false;
        characterAdjustDragging_ = false;
        adjustCharacterCommandIndex_ = static_cast<size_t>(-1);
    }
    if (waitingForChoice_ && currentCommandIndex_ > commandCount)
    {
        waitingForChoice_ = false;
        activeChoices_.clear();
        activeChoiceRects_.clear();
    }
}

ScriptCommand NovelRuntime::CreateDefaultCommand(ScriptCommand::Type type) const
{
    ScriptCommand command;
    command.type = type;
    command.id = L"manual_" + std::to_wstring(scenario_.commands.size() + 1);
    command.sourceLine = selectedCommandIndex_ + 1;

    switch (type)
    {
    case ScriptCommand::Type::Title:
        command.parameters[L"name"] = L"新しいタイトル";
        break;
    case ScriptCommand::Type::Text:
        command.parameters[L"value"] = L"\u65b0\u3057\u3044\u672c\u6587";
        break;
    case ScriptCommand::Type::MessageWindow:
        command.parameters[L"visible"] = L"true";
        break;
    case ScriptCommand::Type::TextSpeed:
        command.parameters[L"value"] = L"40";
        break;
    case ScriptCommand::Type::MessageFont:
        command.parameters[L"face"] = L"Yu Gothic UI";
        break;
    case ScriptCommand::Type::MessageFontReset:
        break;
    case ScriptCommand::Type::MessageStyle:
        command.parameters[L"color"] = L"#080a0e";
        command.parameters[L"border"] = L"#7a808a";
        command.parameters[L"opacity"] = L"70";
        command.parameters[L"padding"] = L"24";
        break;
    case ScriptCommand::Type::Choice:
        command.parameters[L"prompt"] = L"\u65b0\u3057\u3044\u9078\u629e\u80a2";
        command.links.push_back({ L"\u9078\u629e\u80a21", L"start" });
        command.links.push_back({ L"\u9078\u629e\u80a22", L"start" });
        break;
    case ScriptCommand::Type::Jump:
        command.parameters[L"target"] = L"start";
        break;
    case ScriptCommand::Type::Label:
        command.parameters[L"name"] = MakeUniqueLabelName(L"new_label");
        break;
    case ScriptCommand::Type::Background:
        command.parameters[L"name"] = L"背景";
        command.parameters[L"visible"] = L"true";
        command.parameters[L"scale"] = L"100";
        command.parameters[L"opacity"] = L"255";
        break;
    case ScriptCommand::Type::Character:
        command.parameters[L"pos"] = L"center";
        command.parameters[L"name"] = L"キャラクター";
        command.parameters[L"visible"] = L"true";
        command.parameters[L"scale"] = L"100";
        command.parameters[L"opacity"] = L"255";
        break;
    case ScriptCommand::Type::HideCharacter:
        command.parameters[L"pos"] = L"all";
        break;
    case ScriptCommand::Type::Speaker:
        command.parameters[L"name"] = L"話者";
        break;
    case ScriptCommand::Type::Bgm:
        command.parameters[L"category"] = L"bgm";
        command.parameters[L"volume"] = L"100";
        command.parameters[L"loop"] = L"true";
        command.parameters[L"fadein"] = L"0";
        command.parameters[L"fadeout"] = L"0";
        break;
    case ScriptCommand::Type::Se:
    case ScriptCommand::Type::Voice:
        command.parameters[L"category"] = type == ScriptCommand::Type::Se ? L"se" : L"voice";
        command.parameters[L"volume"] = L"100";
        command.parameters[L"loop"] = L"false";
        command.parameters[L"fadein"] = L"0";
        command.parameters[L"fadeout"] = L"0";
        break;
    case ScriptCommand::Type::Wait:
        command.parameters[L"time"] = L"1000";
        break;
    case ScriptCommand::Type::ClearText:
        break;
    case ScriptCommand::Type::TextColor:
    case ScriptCommand::Type::NameColor:
        command.parameters[L"color"] = type == ScriptCommand::Type::TextColor ? L"#f2f4f7" : L"#7bcbff";
        break;
    case ScriptCommand::Type::NameWindow:
        command.parameters[L"visible"] = L"true";
        command.parameters[L"x"] = L"0";
        command.parameters[L"y"] = L"0";
        command.parameters[L"width"] = L"220";
        command.parameters[L"height"] = L"36";
        command.parameters[L"padding"] = L"12";
        command.parameters[L"opacity"] = L"84";
        command.parameters[L"color"] = L"#0c121c";
        command.parameters[L"border"] = L"#5084b4";
        command.parameters[L"image"] = L"";
        break;
    case ScriptCommand::Type::VerticalText:
        command.parameters[L"enabled"] = L"true";
        break;
    case ScriptCommand::Type::PageBreak:
        break;
    case ScriptCommand::Type::Shake:
        command.parameters[L"time"] = L"500";
        command.parameters[L"power"] = L"12";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Fade:
        command.parameters[L"time"] = L"700";
        command.parameters[L"color"] = L"#000000";
        command.parameters[L"opacity"] = L"255";
        command.parameters[L"target"] = L"stage";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Transition:
        command.parameters[L"time"] = L"700";
        command.parameters[L"style"] = L"fade";
        command.parameters[L"color"] = L"#000000";
        command.parameters[L"target"] = L"stage";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Zoom:
        command.parameters[L"time"] = L"500";
        command.parameters[L"scale"] = L"120";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Pan:
        command.parameters[L"time"] = L"500";
        command.parameters[L"x"] = L"0";
        command.parameters[L"y"] = L"0";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Flash:
        command.parameters[L"time"] = L"300";
        command.parameters[L"color"] = L"#ffffff";
        command.parameters[L"opacity"] = L"220";
        command.parameters[L"parallel"] = L"false";
        break;
    case ScriptCommand::Type::Tint:
        command.parameters[L"color"] = L"#ffffff";
        command.parameters[L"opacity"] = L"0";
        break;
    case ScriptCommand::Type::SetValue:
    case ScriptCommand::Type::AddValue:
        command.parameters[L"name"] = L"flag";
        command.parameters[L"value"] = L"1";
        break;
    case ScriptCommand::Type::IfJump:
        command.parameters[L"name"] = L"flag";
        command.parameters[L"op"] = L"eq";
        command.parameters[L"value"] = L"1";
        command.parameters[L"target"] = L"start";
        break;
    default:
        break;
    }

    return command;
}

void NovelRuntime::InsertCommandAtIndex(ScriptCommand::Type type, size_t insertIndex)
{
    PushUndoSnapshot();
    ScriptCommand command = CreateDefaultCommand(type);
    const size_t clampedIndex = (std::min)(insertIndex, scenario_.commands.size());
    scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(clampedIndex), std::move(command));
    selectedCommandIndex_ = clampedIndex;
    SyncDocumentMetadata();
    statusText_ = GetCommandTypeLabel(scenario_.commands[selectedCommandIndex_]) + L"\u3092\u8ffd\u52a0\u3057\u307e\u3057\u305f";
}

void NovelRuntime::InsertCommandAfterSelection(ScriptCommand::Type type)
{
    const size_t insertIndex = scenario_.commands.empty() ? 0 : (std::min)(selectedCommandIndex_ + 1, scenario_.commands.size());
    InsertCommandAtIndex(type, insertIndex);
}

void NovelRuntime::InsertChoiceTemplateAfterSelection()
{
    PushUndoSnapshot();
    const size_t insertIndex = scenario_.commands.empty() ? 0 : (std::min)(selectedCommandIndex_ + 1, scenario_.commands.size());
    const size_t clampedIndex = (std::min)(insertIndex, scenario_.commands.size());
    const std::wstring base = L"choice_" + std::to_wstring(scenario_.commands.size() + 1);
    const std::wstring firstLabelName = MakeUniqueLabelName(base + L"_1");
    const std::wstring secondLabelName = MakeUniqueLabelName(base + L"_2");
    const std::wstring endLabelName = MakeUniqueLabelName(base + L"_end");

    ScriptCommand choice = CreateDefaultCommand(ScriptCommand::Type::Choice);
    choice.parameters[L"prompt"] = L"どちらを選びますか？";
    choice.links.clear();
    choice.links.push_back({ L"選択肢1", firstLabelName });
    choice.links.push_back({ L"選択肢2", secondLabelName });

    ScriptCommand firstLabel = CreateDefaultCommand(ScriptCommand::Type::Label);
    firstLabel.parameters[L"name"] = firstLabelName;
    ScriptCommand firstText = CreateDefaultCommand(ScriptCommand::Type::Text);
    firstText.parameters[L"value"] = L"選択肢1の本文";
    ScriptCommand firstJump = CreateDefaultCommand(ScriptCommand::Type::Jump);
    firstJump.parameters[L"target"] = endLabelName;

    ScriptCommand secondLabel = CreateDefaultCommand(ScriptCommand::Type::Label);
    secondLabel.parameters[L"name"] = secondLabelName;
    ScriptCommand secondText = CreateDefaultCommand(ScriptCommand::Type::Text);
    secondText.parameters[L"value"] = L"選択肢2の本文";
    ScriptCommand endLabel = CreateDefaultCommand(ScriptCommand::Type::Label);
    endLabel.parameters[L"name"] = endLabelName;

    std::vector<ScriptCommand> commands;
    commands.push_back(std::move(choice));
    commands.push_back(std::move(firstLabel));
    commands.push_back(std::move(firstText));
    commands.push_back(std::move(firstJump));
    commands.push_back(std::move(secondLabel));
    commands.push_back(std::move(secondText));
    commands.push_back(std::move(endLabel));

    scenario_.commands.insert(
        scenario_.commands.begin() + static_cast<std::ptrdiff_t>(clampedIndex),
        std::make_move_iterator(commands.begin()),
        std::make_move_iterator(commands.end()));
    selectedCommandIndex_ = clampedIndex;
    selectedChoiceLinkIndex_ = 0;
    expandedTextCommandIndex_ = static_cast<size_t>(-1);
    SyncDocumentMetadata();
    statusText_ = L"分岐テンプレートを追加しました";
}

bool NovelRuntime::CopySelectedCommand()
{
    const ScriptCommand* selected = GetSelectedCommand();
    if (!selected)
    {
        return false;
    }

    copiedCommand_ = *selected;
    hasCopiedCommand_ = true;
    statusText_ = L"イベントをコピーしました";
    return true;
}

bool NovelRuntime::CutSelectedCommand()
{
    if (!CopySelectedCommand())
    {
        return false;
    }
    DeleteSelectedCommand();
    statusText_ = L"イベントを切り取りました";
    return true;
}

bool NovelRuntime::PasteCopiedCommand()
{
    if (!hasCopiedCommand_)
    {
        statusText_ = L"貼り付けるイベントがありません";
        return false;
    }

    PushUndoSnapshot();
    ScriptCommand copy = copiedCommand_;
    copy.id += L"_paste";
    const size_t insertIndex = scenario_.commands.empty() ? 0 : (std::min)(selectedCommandIndex_ + 1, scenario_.commands.size());
    scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(copy));
    selectedCommandIndex_ = insertIndex;
    SyncDocumentMetadata();
    statusText_ = L"イベントを貼り付けました";
    return true;
}

void NovelRuntime::DeleteSelectedCommand()
{
    if (scenario_.commands.empty() || selectedCommandIndex_ >= scenario_.commands.size())
    {
        return;
    }

    PushUndoSnapshot();
    scenario_.commands.erase(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(selectedCommandIndex_));
    if (selectedCommandIndex_ >= scenario_.commands.size() && !scenario_.commands.empty())
    {
        selectedCommandIndex_ = scenario_.commands.size() - 1;
    }
    if (scenario_.commands.empty())
    {
        selectedCommandIndex_ = 0;
    }
    SyncDocumentMetadata();
    statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u3092\u524a\u9664\u3057\u307e\u3057\u305f";
}

void NovelRuntime::DuplicateSelectedCommand()
{
    const ScriptCommand* selected = GetSelectedCommand();
    if (!selected)
    {
        return;
    }

    PushUndoSnapshot();
    ScriptCommand copy = *selected;
    copy.id += L"_copy";
    const size_t insertIndex = selectedCommandIndex_ + 1;
    scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(copy));
    selectedCommandIndex_ = insertIndex;
    SyncDocumentMetadata();
    statusText_ = L"\u30a4\u30d9\u30f3\u30c8\u3092\u8907\u88fd\u3057\u307e\u3057\u305f";
}

void NovelRuntime::MoveSelectedCommand(int delta)
{
    if (scenario_.commands.empty() || selectedCommandIndex_ >= scenario_.commands.size() || delta == 0)
    {
        return;
    }

    const int source = static_cast<int>(selectedCommandIndex_);
    const int target = source + delta;
    if (target < 0 || target >= static_cast<int>(scenario_.commands.size()))
    {
        return;
    }

    PushUndoSnapshot();
    std::iter_swap(
        scenario_.commands.begin() + static_cast<std::ptrdiff_t>(source),
        scenario_.commands.begin() + static_cast<std::ptrdiff_t>(target));
    selectedCommandIndex_ = static_cast<size_t>(target);
    if (expandedTextCommandIndex_ == static_cast<size_t>(source))
    {
        expandedTextCommandIndex_ = static_cast<size_t>(target);
    }
    else if (expandedTextCommandIndex_ == static_cast<size_t>(target))
    {
        expandedTextCommandIndex_ = static_cast<size_t>(source);
    }
    SyncDocumentMetadata();
    statusText_ = delta < 0 ? L"イベントを上へ移動しました" : L"イベントを下へ移動しました";
}

void NovelRuntime::ToggleSelectedCommandEnabled()
{
    ScriptCommand* selected = GetSelectedCommand();
    if (!selected)
    {
        return;
    }

    PushUndoSnapshot();
    const bool disabled = ParseBoolValue(GetCommandParameter(*selected, L"disabled"), false);
    selected->parameters[L"disabled"] = disabled ? L"false" : L"true";
    SyncDocumentMetadata();
    statusText_ = disabled ? L"イベントを有効化しました" : L"イベントを無効化しました";
}

bool NovelRuntime::ExecuteEditorCommand(UINT commandId)
{
    switch (commandId)
    {
    case IDM_PROJECT_NEW:
        ShowProjectDialog();
        return true;
    case IDM_PROJECT_OPEN:
        return LoadProjectFromDialog();
    case IDM_EDIT_RELOAD:
        if (!ConfirmDiscardUnsavedChanges())
        {
            return true;
        }
        LoadScenario(scenarioPath_);
        statusText_ = L"シナリオを再読み込みしました";
        return true;
    case IDM_EDIT_UNDO:
        Undo();
        return true;
    case IDM_EDIT_REDO:
        Redo();
        return true;
    case IDM_EDIT_CUT:
        return CutSelectedCommand();
    case IDM_EDIT_COPY:
        return CopySelectedCommand();
    case IDM_EDIT_PASTE:
        return PasteCopiedCommand();
    case IDM_EDIT_SELECT_ALL:
        if (GetFocus() == inspectorEdit_ && inspectorEdit_)
        {
            SendMessageW(inspectorEdit_, EM_SETSEL, 0, -1);
            return true;
        }
        if (GetFocus() == eventTextEdit_ && eventTextEdit_)
        {
            SendMessageW(eventTextEdit_, EM_SETSEL, 0, -1);
            return true;
        }
        if (GetFocus() == eventSearchEdit_ && eventSearchEdit_)
        {
            SendMessageW(eventSearchEdit_, EM_SETSEL, 0, -1);
            return true;
        }
        if (GetFocus() == sceneNameEdit_ && sceneNameEdit_)
        {
            SendMessageW(sceneNameEdit_, EM_SETSEL, 0, -1);
            return true;
        }
        if (GetFocus() == characterFieldEdit_ && characterFieldEdit_)
        {
            SendMessageW(characterFieldEdit_, EM_SETSEL, 0, -1);
            return true;
        }
        statusText_ = L"選択できる入力欄がありません";
        return false;
    case IDM_EVENT_ADD_TEXT:
        InsertCommandAfterSelection(ScriptCommand::Type::Text);
        return true;
    case IDM_EVENT_DUPLICATE:
        DuplicateSelectedCommand();
        return true;
    case IDM_EVENT_DELETE:
        DeleteSelectedCommand();
        return true;
    case IDM_EVENT_TOGGLE_ENABLE:
        ToggleSelectedCommandEnabled();
        return true;
    case IDM_EVENT_MOVE_UP:
        MoveSelectedCommand(-1);
        return true;
    case IDM_EVENT_MOVE_DOWN:
        MoveSelectedCommand(1);
        return true;
    case IDM_SCENE_RENAME:
        return RenameCurrentScene();
    case IDM_SCENE_DUPLICATE:
        return DuplicateCurrentScene();
    case IDM_SCENE_DELETE:
        return DeleteCurrentScene();
    default:
        return false;
    }
}

void NovelRuntime::BeginInspectorEdit(size_t commandIndex, const std::wstring& key, const std::wstring& label, const std::wstring& initialValue)
{
    editingCommandIndex_ = commandIndex;
    editingKey_ = key;
    editingLabel_ = label;
    editingBuffer_ = initialValue;
    inspectorEditing_ = true;
    statusText_ = label + L"\u0020\u3092\u7de8\u96c6\u4e2d\u3067\u3059";
}

void NovelRuntime::CommitInspectorEdit()
{
    if (!inspectorEditing_)
    {
        return;
    }

    if (inspectorEdit_)
    {
        const int length = GetWindowTextLengthW(inspectorEdit_);
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(inspectorEdit_, &value[0], length + 1);
        value.resize(length);
        editingBuffer_ = value;
    }

    if (editingCommandIndex_ == static_cast<size_t>(-1))
    {
        const std::wstring variableName = editingKey_;
        if (!variableName.empty())
        {
            variables_[variableName] = editingBuffer_;
            PushVariableHistory(L"EDIT " + variableName + L" = " + editingBuffer_);
            statusText_ = variableName + L" を更新しました";
            RefreshPreviewIfActive();
        }
        inspectorEditing_ = false;
        editingLabel_.clear();
        editingBuffer_.clear();
        editingKey_.clear();
        return;
    }

    if (editingCommandIndex_ >= scenario_.commands.size())
    {
        return;
    }

    PushUndoSnapshot();
    ScriptCommand& command = scenario_.commands[editingCommandIndex_];
    if (StartsWithText(editingKey_, L"__choice_text_"))
    {
        const size_t linkIndex = static_cast<size_t>(_wtoi(editingKey_.substr(14).c_str()));
        if (linkIndex < command.links.size())
        {
            command.links[linkIndex].first = editingBuffer_;
        }
    }
    else if (StartsWithText(editingKey_, L"__choice_target_"))
    {
        const size_t linkIndex = static_cast<size_t>(_wtoi(editingKey_.substr(16).c_str()));
        if (linkIndex < command.links.size())
        {
            command.links[linkIndex].second = editingBuffer_;
        }
    }
    else if (StartsWithText(editingKey_, L"__choice_cond_name_"))
    {
        command.parameters[editingKey_] = editingBuffer_;
    }
    else if (StartsWithText(editingKey_, L"__choice_cond_value_"))
    {
        command.parameters[editingKey_] = editingBuffer_;
    }
    else
    {
        command.parameters[editingKey_] = editingBuffer_;
    }
    if (command.type == ScriptCommand::Type::Choice && editingKey_ == L"prompt")
    {
        command.parameters[L"prompt"] = editingBuffer_;
    }
    if (command.type == ScriptCommand::Type::Title && editingKey_ == L"name")
    {
        scenario_.title = editingBuffer_;
        storyTitle_ = editingBuffer_;
    }

    SyncDocumentMetadata();
    inspectorEditing_ = false;
    statusText_ = editingLabel_ + L"\u0020\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
    RefreshPreviewIfActive();
}

void NovelRuntime::CancelInspectorEdit()
{
    inspectorEditing_ = false;
    editingKey_.clear();
    editingLabel_.clear();
    editingBuffer_.clear();
    statusText_ = L"\u7de8\u96c6\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
}

bool NovelRuntime::HandleInspectorClick(POINT point)
{
    if (inspectorEditing_)
    {
        if (PtInRect(&inspectorCommitRect_, point))
        {
            CommitInspectorEdit();
            return true;
        }
        if (PtInRect(&inspectorCancelRect_, point))
        {
            CancelInspectorEdit();
            return true;
        }
    }

    for (const InspectorActionTarget& target : inspectorActionTargets_)
    {
        if (!PtInRect(&target.buttonRect, point))
        {
            continue;
        }

        if (StartsWithText(target.action, L"var_edit:"))
        {
            const std::wstring variableName = target.action.substr(9);
            const auto found = variables_.find(variableName);
            BeginInspectorEdit(static_cast<size_t>(-1), variableName, L"変数 " + variableName, found == variables_.end() ? L"" : found->second);
            return true;
        }
        if (StartsWithText(target.action, L"var_nudge:"))
        {
            const size_t firstColon = target.action.find(L':');
            const size_t secondColon = target.action.find(L':', firstColon + 1);
            if (secondColon != std::wstring::npos)
            {
                const std::wstring dir = target.action.substr(firstColon + 1, secondColon - firstColon - 1);
                const std::wstring variableName = target.action.substr(secondColon + 1);
                long long current = 0;
                TryGetNumber(variables_[variableName], current);
                current += (dir == L"-" ? -1 : 1);
                variables_[variableName] = std::to_wstring(current);
                PushVariableHistory(L"EDIT " + variableName + L" = " + variables_[variableName]);
                statusText_ = variableName + L" を更新しました";
                RefreshPreviewIfActive();
                return true;
            }
        }

        if (target.commandIndex >= scenario_.commands.size())
        {
            continue;
        }

        ScriptCommand& command = scenario_.commands[target.commandIndex];
        if (StartsWithText(target.action, L"nudge:"))
        {
            const size_t firstColon = target.action.find(L':');
            const size_t secondColon = target.action.find(L':', firstColon + 1);
            if (secondColon != std::wstring::npos)
            {
                const std::wstring dir = target.action.substr(firstColon + 1, secondColon - firstColon - 1);
                const std::wstring key = target.action.substr(secondColon + 1);
                int step = 1;
                if (key == L"time" || key == L"fadein" || key == L"fadeout") step = 100;
                else if (key == L"x" || key == L"y") step = 10;
                else if (key == L"scale" || key == L"opacity" || key == L"volume") step = 5;
                else if (key == L"power") step = 2;
                const int delta = dir == L"-" ? -step : step;
                PushUndoSnapshot();
                int value = ParseIntValue(GetCommandParameter(command, key), 0) + delta;
                if (key == L"scale") value = (std::max)(10, value);
                if (key == L"opacity" || key == L"volume") value = (std::max)(0, (std::min)(255, value));
                if (key == L"volume") value = (std::max)(0, (std::min)(100, value));
                if (key == L"power") value = (std::max)(0, value);
                if (key == L"time" || key == L"fadein" || key == L"fadeout") value = (std::max)(0, value);
                command.parameters[key] = std::to_wstring(value);
                SyncDocumentMetadata();
                RefreshPreviewIfActive();
                statusText_ = key + L" を更新しました";
                return true;
            }
        }
        if (target.action == L"browse_image")
        {
            return BrowseCommandAsset(target.commandIndex, L"storage", false);
        }
        if (target.action == L"browse_message_image")
        {
            return BrowseCommandAsset(target.commandIndex, L"image", false);
        }
        if (target.action == L"browse_name_image")
        {
            return BrowseCommandAsset(target.commandIndex, L"image", false);
        }
        if (target.action == L"clear_image")
        {
            PushUndoSnapshot();
            command.parameters[L"storage"].clear();
            SyncDocumentMetadata();
            if (command.type == ScriptCommand::Type::Character)
            {
                ApplyCharacterCommand(command);
            }
            else if (command.type == ScriptCommand::Type::Background)
            {
                ApplyBackgroundCommand(command);
            }
            statusText_ = L"\u753b\u50cf\u53c2\u7167\u3092\u89e3\u9664\u3057\u307e\u3057\u305f";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"clear_message_image")
        {
            PushUndoSnapshot();
            command.parameters[L"image"].clear();
            SyncDocumentMetadata();
            ApplyMessageStyleCommand(command);
            statusText_ = L"メッセージ画像を解除しました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"clear_name_image")
        {
            PushUndoSnapshot();
            command.parameters[L"image"].clear();
            SyncDocumentMetadata();
            ApplyNameWindowCommand(command);
            statusText_ = L"名前欄画像を解除しました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"browse_audio")
        {
            return BrowseCommandAsset(target.commandIndex, L"storage", true);
        }
        if (target.action == L"cycle_message_font")
        {
            RefreshAvailableFonts();
            const std::wstring currentFont = GetCommandParameter(command, L"face");
            const std::wstring selectedFont = ShowFontSelectionMenu(point, currentFont);
            if (!selectedFont.empty() && selectedFont != currentFont)
            {
                PushUndoSnapshot();
                command.parameters[L"face"] = selectedFont;
                SyncDocumentMetadata();
                ApplyMessageFontCommand(command);
                RefreshPreviewIfActive();
            }
            return true;
        }
        if (target.action == L"toggle_visible")
        {
            PushUndoSnapshot();
            command.parameters[L"visible"] = ParseBoolValue(GetCommandParameter(command, L"visible"), true) ? L"false" : L"true";
            SyncDocumentMetadata();
            statusText_ = L"\u8868\u793a\u72b6\u614b\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"toggle_loop")
        {
            PushUndoSnapshot();
            command.parameters[L"loop"] = ParseBoolValue(GetCommandParameter(command, L"loop"), command.type == ScriptCommand::Type::Bgm) ? L"false" : L"true";
            SyncDocumentMetadata();
            statusText_ = L"\u30eb\u30fc\u30d7\u8a2d\u5b9a\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"fade_cycle_target" && (command.type == ScriptCommand::Type::Fade || command.type == ScriptCommand::Type::Transition))
        {
            PushUndoSnapshot();
            const std::wstring current = GetCommandParameter(command, L"target");
            const std::wstring targets[] =
            {
                L"stage",
                L"background",
                L"character:left",
                L"character:center",
                L"character:right",
                L"message",
                L"all"
            };
            size_t nextIndex = 0;
            for (size_t i = 0; i < _countof(targets); ++i)
            {
                if (current == targets[i])
                {
                    nextIndex = (i + 1) % _countof(targets);
                    break;
                }
            }
            command.parameters[L"target"] = targets[nextIndex];
            SyncDocumentMetadata();
            statusText_ = L"フェード対象を更新しました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"character_adjust")
        {
            characterAdjustMode_ = !characterAdjustMode_;
            characterAdjustDragging_ = false;
            adjustCharacterCommandIndex_ = characterAdjustMode_ ? target.commandIndex : static_cast<size_t>(-1);
            statusText_ = characterAdjustMode_ ? L"立ち位置調整モードを開始しました" : L"立ち位置調整モードを終了しました";
            if (characterAdjustMode_ && (!previewWindow_ || !IsWindowVisible(previewWindow_)))
            {
                TogglePreviewWindow();
            }
            return true;
        }
        if (target.action == L"audio_play")
        {
            if (command.type == ScriptCommand::Type::Bgm)
            {
                ApplyBgmCommand(command);
            }
            else if (command.type == ScriptCommand::Type::Se)
            {
                ApplySeCommand(command);
            }
            else if (command.type == ScriptCommand::Type::Voice)
            {
                ApplyVoiceCommand(command);
            }
            return true;
        }
        if (target.action == L"audio_stop")
        {
            if (command.type == ScriptCommand::Type::Bgm)
            {
                StopBgmPlayback();
                statusText_ = L"BGM \u3092\u505c\u6b62\u3057\u307e\u3057\u305f";
            }
            else if (command.type == ScriptCommand::Type::Se)
            {
                StopAudioChannel(AudioChannel::Se);
                statusText_ = L"SE \u3092\u505c\u6b62\u3057\u307e\u3057\u305f";
            }
            else if (command.type == ScriptCommand::Type::Voice)
            {
                StopAudioChannel(AudioChannel::Voice);
                statusText_ = L"\u30dc\u30a4\u30b9\u3092\u505c\u6b62\u3057\u307e\u3057\u305f";
            }
            return true;
        }
        if (target.action == L"set_character_name")
        {
            if (target.linkIndex < characterDefinitions_.size())
            {
                PushUndoSnapshot();
                const CharacterDefinition& definition = characterDefinitions_[target.linkIndex];
                command.parameters[L"name"] = definition.id;
                command.parameters[L"face"].clear();
                if (!definition.baseImagePath.empty())
                {
                    command.parameters[L"storage"] = definition.baseImagePath;
                }
                SyncDocumentMetadata();
                statusText_ = command.parameters[L"name"] + L" を設定しました";
                RefreshPreviewIfActive();
            }
            return true;
        }
        if (target.action == L"set_character_base")
        {
            const CharacterDefinition* definition = FindCharacterDefinition(GetCommandParameter(command, L"name"));
            if (definition)
            {
                PushUndoSnapshot();
                command.parameters[L"face"].clear();
                command.parameters[L"storage"] = definition->baseImagePath;
                SyncDocumentMetadata();
                statusText_ = L"基準画像に切り替えました";
                RefreshPreviewIfActive();
            }
            return true;
        }
        if (target.action == L"set_character_expression")
        {
            const CharacterDefinition* definition = FindCharacterDefinition(GetCommandParameter(command, L"name"));
            if (definition && target.linkIndex < definition->expressions.size())
            {
                PushUndoSnapshot();
                const CharacterExpressionDefinition& expression = definition->expressions[target.linkIndex];
                command.parameters[L"face"] = expression.name;
                command.parameters[L"storage"] = expression.imagePath;
                SyncDocumentMetadata();
                statusText_ = L"表情差分を更新しました";
                RefreshPreviewIfActive();
            }
            return true;
        }
        if (target.action == L"choice_add" && command.type == ScriptCommand::Type::Choice)
        {
            PushUndoSnapshot();
            const std::wstring branchLabelName = MakeUniqueLabelName(L"choice_branch_" + std::to_wstring(command.links.size() + 1));
            const size_t newLinkIndex = command.links.size();
            std::wstring mergeLabelName;
            size_t mergeInsertIndex = scenario_.commands.size();

            for (const auto& link : command.links)
            {
                const auto labelIt = scenario_.labels.find(link.second);
                if (labelIt == scenario_.labels.end())
                {
                    continue;
                }

                for (size_t scanIndex = labelIt->second + 1; scanIndex < scenario_.commands.size(); ++scanIndex)
                {
                    const ScriptCommand& scanned = scenario_.commands[scanIndex];
                    if (scanned.type == ScriptCommand::Type::Label)
                    {
                        break;
                    }
                    if (scanned.type == ScriptCommand::Type::Jump)
                    {
                        const std::wstring jumpTarget = GetCommandParameter(scanned, L"target");
                        if (!jumpTarget.empty() && scenario_.labels.find(jumpTarget) != scenario_.labels.end())
                        {
                            mergeLabelName = jumpTarget;
                            mergeInsertIndex = scenario_.labels.at(jumpTarget);
                            break;
                        }
                    }
                }
                if (!mergeLabelName.empty())
                {
                    break;
                }
            }

            std::vector<ScriptCommand> branchCommands;
            ScriptCommand branchLabel = CreateDefaultCommand(ScriptCommand::Type::Label);
            branchLabel.parameters[L"name"] = branchLabelName;
            ScriptCommand branchText = CreateDefaultCommand(ScriptCommand::Type::Text);
            branchText.parameters[L"value"] = L"新しい選択肢の本文";
            branchCommands.push_back(std::move(branchLabel));
            branchCommands.push_back(std::move(branchText));

            if (!mergeLabelName.empty())
            {
                ScriptCommand branchJump = CreateDefaultCommand(ScriptCommand::Type::Jump);
                branchJump.parameters[L"target"] = mergeLabelName;
                branchCommands.push_back(std::move(branchJump));
            }

            mergeInsertIndex = (std::min)(mergeInsertIndex, scenario_.commands.size());
            command.links.push_back({ L"新しい選択肢", branchLabelName });
            selectedChoiceLinkIndex_ = newLinkIndex;
            const size_t insertedCount = branchCommands.size();
            scenario_.commands.insert(
                scenario_.commands.begin() + static_cast<std::ptrdiff_t>(mergeInsertIndex),
                std::make_move_iterator(branchCommands.begin()),
                std::make_move_iterator(branchCommands.end()));
            if (mergeInsertIndex <= selectedCommandIndex_)
            {
                selectedCommandIndex_ += insertedCount;
            }
            SyncDocumentMetadata();
            statusText_ = mergeLabelName.empty() ? L"選択肢の枝を追加しました" : L"選択肢の枝を合流付きで追加しました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"choice_remove" && command.type == ScriptCommand::Type::Choice && command.links.size() > 1 && target.linkIndex < command.links.size())
        {
            PushUndoSnapshot();
            command.links.erase(command.links.begin() + static_cast<std::ptrdiff_t>(target.linkIndex));
            for (size_t i = target.linkIndex; i <= command.links.size(); ++i)
            {
                const size_t next = i + 1;
                const std::wstring keys[] =
                {
                    GetChoiceParamKey(L"__choice_cond_name_", i),
                    GetChoiceParamKey(L"__choice_cond_op_", i),
                    GetChoiceParamKey(L"__choice_cond_value_", i),
                    GetChoiceParamKey(L"__choice_show_disabled_", i),
                };
                const std::wstring nextKeys[] =
                {
                    GetChoiceParamKey(L"__choice_cond_name_", next),
                    GetChoiceParamKey(L"__choice_cond_op_", next),
                    GetChoiceParamKey(L"__choice_cond_value_", next),
                    GetChoiceParamKey(L"__choice_show_disabled_", next),
                };
                for (size_t keyIndex = 0; keyIndex < _countof(keys); ++keyIndex)
                {
                    const auto found = command.parameters.find(nextKeys[keyIndex]);
                    if (found != command.parameters.end())
                    {
                        command.parameters[keys[keyIndex]] = found->second;
                    }
                    else
                    {
                        command.parameters.erase(keys[keyIndex]);
                    }
                }
            }
            if (selectedChoiceLinkIndex_ >= command.links.size())
            {
                selectedChoiceLinkIndex_ = command.links.empty() ? 0 : command.links.size() - 1;
            }
            SyncDocumentMetadata();
            statusText_ = L"選択肢の枝を削除しました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"choice_cycle_op" && command.type == ScriptCommand::Type::Choice && target.linkIndex < command.links.size())
        {
            PushUndoSnapshot();
            const std::wstring key = GetChoiceParamKey(L"__choice_cond_op_", target.linkIndex);
            command.parameters[key] = NextIfOperator(GetCommandParameter(command, key));
            SyncDocumentMetadata();
            statusText_ = L"選択肢条件の演算子を切り替えました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"choice_toggle_disabled" && command.type == ScriptCommand::Type::Choice && target.linkIndex < command.links.size())
        {
            PushUndoSnapshot();
            const std::wstring key = GetChoiceParamKey(L"__choice_show_disabled_", target.linkIndex);
            const bool current = ParseBoolValue(GetCommandParameter(command, key), false);
            command.parameters[key] = current ? L"false" : L"true";
            SyncDocumentMetadata();
            statusText_ = current ? L"条件未達時は非表示にします" : L"条件未達時も表示します";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"choice_cycle_var" && command.type == ScriptCommand::Type::Choice && target.linkIndex < command.links.size())
        {
            SyncVariableDefinitions();
            if (!variableDefinitions_.empty())
            {
                const std::wstring key = GetChoiceParamKey(L"__choice_cond_name_", target.linkIndex);
                const std::wstring selectedName = ShowVariableSelectionMenu(point, GetCommandParameter(command, key));
                if (selectedName != GetCommandParameter(command, key))
                {
                    PushUndoSnapshot();
                    command.parameters[key] = selectedName;
                    SyncDocumentMetadata();
                    statusText_ = L"選択肢条件の変数を更新しました";
                    RefreshPreviewIfActive();
                }
            }
            return true;
        }
        if (target.action == L"if_cycle_op" && command.type == ScriptCommand::Type::IfJump)
        {
            PushUndoSnapshot();
            command.parameters[L"op"] = NextIfOperator(GetCommandParameter(command, L"op"));
            SyncDocumentMetadata();
            statusText_ = L"条件分岐の演算子を切り替えました";
            RefreshPreviewIfActive();
            return true;
        }
        if (target.action == L"if_cycle_var" && command.type == ScriptCommand::Type::IfJump)
        {
            SyncVariableDefinitions();
            if (!variableDefinitions_.empty())
            {
                const std::wstring selectedName = ShowVariableSelectionMenu(point, GetCommandParameter(command, L"name"));
                if (selectedName != GetCommandParameter(command, L"name"))
                {
                    PushUndoSnapshot();
                    command.parameters[L"name"] = selectedName;
                    SyncDocumentMetadata();
                    statusText_ = L"条件分岐の変数を更新しました";
                    RefreshPreviewIfActive();
                }
            }
            return true;
        }
    }

    for (const InspectorEditTarget& target : inspectorEditTargets_)
    {
        if (PtInRect(&target.buttonRect, point) && target.commandIndex < scenario_.commands.size())
        {
            const ScriptCommand& command = scenario_.commands[target.commandIndex];
            std::wstring initialValue;
            if (StartsWithText(target.key, L"__choice_text_"))
            {
                const size_t linkIndex = static_cast<size_t>(_wtoi(target.key.substr(14).c_str()));
                if (linkIndex < command.links.size())
                {
                    initialValue = command.links[linkIndex].first;
                }
            }
            else if (StartsWithText(target.key, L"__choice_target_"))
            {
                const size_t linkIndex = static_cast<size_t>(_wtoi(target.key.substr(16).c_str()));
                if (linkIndex < command.links.size())
                {
                    initialValue = command.links[linkIndex].second;
                }
            }
            else if (StartsWithText(target.key, L"__choice_cond_name_") || StartsWithText(target.key, L"__choice_cond_value_"))
            {
                initialValue = GetCommandParameter(command, target.key);
            }
            else
            {
                const auto found = command.parameters.find(target.key);
                initialValue = found == command.parameters.end() ? L"" : found->second;
            }
            BeginInspectorEdit(target.commandIndex, target.key, target.label, initialValue);
            return true;
        }
    }
    return false;
}

bool NovelRuntime::BrowseCommandAsset(size_t commandIndex, const std::wstring& key, bool audio)
{
    if (commandIndex >= scenario_.commands.size())
    {
        return false;
    }

    WCHAR fileBuffer[MAX_PATH] = {};
    const ScriptCommand& command = scenario_.commands[commandIndex];
    const std::wstring currentValue = GetCommandParameter(command, key);
    if (!currentValue.empty())
    {
        wcsncpy_s(fileBuffer, currentValue.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = audio
        ? L"\u97f3\u58f0\u30d5\u30a1\u30a4\u30eb (*.mp3;*.wav;*.ogg)\0*.mp3;*.wav;*.ogg\0\u3059\u3079\u3066\u306e\u30d5\u30a1\u30a4\u30eb (*.*)\0*.*\0"
        : L"\u753b\u50cf\u30d5\u30a1\u30a4\u30eb (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0\u3059\u3079\u3066\u306e\u30d5\u30a1\u30a4\u30eb (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn))
    {
        statusText_ = audio ? L"\u97f3\u58f0\u9078\u629e\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f" : L"\u753b\u50cf\u9078\u629e\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    PushUndoSnapshot();
    ScriptCommand& mutableCommand = scenario_.commands[commandIndex];
    mutableCommand.parameters[key] = MakeRelativePath(fileBuffer, scenarioBaseDir_);
    if (!audio)
    {
        if (mutableCommand.type == ScriptCommand::Type::Background && GetCommandParameter(mutableCommand, L"name").empty())
        {
            mutableCommand.parameters[L"name"] = GetFileStemPart(fileBuffer);
        }
        if (mutableCommand.type == ScriptCommand::Type::Character && GetCommandParameter(mutableCommand, L"name").empty())
        {
            mutableCommand.parameters[L"name"] = GetFileStemPart(fileBuffer);
        }
    }
    SyncDocumentMetadata();
    statusText_ = audio ? L"\u97f3\u58f0\u30d5\u30a1\u30a4\u30eb\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f" : L"\u753b\u50cf\u30d5\u30a1\u30a4\u30eb\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f";
    RefreshPreviewIfActive();
    return true;
}

bool NovelRuntime::HandleEventActionClick(POINT point)
{
    if (PtInRect(&eventAddTextRect_, point))
    {
        InsertCommandAfterSelection(ScriptCommand::Type::Text);
        expandedTextCommandIndex_ = selectedCommandIndex_;
        return true;
    }
    if (PtInRect(&eventAddChoiceRect_, point))
    {
        InsertChoiceTemplateAfterSelection();
        return true;
    }
    if (PtInRect(&eventValidateRect_, point))
    {
        return SelectFirstScenarioIssue();
    }
    if (PtInRect(&eventDeleteRect_, point))
    {
        DeleteSelectedCommand();
        return true;
    }
    if (PtInRect(&eventDuplicateRect_, point))
    {
        DuplicateSelectedCommand();
        return true;
    }
    return false;
}

bool NovelRuntime::HandleChar(wchar_t ch)
{
    UNREFERENCED_PARAMETER(ch);
    return false;
}

bool NovelRuntime::HandlePreviewClick(POINT point)
{
    if (characterAdjustMode_)
    {
        UNREFERENCED_PARAMETER(point);
        return false;
    }

    if (textRevealActive_)
    {
        displayedText_ = currentText_;
        textRevealIndex_ = currentText_.size();
        textRevealActive_ = false;
        nextTextRevealTick_ = 0;
        return true;
    }

    if (previewLogVisible_)
    {
        if (PtInRect(&previewLogCloseRect_, point) || !PtInRect(&previewMenuPanelRect_, point))
        {
            previewLogVisible_ = false;
            return true;
        }
        return true;
    }

    if (previewMenuVisible_)
    {
        if (PtInRect(&previewMenuSaveRect_, point))
        {
            previewMenuVisible_ = false;
            return SaveRuntimeStateAs();
        }
        if (PtInRect(&previewMenuLoadRect_, point))
        {
            previewMenuVisible_ = false;
            return LoadRuntimeStateFromDialog();
        }
        if (PtInRect(&previewMenuLogRect_, point))
        {
            previewLogVisible_ = true;
            previewMenuVisible_ = false;
            return true;
        }
        if (PtInRect(&previewMenuSkipRect_, point))
        {
            ToggleSkipMode();
            previewMenuVisible_ = false;
            return true;
        }
        if (PtInRect(&previewMenuAutoRect_, point))
        {
            ToggleAutoMode();
            previewMenuVisible_ = false;
            return true;
        }
        if (PtInRect(&previewMenuConfigRect_, point))
        {
            previewMenuVisible_ = false;
            ShowSettingsDialog();
            return true;
        }
        if (PtInRect(&previewMenuFullscreenRect_, point))
        {
            previewMenuVisible_ = false;
            TogglePreviewFullscreen();
            return true;
        }
        if (PtInRect(&previewMenuTitleRect_, point))
        {
            previewMenuVisible_ = false;
            StartPreviewFromIndex(0);
            return true;
        }
        if (PtInRect(&previewMenuCloseRect_, point) || !PtInRect(&previewMenuPanelRect_, point))
        {
            previewMenuVisible_ = false;
            return true;
        }
        return true;
    }

    for (size_t i = 0; i < previewUiButtonRects_.size() && i < uiButtons_.size(); ++i)
    {
        if (!uiButtons_[i].visible || !PtInRect(&previewUiButtonRects_[i], point))
        {
            continue;
        }

        const std::wstring& buttonId = uiButtons_[i].id;
        if (buttonId == L"save")
        {
            return SaveRuntimeStateToPath(GetQuickSavePath());
        }
        if (buttonId == L"load")
        {
            return LoadRuntimeStateFromPath(GetQuickSavePath());
        }
        if (buttonId == L"log")
        {
            previewLogVisible_ = true;
            previewMenuVisible_ = false;
            return true;
        }
        if (buttonId == L"hide")
        {
            messageWindowVisible_ = !messageWindowVisible_;
            statusText_ = messageWindowVisible_ ? L"メッセージウィンドウを表示しました" : L"メッセージウィンドウを隠しました";
            return true;
        }
        if (buttonId == L"menu")
        {
            previewMenuVisible_ = true;
            previewLogVisible_ = false;
            return true;
        }
    }

    if (PtInRect(&previewMenuButtonRect_, point))
    {
        previewMenuVisible_ = true;
        return true;
    }

    if (waitingForChoice_)
    {
        for (size_t index = 0; index < activeChoiceRects_.size(); ++index)
        {
            if (PtInRect(&activeChoiceRects_[index], point))
            {
                SelectChoice(index);
                return true;
            }
        }
        return false;
    }

    if (!reachedEnd_)
    {
        Advance();
        return true;
    }

    return false;
}

bool NovelRuntime::HandlePreviewMouseDown(POINT point)
{
    if (!characterAdjustMode_ || adjustCharacterCommandIndex_ >= scenario_.commands.size())
    {
        return false;
    }

    ScriptCommand& command = scenario_.commands[adjustCharacterCommandIndex_];
    if (command.type != ScriptCommand::Type::Character)
    {
        return false;
    }

    characterAdjustDragging_ = true;
    adjustDragStartPoint_ = point;
    adjustStartX_ = ParseIntValue(GetCommandParameter(command, L"x"), 0);
    adjustStartY_ = ParseIntValue(GetCommandParameter(command, L"y"), 0);
    return true;
}

bool NovelRuntime::HandlePreviewMouseMove(POINT point)
{
    int newHoveredPreviewButton = -1;
    for (size_t i = 0; i < previewUiButtonRects_.size() && i < uiButtons_.size(); ++i)
    {
        if (uiButtons_[i].visible && PtInRect(&previewUiButtonRects_[i], point))
        {
            newHoveredPreviewButton = static_cast<int>(i);
            break;
        }
    }
    if (newHoveredPreviewButton != hoveredPreviewUiButtonIndex_)
    {
        hoveredPreviewUiButtonIndex_ = newHoveredPreviewButton;
        if (!characterAdjustMode_ || !characterAdjustDragging_)
        {
            return true;
        }
    }

    if (!characterAdjustMode_ || !characterAdjustDragging_ || adjustCharacterCommandIndex_ >= scenario_.commands.size())
    {
        return false;
    }

    ScriptCommand& command = scenario_.commands[adjustCharacterCommandIndex_];
    if (command.type != ScriptCommand::Type::Character)
    {
        return false;
    }

    const int deltaX = point.x - adjustDragStartPoint_.x;
    const int deltaY = point.y - adjustDragStartPoint_.y;
    command.parameters[L"x"] = std::to_wstring(adjustStartX_ + deltaX);
    command.parameters[L"y"] = std::to_wstring(adjustStartY_ + deltaY);
    ApplyCharacterCommand(command);
    statusText_ = L"横位置: " + command.parameters[L"x"] + L" / 縦位置: " + command.parameters[L"y"];
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, FALSE);
    }
    return true;
}

bool NovelRuntime::HandlePreviewMouseUp(POINT point)
{
    UNREFERENCED_PARAMETER(point);
    if (!characterAdjustDragging_)
    {
        return false;
    }

    characterAdjustDragging_ = false;
    return true;
}

bool NovelRuntime::HandleTimer()
{
    const DWORD now = GetTickCount();
    bool needsRedraw = false;

    if (textRevealActive_ && now >= nextTextRevealTick_)
    {
        if (textRevealIndex_ < currentText_.size())
        {
            ++textRevealIndex_;
            displayedText_ = currentText_.substr(0, textRevealIndex_);
            nextTextRevealTick_ = now + static_cast<DWORD>((std::max)(0, textSpeedMs_));
            needsRedraw = true;
        }

        if (textRevealIndex_ >= currentText_.size())
        {
            displayedText_ = currentText_;
            textRevealActive_ = false;
            nextTextRevealTick_ = 0;
            needsRedraw = true;
        }
    }

    if (waitUntilTick_ != 0 && now >= waitUntilTick_)
    {
        waitUntilTick_ = 0;
        if (!waitingForChoice_ && !reachedEnd_)
        {
            Advance();
        }
        needsRedraw = true;
    }

    if ((playerMode_ || previewVisible_) && !previewMenuVisible_ && !previewLogVisible_ && !settingsDialogVisible_)
    {
        if (previewSkipMode_)
        {
            if (waitingForChoice_)
            {
                previewSkipMode_ = false;
                statusText_ = L"選択肢でスキップを停止しました";
            }
            else if (textRevealActive_)
            {
                displayedText_ = currentText_;
                textRevealIndex_ = currentText_.size();
                textRevealActive_ = false;
                nextTextRevealTick_ = 0;
                needsRedraw = true;
            }
            else if (!reachedEnd_ && waitUntilTick_ == 0)
            {
                Advance();
                needsRedraw = true;
            }
        }

        if (previewAutoMode_)
        {
            if (waitingForChoice_)
            {
                autoAdvanceTick_ = 0;
            }
            else if (textRevealActive_ || waitUntilTick_ != 0 || reachedEnd_)
            {
                autoAdvanceTick_ = 0;
            }
            else
            {
                if (autoAdvanceTick_ == 0)
                {
                    autoAdvanceTick_ = now + 900;
                }
                else if (now >= autoAdvanceTick_)
                {
                    autoAdvanceTick_ = 0;
                    Advance();
                    needsRedraw = true;
                }
            }
        }
        else
        {
            autoAdvanceTick_ = 0;
        }
    }
    else if (!previewAutoMode_)
    {
        autoAdvanceTick_ = 0;
    }

    if (fadeEndTick_ != 0)
    {
        needsRedraw = true;
        if (now >= fadeEndTick_)
        {
            fadeStartTick_ = 0;
            fadeEndTick_ = 0;
        }
    }

    if (!toastText_.empty() && toastStartTick_ != 0)
    {
        if (now - toastStartTick_ >= toastDurationMs_)
        {
            toastText_.clear();
            toastStartTick_ = 0;
        }
        needsRedraw = true;
    }

    if (shakeEndTick_ != 0)
    {
        needsRedraw = true;
        if (now >= shakeEndTick_)
        {
            shakeEndTick_ = 0;
            shakePower_ = 0;
        }
    }

    if (zoomEndTick_ != 0)
    {
        needsRedraw = true;
        if (now >= zoomEndTick_)
        {
            stageScale_ = zoomTargetScale_;
            zoomStartTick_ = 0;
            zoomEndTick_ = 0;
        }
        else
        {
            const double progress = static_cast<double>(now - zoomStartTick_) / static_cast<double>((std::max<DWORD>)(1, zoomEndTick_ - zoomStartTick_));
            stageScale_ = zoomStartScale_ + static_cast<int>((zoomTargetScale_ - zoomStartScale_) * progress);
        }
    }

    if (panEndTick_ != 0)
    {
        needsRedraw = true;
        if (now >= panEndTick_)
        {
            stageOffsetX_ = panTargetX_;
            stageOffsetY_ = panTargetY_;
            panStartTick_ = 0;
            panEndTick_ = 0;
        }
        else
        {
            const double progress = static_cast<double>(now - panStartTick_) / static_cast<double>((std::max<DWORD>)(1, panEndTick_ - panStartTick_));
            stageOffsetX_ = panStartX_ + static_cast<int>((panTargetX_ - panStartX_) * progress);
            stageOffsetY_ = panStartY_ + static_cast<int>((panTargetY_ - panStartY_) * progress);
        }
    }

    if (flashEndTick_ != 0)
    {
        needsRedraw = true;
        if (now >= flashEndTick_)
        {
            flashStartTick_ = 0;
            flashEndTick_ = 0;
        }
    }

    if (needsRedraw)
    {
        RefreshPreviewWindow();
    }
    return needsRedraw;
}

bool NovelRuntime::HandleControlCommand(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    const UINT controlId = LOWORD(wParam);
    const UINT notifyCode = HIWORD(wParam);

    if (controlId == IDC_EVENT_SEARCH && notifyCode == EN_CHANGE && eventSearchEdit_)
    {
        const int length = GetWindowTextLengthW(eventSearchEdit_);
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(eventSearchEdit_, &value[0], length + 1);
        value.resize(length);
        if (leftPanelTab_ == LeftPanelTab::Materials)
        {
            materialFilterText_ = value;
        }
        else if (leftPanelTab_ == LeftPanelTab::Scenario)
        {
            scenarioFilterText_ = value;
        }
        else
        {
            eventFilterText_ = value;
            eventListScrollOffset_ = 0;
        }
        return true;
    }

    if (controlId == IDC_INSPECTOR_EDIT && notifyCode == EN_CHANGE && inspectorEdit_)
    {
        const int length = GetWindowTextLengthW(inspectorEdit_);
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(inspectorEdit_, &value[0], length + 1);
        value.resize(length);
        editingBuffer_ = value;
        return true;
    }

    if (controlId == IDC_EVENT_TEXT_EDIT && notifyCode == EN_CHANGE && eventTextEdit_)
    {
        if (expandedTextCommandIndex_ < scenario_.commands.size() && scenario_.commands[expandedTextCommandIndex_].type == ScriptCommand::Type::Text)
        {
            const int length = GetWindowTextLengthW(eventTextEdit_);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(eventTextEdit_, &value[0], length + 1);
            value.resize(length);
            scenario_.commands[expandedTextCommandIndex_].parameters[L"value"] = value;
            currentText_ = value;
            SyncDocumentMetadata();
            RefreshPreviewIfActive();
        }
        return true;
    }

    if (controlId == IDC_CHARACTER_FIELD_EDIT && notifyCode == EN_CHANGE && characterFieldEdit_)
    {
        return true;
    }

    if (controlId == IDC_VARIABLE_FIELD_EDIT && notifyCode == EN_CHANGE && variableFieldEdit_)
    {
        return true;
    }

    return false;
}

void NovelRuntime::PushUndoSnapshot()
{
    undoStack_.push_back(EditorSnapshot{ SerializeScenario(scenario_), selectedCommandIndex_ });
    if (undoStack_.size() > 128)
    {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

bool NovelRuntime::RestoreSnapshot(const EditorSnapshot& snapshot)
{
    ScenarioDocument document;
    std::wstring errorMessage;
    if (!ParseScenario(snapshot.scenarioText, document, errorMessage))
    {
        statusText_ = L"undo/redo restore failed";
        return false;
    }

    scenario_ = std::move(document);
    selectedCommandIndex_ = snapshot.selectedCommandIndex < scenario_.commands.size() ? snapshot.selectedCommandIndex : 0;
    SyncDocumentMetadata();
    RefreshPreviewIfActive();
    return true;
}

void NovelRuntime::Undo()
{
    if (undoStack_.empty())
    {
        return;
    }

    redoStack_.push_back(EditorSnapshot{ SerializeScenario(scenario_), selectedCommandIndex_ });
    const EditorSnapshot snapshot = undoStack_.back();
    undoStack_.pop_back();
    if (RestoreSnapshot(snapshot))
    {
        statusText_ = L"undo";
    }
}

void NovelRuntime::Redo()
{
    if (redoStack_.empty())
    {
        return;
    }

    undoStack_.push_back(EditorSnapshot{ SerializeScenario(scenario_), selectedCommandIndex_ });
    const EditorSnapshot snapshot = redoStack_.back();
    redoStack_.pop_back();
    if (RestoreSnapshot(snapshot))
    {
        statusText_ = L"redo";
    }
}

std::vector<size_t> NovelRuntime::BuildFilteredEventIndices() const
{
    std::vector<size_t> indices;
    for (size_t i = 0; i < scenario_.commands.size(); ++i)
    {
        if (MatchesEventFilter(scenario_.commands[i]))
        {
            indices.push_back(i);
        }
    }
    return indices;
}

bool NovelRuntime::MatchesEventFilter(const ScriptCommand& command) const
{
    if (eventFilterText_.empty())
    {
        return true;
    }

    std::wstring haystack = GetCommandTypeLabel(command) + L" " + GetCommandSummary(command);
    for (const auto& parameter : command.parameters)
    {
        haystack += L" " + parameter.first + L" " + parameter.second;
    }
    for (const auto& link : command.links)
    {
        haystack += L" " + link.first + L" " + link.second;
    }

    std::wstring filter = eventFilterText_;
    if (!haystack.empty())
    {
        CharLowerBuffW(&haystack[0], static_cast<DWORD>(haystack.size()));
    }
    if (!filter.empty())
    {
        CharLowerBuffW(&filter[0], static_cast<DWORD>(filter.size()));
    }
    return haystack.find(filter) != std::wstring::npos;
}

void NovelRuntime::UpdateChildControls()
{
    if (playerMode_)
    {
        return;
    }
    EnsureChildControls();

    if (projectLauncherVisible_ || projectDialogVisible_ || sceneDialogVisible_ || characterManagerVisible_ || variableManagerVisible_ || settingsDialogVisible_)
    {
        if (eventSearchEdit_) ShowWindow(eventSearchEdit_, SW_HIDE);
        if (inspectorEdit_) ShowWindow(inspectorEdit_, SW_HIDE);
        if (eventTextEdit_) ShowWindow(eventTextEdit_, SW_HIDE);
    }

    if (eventSearchEdit_ && !projectLauncherVisible_ && !projectDialogVisible_ && !sceneDialogVisible_ && !characterManagerVisible_ && !variableManagerVisible_ && !settingsDialogVisible_)
    {
        if (leftPanelTab_ == LeftPanelTab::Materials && showComponents_)
        {
            SetWindowPos(eventSearchEdit_, nullptr, eventSearchRect_.left, eventSearchRect_.top, eventSearchRect_.right - eventSearchRect_.left, eventSearchRect_.bottom - eventSearchRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            const std::wstring expectedValue = materialFilterText_;
            const int length = GetWindowTextLengthW(eventSearchEdit_);
            std::wstring currentValue(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(eventSearchEdit_, &currentValue[0], length + 1);
            currentValue.resize(length);
            if (currentValue != expectedValue)
            {
                SetWindowTextW(eventSearchEdit_, expectedValue.c_str());
            }
        }
        else if (leftPanelTab_ == LeftPanelTab::Scenario && showComponents_)
        {
            SetWindowPos(eventSearchEdit_, nullptr, eventSearchRect_.left, eventSearchRect_.top, eventSearchRect_.right - eventSearchRect_.left, eventSearchRect_.bottom - eventSearchRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            const std::wstring expectedValue = scenarioFilterText_;
            const int length = GetWindowTextLengthW(eventSearchEdit_);
            std::wstring currentValue(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(eventSearchEdit_, &currentValue[0], length + 1);
            currentValue.resize(length);
            if (currentValue != expectedValue)
            {
                SetWindowTextW(eventSearchEdit_, expectedValue.c_str());
            }
        }
        else if (showEventList_ && HasVisibleArea(currentEventListRect_))
        {
            eventSearchRect_ = { currentEventListRect_.left + 12, currentEventListRect_.top + 8, currentEventListRect_.left + 220, currentEventListRect_.top + 30 };
            SetWindowPos(eventSearchEdit_, nullptr, eventSearchRect_.left, eventSearchRect_.top, eventSearchRect_.right - eventSearchRect_.left, eventSearchRect_.bottom - eventSearchRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            const int length = GetWindowTextLengthW(eventSearchEdit_);
            std::wstring currentValue(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(eventSearchEdit_, &currentValue[0], length + 1);
            currentValue.resize(length);
            if (currentValue != eventFilterText_)
            {
                SetWindowTextW(eventSearchEdit_, eventFilterText_.c_str());
            }
        }
        else
        {
            ShowWindow(eventSearchEdit_, SW_HIDE);
        }
    }

    if (inspectorEdit_ && !projectLauncherVisible_ && !projectDialogVisible_ && !sceneDialogVisible_ && !characterManagerVisible_ && !variableManagerVisible_ && !settingsDialogVisible_)
    {
        if (inspectorEditing_ && HasVisibleArea(currentInspectorRect_))
        {
            inspectorEditRect_ = { currentInspectorRect_.left + 20, inspectorCommitRect_.top - 74, currentInspectorRect_.right - 20, inspectorCommitRect_.top - 38 };
            SetWindowPos(inspectorEdit_, nullptr, inspectorEditRect_.left, inspectorEditRect_.top, inspectorEditRect_.right - inspectorEditRect_.left, inspectorEditRect_.bottom - inspectorEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            const int length = GetWindowTextLengthW(inspectorEdit_);
            std::wstring currentValue(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(inspectorEdit_, &currentValue[0], length + 1);
            currentValue.resize(length);
            if (currentValue != editingBuffer_)
            {
                SetWindowTextW(inspectorEdit_, editingBuffer_.c_str());
                SendMessageW(inspectorEdit_, EM_SETSEL, static_cast<WPARAM>(editingBuffer_.size()), static_cast<LPARAM>(editingBuffer_.size()));
            }
            if (GetFocus() != inspectorEdit_)
            {
                SetFocus(inspectorEdit_);
            }
        }
        else
        {
            ShowWindow(inspectorEdit_, SW_HIDE);
        }
    }

    if (eventTextEdit_ && !projectLauncherVisible_ && !projectDialogVisible_ && !sceneDialogVisible_ && !characterManagerVisible_ && !variableManagerVisible_ && !settingsDialogVisible_)
    {
        if (expandedTextCommandIndex_ < scenario_.commands.size() && HasVisibleArea(eventTextEditRect_))
        {
            SetWindowPos(eventTextEdit_, nullptr, eventTextEditRect_.left, eventTextEditRect_.top, eventTextEditRect_.right - eventTextEditRect_.left, eventTextEditRect_.bottom - eventTextEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            const std::wstring value = GetCommandParameter(scenario_.commands[expandedTextCommandIndex_], L"value");
            const int length = GetWindowTextLengthW(eventTextEdit_);
            std::wstring currentValue(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(eventTextEdit_, &currentValue[0], length + 1);
            currentValue.resize(length);
            if (currentValue != value)
            {
                SetWindowTextW(eventTextEdit_, value.c_str());
            }
        }
        else
        {
            ShowWindow(eventTextEdit_, SW_HIDE);
        }
    }

    if (sceneNameEdit_)
    {
        if (projectDialogVisible_)
        {
            SetWindowPos(sceneNameEdit_, nullptr, projectDialogEditRect_.left, projectDialogEditRect_.top, projectDialogEditRect_.right - projectDialogEditRect_.left, projectDialogEditRect_.bottom - projectDialogEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (GetFocus() != sceneNameEdit_)
            {
                SetFocus(sceneNameEdit_);
                SendMessageW(sceneNameEdit_, EM_SETSEL, 0, -1);
            }
        }
        else if (sceneDialogVisible_)
        {
            SetWindowPos(sceneNameEdit_, nullptr, sceneDialogEditRect_.left, sceneDialogEditRect_.top, sceneDialogEditRect_.right - sceneDialogEditRect_.left, sceneDialogEditRect_.bottom - sceneDialogEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (GetFocus() != sceneNameEdit_)
            {
                SetFocus(sceneNameEdit_);
                SendMessageW(sceneNameEdit_, EM_SETSEL, 0, -1);
            }
        }
        else if (characterManagerVisible_)
        {
            SetWindowPos(sceneNameEdit_, nullptr, characterDialogEditRect_.left, characterDialogEditRect_.top, characterDialogEditRect_.right - characterDialogEditRect_.left, characterDialogEditRect_.bottom - characterDialogEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (!characterFieldDialogVisible_ && GetFocus() != sceneNameEdit_)
            {
                SetFocus(sceneNameEdit_);
            }
        }
        else if (variableManagerVisible_)
        {
            SetWindowPos(sceneNameEdit_, nullptr, variableDialogEditRect_.left, variableDialogEditRect_.top, variableDialogEditRect_.right - variableDialogEditRect_.left, variableDialogEditRect_.bottom - variableDialogEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (!variableFieldDialogVisible_ && GetFocus() != sceneNameEdit_)
            {
                SetFocus(sceneNameEdit_);
            }
        }
        else
        {
            ShowWindow(sceneNameEdit_, SW_HIDE);
        }
    }

    if (characterFieldEdit_)
    {
        if (characterManagerVisible_ && characterFieldDialogVisible_)
        {
            SetWindowPos(characterFieldEdit_, nullptr, characterDialogFieldEditRect_.left, characterDialogFieldEditRect_.top, characterDialogFieldEditRect_.right - characterDialogFieldEditRect_.left, characterDialogFieldEditRect_.bottom - characterDialogFieldEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (GetFocus() != characterFieldEdit_)
            {
                SetFocus(characterFieldEdit_);
                SendMessageW(characterFieldEdit_, EM_SETSEL, 0, -1);
            }
        }
        else
        {
            ShowWindow(characterFieldEdit_, SW_HIDE);
        }
    }

    if (variableFieldEdit_)
    {
        if (variableManagerVisible_ && variableFieldDialogVisible_)
        {
            SetWindowPos(variableFieldEdit_, nullptr, variableFieldEditRect_.left, variableFieldEditRect_.top, variableFieldEditRect_.right - variableFieldEditRect_.left, variableFieldEditRect_.bottom - variableFieldEditRect_.top, SWP_NOZORDER | SWP_SHOWWINDOW);
            if (GetFocus() != variableFieldEdit_)
            {
                SetFocus(variableFieldEdit_);
                SendMessageW(variableFieldEdit_, EM_SETSEL, 0, -1);
            }
        }
        else
        {
            ShowWindow(variableFieldEdit_, SW_HIDE);
        }
    }
}

void NovelRuntime::TogglePreviewWindow()
{
    if (previewWindow_ && IsWindow(previewWindow_))
    {
        if (IsWindowVisible(previewWindow_))
        {
            ShowWindow(previewWindow_, SW_HIDE);
            previewVisible_ = false;
        }
        else
        {
            StartPreviewFromSelection();
            ShowWindow(previewWindow_, SW_SHOW);
            SetForegroundWindow(previewWindow_);
            previewVisible_ = true;
            SetWindowTextW(previewWindow_, storyTitle_.c_str());
        }
        return;
    }

    previewWindow_ = CreateWindowW(
        L"KaktosPreviewWindow",
        storyTitle_.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        700,
        hostWindow_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    previewVisible_ = previewWindow_ != nullptr;
    if (previewWindow_)
    {
        StartPreviewFromSelection();
        SetWindowTextW(previewWindow_, storyTitle_.c_str());
    }
}

void NovelRuntime::LoadProjectSettings(const std::wstring& projectPath)
{
    std::wstring content;
    if (!TryReadTextFile(projectPath, content))
    {
        return;
    }

    characterDefinitions_.clear();
    variableDefinitions_.clear();
    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        const size_t split = line.find(L'=');
        if (split == std::wstring::npos)
        {
            continue;
        }

        const std::wstring key = Trim(line.substr(0, split));
        const std::wstring value = Trim(line.substr(split + 1));
        if (key == L"left_panel_width") leftPanelWidth_ = _wtoi(value.c_str());
        else if (key == L"right_panel_width") rightPanelWidth_ = _wtoi(value.c_str());
        else if (key == L"graph_height") graphHeight_ = _wtoi(value.c_str());
        else if (key == L"event_list_height") eventListHeight_ = _wtoi(value.c_str());
        else if (key == L"show_components") showComponents_ = value == L"1";
        else if (key == L"show_inspector") showInspector_ = value == L"1";
        else if (key == L"show_flow_graph") showFlowGraph_ = value == L"1";
        else if (key == L"show_preview_panel") showPreviewPanel_ = value == L"1";
        else if (key == L"show_event_list") showEventList_ = value == L"1";
        else if (key == L"settings_window_width") editorSettings_.windowWidth = (std::max)(960, _wtoi(value.c_str()));
        else if (key == L"settings_window_height") editorSettings_.windowHeight = (std::max)(540, _wtoi(value.c_str()));
        else if (key == L"settings_default_font" && !value.empty()) editorSettings_.defaultFont = UnescapeSaveValue(value);
        else if (key == L"settings_default_text_speed") editorSettings_.defaultTextSpeed = (std::max)(0, _wtoi(value.c_str()));
        else if (key == L"settings_master_volume") editorSettings_.masterVolume = (std::max)(0, (std::min)(100, _wtoi(value.c_str())));
        else if (key == L"settings_bgm_volume") editorSettings_.bgmVolume = (std::max)(0, (std::min)(100, _wtoi(value.c_str())));
        else if (key == L"settings_se_volume") editorSettings_.seVolume = (std::max)(0, (std::min)(100, _wtoi(value.c_str())));
        else if (key == L"settings_voice_volume") editorSettings_.voiceVolume = (std::max)(0, (std::min)(100, _wtoi(value.c_str())));
        else if (key == L"settings_save_directory") editorSettings_.saveDirectory = UnescapeSaveValue(value);
        else if (key == L"settings_autosave_enabled") editorSettings_.autosaveEnabled = value != L"0";
        else if (key == L"settings_message_visible") editorSettings_.defaultMessageWindowVisible = value != L"0";
        else if (key == L"settings_message_color") editorSettings_.defaultMessageWindowColor = static_cast<COLORREF>(_wtoi(value.c_str()));
        else if (key == L"settings_message_border") editorSettings_.defaultMessageWindowBorderColor = static_cast<COLORREF>(_wtoi(value.c_str()));
        else if (key == L"settings_message_opacity") editorSettings_.defaultMessageWindowOpacity = ClampByteValue(_wtoi(value.c_str()));
        else if (key == L"settings_message_padding") editorSettings_.defaultMessageWindowPadding = (std::max)(8, _wtoi(value.c_str()));
        else if (key == L"settings_message_image") editorSettings_.defaultMessageWindowImage = UnescapeSaveValue(value);
        else if (key == L"settings_title_start_enabled") editorSettings_.titleMenuStartEnabled = value != L"0";
        else if (key == L"settings_title_load_enabled") editorSettings_.titleMenuLoadEnabled = value != L"0";
        else if (key == L"settings_title_options_enabled") editorSettings_.titleMenuOptionsEnabled = value != L"0";
        else if (key == L"settings_title_exit_enabled") editorSettings_.titleMenuExitEnabled = value != L"0";
        else if (key == L"settings_name_visible") editorSettings_.defaultNameWindowVisible = value != L"0";
        else if (key == L"settings_name_color") editorSettings_.defaultNameWindowColor = static_cast<COLORREF>(_wtoi(value.c_str()));
        else if (key == L"settings_name_border") editorSettings_.defaultNameWindowBorderColor = static_cast<COLORREF>(_wtoi(value.c_str()));
        else if (key == L"settings_name_opacity") editorSettings_.defaultNameWindowOpacity = ClampByteValue(_wtoi(value.c_str()));
        else if (key == L"settings_name_x") editorSettings_.defaultNameWindowOffsetX = _wtoi(value.c_str());
        else if (key == L"settings_name_y") editorSettings_.defaultNameWindowOffsetY = _wtoi(value.c_str());
        else if (key == L"settings_name_width") editorSettings_.defaultNameWindowWidth = (std::max)(120, _wtoi(value.c_str()));
        else if (key == L"settings_name_height") editorSettings_.defaultNameWindowHeight = (std::max)(28, _wtoi(value.c_str()));
        else if (key == L"settings_name_padding") editorSettings_.defaultNameWindowPadding = (std::max)(4, _wtoi(value.c_str()));
        else if (key == L"settings_name_image") editorSettings_.defaultNameWindowImage = UnescapeSaveValue(value);
        else if (StartsWithText(key, L"variable_def."))
        {
            const size_t firstDot = key.find(L'.');
            const size_t secondDot = key.find(L'.', firstDot + 1);
            if (secondDot == std::wstring::npos)
            {
                continue;
            }
            const size_t variableIndex = static_cast<size_t>(_wtoi(key.substr(firstDot + 1, secondDot - firstDot - 1).c_str()));
            while (variableDefinitions_.size() <= variableIndex)
            {
                variableDefinitions_.push_back(VariableDefinition{});
            }
            VariableDefinition& definition = variableDefinitions_[variableIndex];
            const std::wstring field = key.substr(secondDot + 1);
            if (field == L"name") definition.name = UnescapeSaveValue(value);
            else if (field == L"type")
            {
                const std::wstring decoded = UnescapeSaveValue(value);
                definition.type = decoded == L"bool" ? VariableType::Bool : (decoded == L"int" ? VariableType::Integer : VariableType::String);
            }
            else if (field == L"initial") definition.initialValue = UnescapeSaveValue(value);
            else if (field == L"description") definition.description = UnescapeSaveValue(value);
        }
        else if (StartsWithText(key, L"ui_button."))
        {
            const size_t firstDot = key.find(L'.');
            const size_t secondDot = key.find(L'.', firstDot + 1);
            if (secondDot == std::wstring::npos)
            {
                continue;
            }
            const size_t buttonIndex = static_cast<size_t>(_wtoi(key.substr(firstDot + 1, secondDot - firstDot - 1).c_str()));
            while (uiButtons_.size() <= buttonIndex)
            {
                uiButtons_.push_back(UiButtonDefinition{});
            }
            UiButtonDefinition& button = uiButtons_[buttonIndex];
            const std::wstring field = key.substr(secondDot + 1);
            if (field == L"id") button.id = UnescapeSaveValue(value);
            else if (field == L"label") button.label = UnescapeSaveValue(value);
            else if (field == L"icon") button.iconPath = UnescapeSaveValue(value);
            else if (field == L"x") button.x = _wtoi(value.c_str());
            else if (field == L"y") button.y = _wtoi(value.c_str());
            else if (field == L"width") button.width = (std::max)(40, _wtoi(value.c_str()));
            else if (field == L"height") button.height = (std::max)(20, _wtoi(value.c_str()));
            else if (field == L"visible") button.visible = value != L"0";
        }
        else if (key == L"character_def" && !value.empty())
        {
            CharacterDefinition definition;
            definition.id = value;
            definition.displayName = value;
            characterDefinitions_.push_back(definition);
        }
        else if (StartsWithText(key, L"character."))
        {
            const size_t firstDot = key.find(L'.');
            const size_t secondDot = key.find(L'.', firstDot + 1);
            if (secondDot == std::wstring::npos)
            {
                continue;
            }

            const size_t characterIndex = static_cast<size_t>(_wtoi(key.substr(firstDot + 1, secondDot - firstDot - 1).c_str()));
            while (characterDefinitions_.size() <= characterIndex)
            {
                characterDefinitions_.push_back(CharacterDefinition{});
            }

            CharacterDefinition& definition = characterDefinitions_[characterIndex];
            const std::wstring field = key.substr(secondDot + 1);
            if (field == L"id") definition.id = UnescapeSaveValue(value);
            else if (field == L"display_name") definition.displayName = UnescapeSaveValue(value);
            else if (field == L"base_image") definition.baseImagePath = UnescapeSaveValue(value);
            else if (field == L"color") definition.color = UnescapeSaveValue(value);
            else if (StartsWithText(field, L"expression."))
            {
                const size_t exprSecondDot = field.find(L'.', 11);
                if (exprSecondDot == std::wstring::npos)
                {
                    continue;
                }
                const size_t expressionIndex = static_cast<size_t>(_wtoi(field.substr(11, exprSecondDot - 11).c_str()));
                while (definition.expressions.size() <= expressionIndex)
                {
                    definition.expressions.push_back(CharacterExpressionDefinition{});
                }
                CharacterExpressionDefinition& expression = definition.expressions[expressionIndex];
                const std::wstring exprField = field.substr(exprSecondDot + 1);
                if (exprField == L"name") expression.name = UnescapeSaveValue(value);
                else if (exprField == L"image") expression.imagePath = UnescapeSaveValue(value);
            }
        }
    }

    characterDefinitions_.erase(
        std::remove_if(
            characterDefinitions_.begin(),
            characterDefinitions_.end(),
            [](const CharacterDefinition& definition) { return Trim(definition.id).empty(); }),
        characterDefinitions_.end());

    variableDefinitions_.erase(
        std::remove_if(
            variableDefinitions_.begin(),
            variableDefinitions_.end(),
            [](const VariableDefinition& definition) { return Trim(definition.name).empty(); }),
        variableDefinitions_.end());

    SyncVariableDefinitions();
    ApplyEditorUiDefaults();

    showEventList_ = true;
    if (hostWindow_ && !playerMode_)
    {
        SetWindowPos(hostWindow_, nullptr, 0, 0, editorSettings_.windowWidth, editorSettings_.windowHeight, SWP_NOMOVE | SWP_NOZORDER);
    }
}

bool NovelRuntime::SaveProject()
{
    if (scenarioPath_.empty())
    {
        scenarioPath_ = CombinePath(GetScenarioDirectory(), L"main.ks");
    }
    if (projectPath_.empty())
    {
        projectPath_ = CombinePath(GetAssetsRootDirectory(), L"project.kproj");
    }

    SyncDocumentMetadata();
    if (!TryWriteTextFile(scenarioPath_, SerializeScenario(scenario_)))
    {
        statusText_ = L"\u30b7\u30ca\u30ea\u30aa\u4fdd\u5b58\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    const std::wstring projectText = BuildProjectFileText();
    if (!TryWriteTextFile(projectPath_, projectText))
    {
        statusText_ = L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u4fdd\u5b58\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    MarkCurrentStateSaved();
    statusText_ = L"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\u3092\u4fdd\u5b58\u3057\u307e\u3057\u305f";
    ShowToast(L"保存しました");
    return true;
}

std::wstring NovelRuntime::BuildProjectFileText() const
{
    std::wstring projectText = SerializeProjectSettings();
    for (size_t i = 0; i < characterDefinitions_.size(); ++i)
    {
        const CharacterDefinition& definition = characterDefinitions_[i];
        projectText += L"character." + std::to_wstring(i) + L".id=" + EscapeSaveValue(definition.id) + L"\r\n";
        projectText += L"character." + std::to_wstring(i) + L".display_name=" + EscapeSaveValue(definition.displayName) + L"\r\n";
        projectText += L"character." + std::to_wstring(i) + L".base_image=" + EscapeSaveValue(definition.baseImagePath) + L"\r\n";
        projectText += L"character." + std::to_wstring(i) + L".color=" + EscapeSaveValue(definition.color) + L"\r\n";
        for (size_t expressionIndex = 0; expressionIndex < definition.expressions.size(); ++expressionIndex)
        {
            const CharacterExpressionDefinition& expression = definition.expressions[expressionIndex];
            projectText += L"character." + std::to_wstring(i) + L".expression." + std::to_wstring(expressionIndex) + L".name=" + EscapeSaveValue(expression.name) + L"\r\n";
            projectText += L"character." + std::to_wstring(i) + L".expression." + std::to_wstring(expressionIndex) + L".image=" + EscapeSaveValue(expression.imagePath) + L"\r\n";
        }
    }
    for (size_t i = 0; i < variableDefinitions_.size(); ++i)
    {
        const VariableDefinition& definition = variableDefinitions_[i];
        projectText += L"variable_def." + std::to_wstring(i) + L".name=" + EscapeSaveValue(definition.name) + L"\r\n";
        projectText += L"variable_def." + std::to_wstring(i) + L".type=" + EscapeSaveValue(definition.type == VariableType::Bool ? L"bool" : (definition.type == VariableType::Integer ? L"int" : L"string")) + L"\r\n";
        projectText += L"variable_def." + std::to_wstring(i) + L".initial=" + EscapeSaveValue(definition.initialValue) + L"\r\n";
        projectText += L"variable_def." + std::to_wstring(i) + L".description=" + EscapeSaveValue(definition.description) + L"\r\n";
    }
    return projectText;
}

void NovelRuntime::MarkCurrentStateSaved()
{
    lastSavedScenarioText_ = SerializeScenario(scenario_);
    lastSavedProjectText_ = BuildProjectFileText();
}

bool NovelRuntime::HasUnsavedChanges() const
{
    if (scenarioPath_.empty() && projectPath_.empty())
    {
        return false;
    }
    return SerializeScenario(scenario_) != lastSavedScenarioText_ || BuildProjectFileText() != lastSavedProjectText_;
}

bool NovelRuntime::ConfirmDiscardUnsavedChanges()
{
    if (!HasUnsavedChanges())
    {
        return true;
    }

    const int result = MessageBoxW(
        hostWindow_,
        L"保存していない変更があります。\n保存してから移動しますか？",
        L"未保存の変更",
        MB_ICONWARNING | MB_YESNOCANCEL);
    if (result == IDCANCEL)
    {
        statusText_ = L"移動をキャンセルしました";
        return false;
    }
    if (result == IDYES)
    {
        return SaveProject();
    }
    return true;
}

bool NovelRuntime::SwitchScenarioFile(const std::wstring& scenarioPath)
{
    if (scenarioPath.empty() || scenarioPath == scenarioPath_)
    {
        selectedScenePath_ = scenarioPath_;
        return true;
    }
    if (!ConfirmDiscardUnsavedChanges())
    {
        return false;
    }

    selectedScenePath_ = scenarioPath;
    LoadScenario(scenarioPath);
    statusText_ = L"\u30b7\u30fc\u30f3\u3092\u5207\u308a\u66ff\u3048\u307e\u3057\u305f";
    return true;
}

void NovelRuntime::ShowToast(const std::wstring& text)
{
    toastText_ = text;
    toastStartTick_ = GetTickCount();
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, FALSE);
    }
}

void NovelRuntime::DrawToast(HDC hdc, const RECT& clientRect) const
{
    if (toastText_.empty() || toastStartTick_ == 0)
    {
        return;
    }

    const DWORD elapsed = GetTickCount() - toastStartTick_;
    if (elapsed >= toastDurationMs_)
    {
        return;
    }

    const int width = 230;
    const int height = 46;
    const int margin = 20;
    int slide = 0;
    if (elapsed < 240)
    {
        slide = width - static_cast<int>((static_cast<double>(elapsed) / 240.0) * width);
    }
    else if (elapsed > toastDurationMs_ - 280)
    {
        slide = static_cast<int>((static_cast<double>(elapsed - (toastDurationMs_ - 280)) / 280.0) * width);
    }

    RECT toastRect =
    {
        clientRect.right - margin - width + slide,
        clientRect.bottom - margin - height,
        clientRect.right - margin + slide,
        clientRect.bottom - margin
    };

    HDC memoryDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    RECT localRect = { 0, 0, width, height };
    HBRUSH brush = CreateSolidBrush(RGB(28, 34, 42));
    FillRect(memoryDc, &localRect, brush);
    DeleteObject(brush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(82, 152, 102));
    FrameRect(memoryDc, &localRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(memoryDc, TRANSPARENT);
    HFONT font = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memoryDc, font));
    SetTextColor(memoryDc, RGB(236, 246, 238));
    RECT textRect = { 16, 0, width - 16, height };
    DrawTextW(memoryDc, toastText_.c_str(), -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(memoryDc, oldFont);
    DeleteObject(font);

    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 236, 0 };
    AlphaBlend(hdc, toastRect.left, toastRect.top, width, height, memoryDc, 0, 0, width, height, blend);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
}

std::wstring NovelRuntime::SerializeProjectSettings() const
{
    std::wstring projectText;
    projectText += L"scenario_path=" + scenarioPath_ + L"\r\n";
    projectText += L"left_panel_width=" + std::to_wstring(leftPanelWidth_) + L"\r\n";
    projectText += L"right_panel_width=" + std::to_wstring(rightPanelWidth_) + L"\r\n";
    projectText += L"graph_height=" + std::to_wstring(graphHeight_) + L"\r\n";
    projectText += L"event_list_height=" + std::to_wstring(eventListHeight_) + L"\r\n";
    projectText += L"show_components=" + std::wstring(showComponents_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_inspector=" + std::wstring(showInspector_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_flow_graph=" + std::wstring(showFlowGraph_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_preview_panel=" + std::wstring(showPreviewPanel_ ? L"1" : L"0") + L"\r\n";
    projectText += L"show_event_list=" + std::wstring(showEventList_ ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_window_width=" + std::to_wstring(editorSettings_.windowWidth) + L"\r\n";
    projectText += L"settings_window_height=" + std::to_wstring(editorSettings_.windowHeight) + L"\r\n";
    projectText += L"settings_default_font=" + EscapeSaveValue(editorSettings_.defaultFont) + L"\r\n";
    projectText += L"settings_default_text_speed=" + std::to_wstring(editorSettings_.defaultTextSpeed) + L"\r\n";
    projectText += L"settings_master_volume=" + std::to_wstring(editorSettings_.masterVolume) + L"\r\n";
    projectText += L"settings_bgm_volume=" + std::to_wstring(editorSettings_.bgmVolume) + L"\r\n";
    projectText += L"settings_se_volume=" + std::to_wstring(editorSettings_.seVolume) + L"\r\n";
    projectText += L"settings_voice_volume=" + std::to_wstring(editorSettings_.voiceVolume) + L"\r\n";
    projectText += L"settings_save_directory=" + EscapeSaveValue(editorSettings_.saveDirectory) + L"\r\n";
    projectText += L"settings_autosave_enabled=" + std::wstring(editorSettings_.autosaveEnabled ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_message_visible=" + std::wstring(editorSettings_.defaultMessageWindowVisible ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_message_color=" + std::to_wstring(static_cast<unsigned int>(editorSettings_.defaultMessageWindowColor)) + L"\r\n";
    projectText += L"settings_message_border=" + std::to_wstring(static_cast<unsigned int>(editorSettings_.defaultMessageWindowBorderColor)) + L"\r\n";
    projectText += L"settings_message_opacity=" + std::to_wstring(editorSettings_.defaultMessageWindowOpacity) + L"\r\n";
    projectText += L"settings_message_padding=" + std::to_wstring(editorSettings_.defaultMessageWindowPadding) + L"\r\n";
    projectText += L"settings_message_image=" + EscapeSaveValue(editorSettings_.defaultMessageWindowImage) + L"\r\n";
    projectText += L"settings_title_start_enabled=" + std::wstring(editorSettings_.titleMenuStartEnabled ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_title_load_enabled=" + std::wstring(editorSettings_.titleMenuLoadEnabled ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_title_options_enabled=" + std::wstring(editorSettings_.titleMenuOptionsEnabled ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_title_exit_enabled=" + std::wstring(editorSettings_.titleMenuExitEnabled ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_name_visible=" + std::wstring(editorSettings_.defaultNameWindowVisible ? L"1" : L"0") + L"\r\n";
    projectText += L"settings_name_color=" + std::to_wstring(static_cast<unsigned int>(editorSettings_.defaultNameWindowColor)) + L"\r\n";
    projectText += L"settings_name_border=" + std::to_wstring(static_cast<unsigned int>(editorSettings_.defaultNameWindowBorderColor)) + L"\r\n";
    projectText += L"settings_name_opacity=" + std::to_wstring(editorSettings_.defaultNameWindowOpacity) + L"\r\n";
    projectText += L"settings_name_x=" + std::to_wstring(editorSettings_.defaultNameWindowOffsetX) + L"\r\n";
    projectText += L"settings_name_y=" + std::to_wstring(editorSettings_.defaultNameWindowOffsetY) + L"\r\n";
    projectText += L"settings_name_width=" + std::to_wstring(editorSettings_.defaultNameWindowWidth) + L"\r\n";
    projectText += L"settings_name_height=" + std::to_wstring(editorSettings_.defaultNameWindowHeight) + L"\r\n";
    projectText += L"settings_name_padding=" + std::to_wstring(editorSettings_.defaultNameWindowPadding) + L"\r\n";
    projectText += L"settings_name_image=" + EscapeSaveValue(editorSettings_.defaultNameWindowImage) + L"\r\n";
    for (size_t i = 0; i < uiButtons_.size(); ++i)
    {
        const UiButtonDefinition& button = uiButtons_[i];
        projectText += L"ui_button." + std::to_wstring(i) + L".id=" + EscapeSaveValue(button.id) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".label=" + EscapeSaveValue(button.label) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".icon=" + EscapeSaveValue(button.iconPath) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".x=" + std::to_wstring(button.x) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".y=" + std::to_wstring(button.y) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".width=" + std::to_wstring(button.width) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".height=" + std::to_wstring(button.height) + L"\r\n";
        projectText += L"ui_button." + std::to_wstring(i) + L".visible=" + std::wstring(button.visible ? L"1" : L"0") + L"\r\n";
    }
    return projectText;
}

bool NovelRuntime::SaveProjectAs()
{
    WCHAR fileBuffer[MAX_PATH] = {};
    const std::wstring defaultName = scenarioPath_.empty() ? L"main.ks" : GetFileNamePart(scenarioPath_);
    wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        statusText_ = L"\u4fdd\u5b58\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    scenarioPath_ = fileBuffer;
    scenarioBaseDir_ = GetDirectoryPath(scenarioPath_);
    projectPath_ = CombinePath(GetAssetsRootDirectory(), L"project.kproj");
    RefreshSceneList();
    RefreshAssetList();
    return SaveProject();
}

std::wstring NovelRuntime::BuildDefaultProjectSettingsText(const std::wstring& scenarioPath) const
{
    std::wstring text;
    text += L"scenario_path=" + EscapeSaveValue(scenarioPath) + L"\r\n";
    text += L"left_panel_width=280\r\n";
    text += L"right_panel_width=320\r\n";
    text += L"graph_height=162\r\n";
    text += L"event_list_height=208\r\n";
    text += L"show_components=1\r\n";
    text += L"show_inspector=1\r\n";
    text += L"show_flow_graph=0\r\n";
    text += L"show_preview_panel=0\r\n";
    text += L"show_event_list=1\r\n";
    text += L"settings_window_width=1280\r\n";
    text += L"settings_window_height=720\r\n";
    text += L"settings_default_font=" + EscapeSaveValue(editorSettings_.defaultFont) + L"\r\n";
    text += L"settings_default_text_speed=40\r\n";
    text += L"settings_master_volume=100\r\n";
    text += L"settings_bgm_volume=100\r\n";
    text += L"settings_se_volume=100\r\n";
    text += L"settings_voice_volume=100\r\n";
    text += L"settings_save_directory=\r\n";
    text += L"settings_autosave_enabled=1\r\n";
    text += L"settings_message_visible=1\r\n";
    text += L"settings_message_color=" + std::to_wstring(static_cast<unsigned int>(RGB(8, 10, 14))) + L"\r\n";
    text += L"settings_message_border=" + std::to_wstring(static_cast<unsigned int>(RGB(122, 128, 138))) + L"\r\n";
    text += L"settings_message_opacity=178\r\n";
    text += L"settings_message_padding=24\r\n";
    text += L"settings_message_image=\r\n";
    text += L"settings_title_start_enabled=1\r\n";
    text += L"settings_title_load_enabled=0\r\n";
    text += L"settings_title_options_enabled=0\r\n";
    text += L"settings_title_exit_enabled=0\r\n";
    text += L"settings_name_visible=1\r\n";
    text += L"settings_name_color=" + std::to_wstring(static_cast<unsigned int>(RGB(12, 18, 28))) + L"\r\n";
    text += L"settings_name_border=" + std::to_wstring(static_cast<unsigned int>(RGB(80, 132, 180))) + L"\r\n";
    text += L"settings_name_opacity=214\r\n";
    text += L"settings_name_x=0\r\n";
    text += L"settings_name_y=0\r\n";
    text += L"settings_name_width=220\r\n";
    text += L"settings_name_height=36\r\n";
    text += L"settings_name_padding=12\r\n";
    text += L"settings_name_image=\r\n";
    text += L"ui_button.0.id=save\r\nui_button.0.label=SAVE\r\nui_button.0.icon=ui\\\\save.png\r\nui_button.0.x=-348\r\nui_button.0.y=-52\r\nui_button.0.width=36\r\nui_button.0.height=36\r\nui_button.0.visible=0\r\n";
    text += L"ui_button.1.id=load\r\nui_button.1.label=LOAD\r\nui_button.1.icon=ui\\\\load.png\r\nui_button.1.x=-296\r\nui_button.1.y=-52\r\nui_button.1.width=36\r\nui_button.1.height=36\r\nui_button.1.visible=0\r\n";
    text += L"ui_button.2.id=log\r\nui_button.2.label=LOG\r\nui_button.2.icon=ui\\\\log.png\r\nui_button.2.x=-244\r\nui_button.2.y=-52\r\nui_button.2.width=36\r\nui_button.2.height=36\r\nui_button.2.visible=0\r\n";
    text += L"ui_button.3.id=hide\r\nui_button.3.label=HIDE\r\nui_button.3.icon=ui\\\\hide.png\r\nui_button.3.x=-192\r\nui_button.3.y=-52\r\nui_button.3.width=36\r\nui_button.3.height=36\r\nui_button.3.visible=0\r\n";
    text += L"ui_button.4.id=menu\r\nui_button.4.label=MENU\r\nui_button.4.icon=ui\\\\menu_ui.png\r\nui_button.4.x=-140\r\nui_button.4.y=-52\r\nui_button.4.width=36\r\nui_button.4.height=36\r\nui_button.4.visible=1\r\n";
    return text;
}

std::wstring NovelRuntime::GetRecentProjectsPath() const
{
    WCHAR appDataPath[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataPath)))
    {
        return L"recent_projects.txt";
    }

    const std::wstring settingsDir = CombinePath(appDataPath, L"KaktosEngine");
    EnsureDirectoryExists(settingsDir);
    return CombinePath(settingsDir, L"recent_projects.txt");
}

void NovelRuntime::LoadRecentProjects()
{
    recentProjects_.clear();
    std::wstring content;
    if (!TryReadTextFile(GetRecentProjectsPath(), content))
    {
        return;
    }

    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty())
        {
            continue;
        }
        const std::wstring normalized = NormalizeFullPath(line);
        if (std::none_of(recentProjects_.begin(), recentProjects_.end(), [&](const std::wstring& existing)
            {
                return _wcsicmp(existing.c_str(), normalized.c_str()) == 0;
            }))
        {
            recentProjects_.push_back(normalized);
        }
    }
}

void NovelRuntime::SaveRecentProjects() const
{
    std::wstring content;
    for (const std::wstring& path : recentProjects_)
    {
        if (!path.empty())
        {
            content += path + L"\r\n";
        }
    }
    TryWriteTextFile(GetRecentProjectsPath(), content);
}

void NovelRuntime::AddRecentProject(const std::wstring& projectPath)
{
    const std::wstring normalized = NormalizeFullPath(projectPath);
    if (normalized.empty())
    {
        return;
    }

    recentProjects_.erase(std::remove_if(recentProjects_.begin(), recentProjects_.end(), [&](const std::wstring& existing)
        {
            return _wcsicmp(existing.c_str(), normalized.c_str()) == 0;
        }), recentProjects_.end());
    recentProjects_.insert(recentProjects_.begin(), normalized);
    if (recentProjects_.size() > 20)
    {
        recentProjects_.resize(20);
    }
    SaveRecentProjects();
}

void NovelRuntime::RemoveRecentProject(const std::wstring& projectPath)
{
    const std::wstring normalized = NormalizeFullPath(projectPath);
    recentProjects_.erase(std::remove_if(recentProjects_.begin(), recentProjects_.end(), [&](const std::wstring& existing)
        {
            return _wcsicmp(existing.c_str(), normalized.c_str()) == 0;
        }), recentProjects_.end());
    SaveRecentProjects();
}

std::wstring NovelRuntime::GetProjectRootFromProjectPath(const std::wstring& projectPath) const
{
    const std::wstring projectDir = GetDirectoryPath(projectPath);
    if (_wcsicmp(GetFileNamePart(projectDir).c_str(), L"assets") == 0)
    {
        const std::wstring root = GetDirectoryPath(projectDir);
        if (!root.empty())
        {
            return root;
        }
    }
    return projectDir;
}

bool NovelRuntime::OpenProjectFolder(const std::wstring& projectPath)
{
    const std::wstring root = GetProjectRootFromProjectPath(projectPath);
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (root.empty() || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        statusText_ = L"プロジェクトフォルダが見つかりません";
        return true;
    }

    ShellExecuteW(hostWindow_, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    statusText_ = L"プロジェクトフォルダを開きました";
    return true;
}

bool NovelRuntime::DeleteProjectToRecycleBin(const std::wstring& projectPath)
{
    const std::wstring root = GetProjectRootFromProjectPath(projectPath);
    if (root.empty())
    {
        RemoveRecentProject(projectPath);
        return true;
    }

    if (MessageBoxW(hostWindow_, (L"プロジェクトをゴミ箱へ移動しますか?\n" + root).c_str(), L"プロジェクト削除", MB_ICONWARNING | MB_YESNO) != IDYES)
    {
        return true;
    }

    std::wstring doubleNullPath = root;
    doubleNullPath.push_back(L'\0');
    doubleNullPath.push_back(L'\0');

    SHFILEOPSTRUCTW fileOp = {};
    fileOp.hwnd = hostWindow_;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    const int result = SHFileOperationW(&fileOp);
    if (result == 0 && !fileOp.fAnyOperationsAborted)
    {
        RemoveRecentProject(projectPath);
        if (_wcsicmp(projectPath_.c_str(), projectPath.c_str()) == 0)
        {
            scenarioPath_.clear();
            projectPath_.clear();
            scenarioBaseDir_.clear();
            scenario_ = ScenarioDocument{};
            currentText_.clear();
            displayedText_.clear();
            reachedEnd_ = true;
            ShowProjectLauncher();
        }
        statusText_ = L"プロジェクトをゴミ箱へ移動しました";
    }
    else
    {
        statusText_ = L"プロジェクト削除に失敗しました";
    }
    return true;
}

bool NovelRuntime::HandleProjectLauncherClick(POINT point)
{
    if (PtInRect(&projectLauncherCreateRect_, point))
    {
        ShowProjectDialog();
        return true;
    }
    if (PtInRect(&projectLauncherOpenRect_, point))
    {
        return LoadProjectFromDialog();
    }

    for (const ProjectLauncherRow& row : projectLauncherRows_)
    {
        if (PtInRect(&row.dataRect, point))
        {
            return OpenProjectFolder(row.projectPath);
        }
        if (PtInRect(&row.deleteRect, point))
        {
            return DeleteProjectToRecycleBin(row.projectPath);
        }
        if (PtInRect(&row.rowRect, point))
        {
            return LoadProjectFile(row.projectPath);
        }
    }

    return true;
}

bool NovelRuntime::CreateProjectFromDialog()
{
    if (!sceneNameEdit_)
    {
        return false;
    }
    if (!ConfirmDiscardUnsavedChanges())
    {
        return true;
    }

    const int length = GetWindowTextLengthW(sceneNameEdit_);
    std::wstring projectName(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(sceneNameEdit_, &projectName[0], length + 1);
    projectName.resize(length);
    projectName = Trim(projectName);
    projectName.erase(std::remove_if(projectName.begin(), projectName.end(), [](wchar_t ch)
    {
        return ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch < 32;
    }), projectName.end());

    if (projectName.empty())
    {
        statusText_ = L"プロジェクト名を入力してください";
        SetFocus(sceneNameEdit_);
        return true;
    }

    std::wstring initialPath = GetDirectoryPath(GetAssetsRootDirectory());
    const std::wstring parentDirectory = BrowseForFolder(L"プロジェクト作成先を選択", initialPath);
    if (parentDirectory.empty())
    {
        statusText_ = L"プロジェクト作成をキャンセルしました";
        return true;
    }

    const std::wstring projectRoot = CombinePath(parentDirectory, projectName);
    const std::wstring assetsRoot = CombinePath(projectRoot, L"assets");
    const std::wstring scenarioDir = CombinePath(assetsRoot, L"scenario");
    if (!EnsureDirectoryExists(projectRoot) || !EnsureDirectoryExists(assetsRoot) || !EnsureDirectoryExists(scenarioDir))
    {
        statusText_ = L"プロジェクトフォルダ作成に失敗しました";
        return true;
    }

    const wchar_t* subdirs[] = { L"background", L"bgm", L"character", L"fonts", L"picture", L"save", L"se", L"textbox", L"ui" };
    for (const wchar_t* subdir : subdirs)
    {
        EnsureDirectoryExists(CombinePath(assetsRoot, subdir));
    }

    const std::wstring templateAssets = GetAssetsRootDirectory();
    const std::wstring templateUi = CombinePath(templateAssets, L"ui");
    const std::wstring templateTextbox = CombinePath(templateAssets, L"textbox");
    const std::wstring templateFonts = CombinePath(templateAssets, L"fonts");
    if (GetFileAttributesW(templateUi.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CopyDirectoryTree(templateUi, CombinePath(assetsRoot, L"ui"));
    }
    if (GetFileAttributesW(templateTextbox.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CopyDirectoryTree(templateTextbox, CombinePath(assetsRoot, L"textbox"));
    }
    if (GetFileAttributesW(templateFonts.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CopyDirectoryTree(templateFonts, CombinePath(assetsRoot, L"fonts"));
    }

    const std::wstring titleScenarioPath = CombinePath(scenarioDir, L"title.ks");
    const std::wstring sceneScenarioPath = CombinePath(scenarioDir, L"scene.ks");
    const std::wstring projectPath = CombinePath(assetsRoot, L"project.kproj");
    const std::wstring title = projectName.empty() ? L"Kaktos Engine" : projectName;
    std::wstring titleOptions;
    if (editorSettings_.titleMenuStartEnabled)
    {
        titleOptions += L"[option text=\"はじめる\" target=\"scene.ks\"]\r\n";
    }
    if (editorSettings_.titleMenuLoadEnabled)
    {
        titleOptions += L"[option text=\"ロード\" target=\"__load\"]\r\n";
    }
    if (editorSettings_.titleMenuOptionsEnabled)
    {
        titleOptions += L"[option text=\"オプション\" target=\"__options\"]\r\n";
    }
    if (editorSettings_.titleMenuExitEnabled)
    {
        titleOptions += L"[option text=\"終了\" target=\"__exit\"]\r\n";
    }
    if (titleOptions.empty())
    {
        titleOptions += L"[option text=\"はじめる\" target=\"scene.ks\"]\r\n";
    }
    const std::wstring titleScenarioText =
        L"[title name=\"" + title + L"\"]\r\n"
        L"[messagewindow visible=\"false\"]\r\n"
        L"[fade time=\"1000\" color=\"#000000\" opacity=\"255\" target=\"all\"]\r\n"
        L"[choice prompt=\"\"]\r\n"
        + titleOptions
        + L"[endchoice]\r\n";
    const std::wstring sceneScenarioText =
        L"[title name=\"Scene\"]\r\n"
        L"[fade time=\"1000\" color=\"#000000\" opacity=\"255\" target=\"all\"]\r\n"
        L"[text value=\"New Text\"]\r\n";
    if (!TryWriteTextFile(titleScenarioPath, titleScenarioText) ||
        !TryWriteTextFile(sceneScenarioPath, sceneScenarioText) ||
        !TryWriteTextFile(projectPath, BuildDefaultProjectSettingsText(L"scenario\\title.ks")))
    {
        statusText_ = L"プロジェクトファイル作成に失敗しました";
        return true;
    }

    HideProjectDialog();
    autosaveRestoreChecked_ = true;
    if (LoadProjectFile(projectPath))
    {
        statusText_ = L"プロジェクトを作成しました: " + projectName;
    }
    return true;
}

bool NovelRuntime::LoadProjectFromDialog()
{
    WCHAR fileBuffer[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Project (*.kproj)\0*.kproj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"kproj";
    if (!GetOpenFileNameW(&ofn))
    {
        statusText_ = L"プロジェクト読み込みをキャンセルしました";
        return true;
    }

    HideProjectDialog();
    autosaveRestoreChecked_ = true;
    return LoadProjectFile(fileBuffer);
}

bool NovelRuntime::LoadProjectFile(const std::wstring& projectPath)
{
    if (projectPath != projectPath_ && !ConfirmDiscardUnsavedChanges())
    {
        return false;
    }

    std::wstring content;
    if (!TryReadTextFile(projectPath, content))
    {
        statusText_ = L"プロジェクトを読み込めませんでした";
        return false;
    }

    std::wstring scenarioValue;
    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        const size_t split = line.find(L'=');
        if (split == std::wstring::npos)
        {
            continue;
        }
        if (Trim(line.substr(0, split)) == L"scenario_path")
        {
            scenarioValue = UnescapeSaveValue(Trim(line.substr(split + 1)));
            break;
        }
    }

    if (scenarioValue.empty())
    {
        scenarioValue = L"scenario\\main.ks";
    }

    std::wstring scenarioPath = scenarioValue;
    if (scenarioPath.find(L":") == std::wstring::npos && !(scenarioPath.size() >= 2 && scenarioPath[0] == L'\\' && scenarioPath[1] == L'\\'))
    {
        scenarioPath = CombinePath(GetDirectoryPath(projectPath), scenarioPath);
    }

    if (GetFileAttributesW(scenarioPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        statusText_ = L"プロジェクトのシナリオが見つかりません: " + scenarioPath;
        return false;
    }

    LoadScenario(scenarioPath);
    projectPath_ = projectPath;
    LoadProjectSettings(projectPath_);
    RefreshSceneList();
    RefreshAssetList();
    LoadToolbarIcons();
    LoadUiButtonIcons();
    AddRecentProject(projectPath);
    projectLauncherVisible_ = false;
    MarkCurrentStateSaved();
    statusText_ = L"プロジェクトを読み込みました: " + GetFileNamePart(projectPath);
    return true;
}

bool NovelRuntime::ExportBuild()
{
    if (scenarioPath_.empty() && !SaveProjectAs())
    {
        return false;
    }
    if (!SaveProject())
    {
        return false;
    }

    BROWSEINFOW browseInfo = {};
    browseInfo.hwndOwner = hostWindow_;
    browseInfo.lpszTitle = L"\u66f8\u304d\u51fa\u3057\u5148\u30d5\u30a9\u30eb\u30c0\u3092\u9078\u629e";
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browseInfo);
    if (!pidl)
    {
        statusText_ = L"\u66f8\u304d\u51fa\u3057\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    WCHAR selectedPath[MAX_PATH] = {};
    const bool pathOk = SHGetPathFromIDListW(pidl, selectedPath) == TRUE;
    CoTaskMemFree(pidl);
    if (!pathOk)
    {
        statusText_ = L"\u66f8\u304d\u51fa\u3057\u5148\u3092\u53d6\u5f97\u3067\u304d\u307e\u305b\u3093";
        return false;
    }

    const std::wstring exportRoot = std::wstring(selectedPath) + L"\\" + GetFileStemPart(scenarioPath_) + L"_export";
    const std::wstring exportAssetsDir = exportRoot + L"\\assets";
    if (!EnsureDirectoryExists(exportRoot) || !EnsureDirectoryExists(exportAssetsDir))
    {
        statusText_ = L"\u66f8\u304d\u51fa\u3057\u5148\u306e\u4f5c\u6210\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    WCHAR modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::wstring runtimeExe = modulePath;
    const std::wstring exportExe = exportRoot + L"\\kaktosPlayer.exe";
    if (!CopyFileW(runtimeExe.c_str(), exportExe.c_str(), FALSE))
    {
        statusText_ = L"\u5b9f\u884c\u30d5\u30a1\u30a4\u30eb\u306e\u66f8\u304d\u51fa\u3057\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    const std::wstring sourceAssetsDir = GetAssetsRootDirectory();
    if (!CopyDirectoryTree(sourceAssetsDir, exportAssetsDir))
    {
        statusText_ = L"\u30a2\u30bb\u30c3\u30c8\u306e\u66f8\u304d\u51fa\u3057\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    const std::wstring sourceUiDir = sourceAssetsDir + L"\\ui";
    if (GetFileAttributesW(sourceUiDir.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CopyDirectoryTree(sourceUiDir, exportAssetsDir + L"\\ui");
    }

    const std::wstring exportScenarioPath = exportAssetsDir + L"\\main.ks";
    if (!CopyFileW(scenarioPath_.c_str(), exportScenarioPath.c_str(), TRUE))
    {
        statusText_ = L"\u30b7\u30ca\u30ea\u30aa\u306e\u66f8\u304d\u51fa\u3057\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    if (!projectPath_.empty())
    {
        std::wstring projectText;
        if (TryReadTextFile(projectPath_, projectText))
        {
            std::wistringstream input(projectText);
            std::wstring line;
            std::wstring normalizedProjectText;
            bool wroteScenarioPath = false;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }
                const size_t split = line.find(L'=');
                if (split != std::wstring::npos && Trim(line.substr(0, split)) == L"scenario_path")
                {
                    normalizedProjectText += L"scenario_path=main.ks\r\n";
                    wroteScenarioPath = true;
                }
                else
                {
                    normalizedProjectText += line + L"\r\n";
                }
            }
            if (!wroteScenarioPath)
            {
                normalizedProjectText = L"scenario_path=main.ks\r\n" + normalizedProjectText;
            }
            TryWriteTextFile(exportAssetsDir + L"\\project.kproj", normalizedProjectText);
        }
    }

    const std::wstring readmePath = exportRoot + L"\\README.txt";
    std::wstring readme;
    readme += L"Kaktos Engine Export\r\n";
    readme += L"1. kaktosPlayer.exe \u3092\u8d77\u52d5\r\n";
    readme += L"2. assets\\main.ks \u3092\u8aad\u307f\u8fbc\u3093\u3067\u518d\u751f\r\n";
    readme += L"3. assets \u30d5\u30a9\u30eb\u30c0\u306b\u7d20\u6750\u3068 project.kproj \u3092\u540c\u68b1\r\n";
    TryWriteTextFile(readmePath, readme);

    statusText_ = L"\u66f8\u304d\u51fa\u3057\u5b8c\u4e86: " + exportRoot;
    return true;
}

std::wstring NovelRuntime::EscapeSaveValue(const std::wstring& value) const
{
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            escaped += L"\\\\";
        }
        else if (ch == L'\r')
        {
            escaped += L"\\r";
        }
        else if (ch == L'\n')
        {
            escaped += L"\\n";
        }
        else if (ch == L'=')
        {
            escaped += L"\\e";
        }
        else
        {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

std::wstring NovelRuntime::UnescapeSaveValue(const std::wstring& value) const
{
    std::wstring unescaped;
    unescaped.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == L'\\' && i + 1 < value.size())
        {
            const wchar_t next = value[i + 1];
            if (next == L'\\')
            {
                unescaped.push_back(L'\\');
                ++i;
                continue;
            }
            if (next == L'r')
            {
                unescaped.push_back(L'\r');
                ++i;
                continue;
            }
            if (next == L'n')
            {
                unescaped.push_back(L'\n');
                ++i;
                continue;
            }
            if (next == L'e')
            {
                unescaped.push_back(L'=');
                ++i;
                continue;
            }
        }
        unescaped.push_back(value[i]);
    }
    return unescaped;
}

std::wstring NovelRuntime::GetAutosavePath() const
{
    return CombinePath(GetAssetsRootDirectory(), L".editor_autosave.kauto");
}

std::wstring NovelRuntime::GetQuickSavePath() const
{
    const std::wstring root = editorSettings_.saveDirectory.empty() ? GetAssetsRootDirectory() : editorSettings_.saveDirectory;
    EnsureDirectoryExists(root);
    return CombinePath(root, L"quicksave.ksav");
}

void NovelRuntime::SaveAutosaveSnapshot()
{
    if (!editorSettings_.autosaveEnabled || scenarioPath_.empty())
    {
        return;
    }

    std::wstring text;
    text += L"scenario_path=" + EscapeSaveValue(scenarioPath_) + L"\r\n";
    text += L"selected_command_index=" + std::to_wstring(selectedCommandIndex_) + L"\r\n";
    text += L"project_text=" + EscapeSaveValue(SerializeProjectSettings()) + L"\r\n";
    text += L"scenario_text=" + EscapeSaveValue(SerializeScenario(scenario_)) + L"\r\n";
    TryWriteTextFile(GetAutosavePath(), text);
}

void NovelRuntime::DeleteAutosaveSnapshot()
{
    const std::wstring autosavePath = GetAutosavePath();
    if (!autosavePath.empty())
    {
        DeleteFileW(autosavePath.c_str());
    }
}

bool NovelRuntime::RestoreAutosaveSnapshot(bool notifyOnMissing)
{
    std::wstring content;
    if (!TryReadTextFile(GetAutosavePath(), content))
    {
        if (notifyOnMissing)
        {
            statusText_ = L"復元できるオートセーブはありません";
        }
        return false;
    }

    std::unordered_map<std::wstring, std::wstring> values;
    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        const size_t split = line.find(L'=');
        if (split == std::wstring::npos)
        {
            continue;
        }
        values[Trim(line.substr(0, split))] = UnescapeSaveValue(line.substr(split + 1));
    }

    const auto scenarioPathIt = values.find(L"scenario_path");
    const auto scenarioTextIt = values.find(L"scenario_text");
    if (scenarioPathIt == values.end() || scenarioTextIt == values.end())
    {
        statusText_ = L"オートセーブの形式が壊れています";
        return false;
    }

    LoadScenario(scenarioPathIt->second);
    ScenarioDocument document;
    std::wstring errorMessage;
    if (!ParseScenario(scenarioTextIt->second, document, errorMessage))
    {
        statusText_ = L"オートセーブのシナリオ復元に失敗しました";
        return false;
    }
    scenario_ = std::move(document);
    selectedCommandIndex_ = values.count(L"selected_command_index") ? static_cast<size_t>(_wtoi(values[L"selected_command_index"].c_str())) : 0;
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        selectedCommandIndex_ = scenario_.commands.empty() ? 0 : scenario_.commands.size() - 1;
    }
    statusText_ = L"オートセーブから復元しました";
    RefreshPreviewIfActive();
    return true;
}

void NovelRuntime::ApplyEditorUiDefaults()
{
    messageWindowVisible_ = editorSettings_.defaultMessageWindowVisible;
    messageWindowColor_ = editorSettings_.defaultMessageWindowColor;
    messageWindowBorderColor_ = editorSettings_.defaultMessageWindowBorderColor;
    messageWindowOpacity_ = editorSettings_.defaultMessageWindowOpacity;
    messageWindowPadding_ = editorSettings_.defaultMessageWindowPadding;
    messageWindowImagePath_ = editorSettings_.defaultMessageWindowImage.empty() ? FindDefaultTextboxImagePath() : editorSettings_.defaultMessageWindowImage;
    messageWindowImage_.reset();
    if (!messageWindowImagePath_.empty())
    {
        messageWindowImage_ = TryLoadImage(messageWindowImagePath_);
        if (!messageWindowImage_)
        {
            messageWindowImage_ = TryLoadImage(CombinePath(scenarioBaseDir_, messageWindowImagePath_));
        }
    }

    nameBoxVisible_ = editorSettings_.defaultNameWindowVisible;
    nameWindowColor_ = editorSettings_.defaultNameWindowColor;
    nameWindowBorderColor_ = editorSettings_.defaultNameWindowBorderColor;
    nameWindowOpacity_ = editorSettings_.defaultNameWindowOpacity;
    nameWindowOffsetX_ = editorSettings_.defaultNameWindowOffsetX;
    nameWindowOffsetY_ = editorSettings_.defaultNameWindowOffsetY;
    nameWindowWidth_ = editorSettings_.defaultNameWindowWidth;
    nameWindowHeight_ = editorSettings_.defaultNameWindowHeight;
    nameWindowPadding_ = editorSettings_.defaultNameWindowPadding;
    nameWindowImagePath_ = editorSettings_.defaultNameWindowImage;
    nameWindowImage_.reset();
    if (!nameWindowImagePath_.empty())
    {
        nameWindowImage_ = TryLoadImage(nameWindowImagePath_);
        if (!nameWindowImage_)
        {
            nameWindowImage_ = TryLoadImage(CombinePath(scenarioBaseDir_, nameWindowImagePath_));
        }
    }
}

std::wstring NovelRuntime::MakeProjectRelativeAssetPath(const std::wstring& fullPath) const
{
    if (fullPath.empty())
    {
        return fullPath;
    }

    std::wstring normalized = fullPath;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    std::wstring assetsRoot = GetAssetsRootDirectory();
    std::replace(assetsRoot.begin(), assetsRoot.end(), L'/', L'\\');
    if (StartsWithText(normalized, assetsRoot + L"\\"))
    {
        return normalized.substr(assetsRoot.size() + 1);
    }
    return fullPath;
}

std::wstring NovelRuntime::FindDefaultTextboxImagePath() const
{
    const std::wstring textboxDir = CombinePath(GetAssetsRootDirectory(), L"textbox");
    WIN32_FIND_DATAW findData = {};
    HANDLE handle = FindFirstFileW((textboxDir + L"\\*").c_str(), &findData);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return L"";
    }

    std::wstring firstMatch;
    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            continue;
        }

        std::wstring name = findData.cFileName;
        std::wstring lower = name;
        CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
        const auto hasExt = [&](const wchar_t* ext)
        {
            const size_t extLen = wcslen(ext);
            return lower.size() >= extLen && lower.compare(lower.size() - extLen, extLen, ext) == 0;
        };
        if (hasExt(L".png") || hasExt(L".jpg") || hasExt(L".jpeg") || hasExt(L".bmp"))
        {
            firstMatch = L"textbox\\" + name;
            break;
        }
    } while (FindNextFileW(handle, &findData));
    FindClose(handle);

    return firstMatch;
}

std::wstring NovelRuntime::BrowseForFolder(const std::wstring& title, const std::wstring& initialPath) const
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = hostWindow_;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
    {
        return initialPath;
    }

    WCHAR path[MAX_PATH] = {};
    std::wstring result = initialPath;
    if (SHGetPathFromIDListW(pidl, path))
    {
        result = path;
    }
    CoTaskMemFree(pidl);
    return result;
}

bool NovelRuntime::SaveRuntimeStateToPath(const std::wstring& savePath)
{
    std::wstring saveText;
    saveText += L"scenario_path=" + EscapeSaveValue(scenarioPath_) + L"\r\n";
    saveText += L"story_title=" + EscapeSaveValue(storyTitle_) + L"\r\n";
    saveText += L"speaker=" + EscapeSaveValue(speakerName_) + L"\r\n";
    saveText += L"name_visible=" + std::to_wstring(nameBoxVisible_ ? 1 : 0) + L"\r\n";
    saveText += L"name_x=" + std::to_wstring(nameWindowOffsetX_) + L"\r\n";
    saveText += L"name_y=" + std::to_wstring(nameWindowOffsetY_) + L"\r\n";
    saveText += L"name_width=" + std::to_wstring(nameWindowWidth_) + L"\r\n";
    saveText += L"name_height=" + std::to_wstring(nameWindowHeight_) + L"\r\n";
    saveText += L"name_padding=" + std::to_wstring(nameWindowPadding_) + L"\r\n";
    saveText += L"name_opacity=" + std::to_wstring(nameWindowOpacity_) + L"\r\n";
    saveText += L"name_color=" + std::to_wstring(static_cast<unsigned int>(nameWindowColor_)) + L"\r\n";
    saveText += L"name_border=" + std::to_wstring(static_cast<unsigned int>(nameWindowBorderColor_)) + L"\r\n";
    saveText += L"name_image=" + EscapeSaveValue(nameWindowImagePath_) + L"\r\n";
    saveText += L"current_text=" + EscapeSaveValue(currentText_) + L"\r\n";
    saveText += L"displayed_text=" + EscapeSaveValue(displayedText_) + L"\r\n";
    saveText += L"status_text=" + EscapeSaveValue(statusText_) + L"\r\n";
    saveText += L"background_color=" + std::to_wstring(static_cast<unsigned int>(backgroundColor_)) + L"\r\n";
    saveText += L"background_path=" + EscapeSaveValue(backgroundPath_) + L"\r\n";
    saveText += L"background_name=" + EscapeSaveValue(backgroundDisplayName_) + L"\r\n";
    saveText += L"background_visible=" + std::to_wstring(backgroundVisible_ ? 1 : 0) + L"\r\n";
    saveText += L"background_x=" + std::to_wstring(backgroundOffsetX_) + L"\r\n";
    saveText += L"background_y=" + std::to_wstring(backgroundOffsetY_) + L"\r\n";
    saveText += L"background_scale=" + std::to_wstring(backgroundScale_) + L"\r\n";
    saveText += L"background_opacity=" + std::to_wstring(backgroundOpacity_) + L"\r\n";
    saveText += L"current_command_index=" + std::to_wstring(currentCommandIndex_) + L"\r\n";
    saveText += L"selected_command_index=" + std::to_wstring(selectedCommandIndex_) + L"\r\n";
    saveText += L"selected_choice_link_index=" + std::to_wstring(selectedChoiceLinkIndex_) + L"\r\n";
    saveText += L"waiting_for_choice=" + std::to_wstring(waitingForChoice_ ? 1 : 0) + L"\r\n";
    saveText += L"reached_end=" + std::to_wstring(reachedEnd_ ? 1 : 0) + L"\r\n";
    saveText += L"text_reveal_active=" + std::to_wstring(textRevealActive_ ? 1 : 0) + L"\r\n";
    saveText += L"text_reveal_index=" + std::to_wstring(textRevealIndex_) + L"\r\n";
    saveText += L"left_name=" + EscapeSaveValue(leftCharacter_.displayName) + L"\r\n";
    saveText += L"left_image=" + EscapeSaveValue(leftCharacter_.imagePath) + L"\r\n";
    saveText += L"left_visible=" + std::to_wstring(leftCharacter_.visible ? 1 : 0) + L"\r\n";
    saveText += L"left_x=" + std::to_wstring(leftCharacter_.offsetX) + L"\r\n";
    saveText += L"left_y=" + std::to_wstring(leftCharacter_.offsetY) + L"\r\n";
    saveText += L"left_scale=" + std::to_wstring(leftCharacter_.scale) + L"\r\n";
    saveText += L"left_opacity=" + std::to_wstring(leftCharacter_.opacity) + L"\r\n";
    saveText += L"center_name=" + EscapeSaveValue(centerCharacter_.displayName) + L"\r\n";
    saveText += L"center_image=" + EscapeSaveValue(centerCharacter_.imagePath) + L"\r\n";
    saveText += L"center_visible=" + std::to_wstring(centerCharacter_.visible ? 1 : 0) + L"\r\n";
    saveText += L"center_x=" + std::to_wstring(centerCharacter_.offsetX) + L"\r\n";
    saveText += L"center_y=" + std::to_wstring(centerCharacter_.offsetY) + L"\r\n";
    saveText += L"center_scale=" + std::to_wstring(centerCharacter_.scale) + L"\r\n";
    saveText += L"center_opacity=" + std::to_wstring(centerCharacter_.opacity) + L"\r\n";
    saveText += L"right_name=" + EscapeSaveValue(rightCharacter_.displayName) + L"\r\n";
    saveText += L"right_image=" + EscapeSaveValue(rightCharacter_.imagePath) + L"\r\n";
    saveText += L"right_visible=" + std::to_wstring(rightCharacter_.visible ? 1 : 0) + L"\r\n";
    saveText += L"right_x=" + std::to_wstring(rightCharacter_.offsetX) + L"\r\n";
    saveText += L"right_y=" + std::to_wstring(rightCharacter_.offsetY) + L"\r\n";
    saveText += L"right_scale=" + std::to_wstring(rightCharacter_.scale) + L"\r\n";
    saveText += L"right_opacity=" + std::to_wstring(rightCharacter_.opacity) + L"\r\n";
    for (const auto& variable : variables_)
    {
        saveText += L"var." + EscapeSaveValue(variable.first) + L"=" + EscapeSaveValue(variable.second) + L"\r\n";
    }
    for (size_t i = 0; i < activeChoices_.size(); ++i)
    {
        saveText += L"choice." + std::to_wstring(i) + L".text=" + EscapeSaveValue(activeChoices_[i].text) + L"\r\n";
        saveText += L"choice." + std::to_wstring(i) + L".target=" + EscapeSaveValue(activeChoices_[i].target) + L"\r\n";
        saveText += L"choice." + std::to_wstring(i) + L".enabled=" + std::wstring(activeChoices_[i].enabled ? L"1" : L"0") + L"\r\n";
    }

    if (!TryWriteTextFile(savePath, saveText))
    {
        statusText_ = L"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u306e\u4fdd\u5b58\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    statusText_ = L"\u30bb\u30fc\u30d6\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::SaveRuntimeStateAs()
{
    WCHAR fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, L"save01.ksav", _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Save (*.ksav)\0*.ksav\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ksav";
    ofn.lpstrInitialDir = editorSettings_.saveDirectory.empty() ? nullptr : editorSettings_.saveDirectory.c_str();
    if (!GetSaveFileNameW(&ofn))
    {
        statusText_ = L"\u30bb\u30fc\u30d6\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    return SaveRuntimeStateToPath(fileBuffer);
}

void NovelRuntime::ApplyLoadedCharacterState(CharacterSlot& slot, const std::wstring& slotName, const std::wstring& displayName, const std::wstring& imagePath)
{
    slot = { slotName };
    slot.displayName = displayName;
    slot.imagePath = imagePath;
    if (!imagePath.empty() && imagePath != L"solid")
    {
        auto image = TryLoadImage(imagePath);
        if (!image)
        {
            image = TryLoadImage(CombinePath(scenarioBaseDir_, imagePath));
        }
        slot.image = std::move(image);
    }
}

bool NovelRuntime::LoadRuntimeStateFromPath(const std::wstring& savePath)
{
    std::wstring content;
    if (!TryReadTextFile(savePath, content))
    {
        statusText_ = L"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3092\u958b\u3051\u307e\u305b\u3093";
        return false;
    }

    std::unordered_map<std::wstring, std::wstring> values;
    std::vector<std::pair<std::wstring, std::wstring>> savedChoices;
    std::wistringstream input(content);
    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        const size_t split = line.find(L'=');
        if (split == std::wstring::npos)
        {
            continue;
        }
        values[Trim(line.substr(0, split))] = UnescapeSaveValue(line.substr(split + 1));
    }

    const auto scenarioPathIt = values.find(L"scenario_path");
    if (scenarioPathIt == values.end() || scenarioPathIt->second.empty())
    {
        statusText_ = L"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u304c\u58ca\u308c\u3066\u3044\u307e\u3059";
        return false;
    }

    LoadScenario(scenarioPathIt->second);

    auto getValue = [&](const std::wstring& key) -> std::wstring
    {
        const auto found = values.find(key);
        return found == values.end() ? L"" : found->second;
    };

    storyTitle_ = getValue(L"story_title").empty() ? storyTitle_ : getValue(L"story_title");
    speakerName_ = getValue(L"speaker");
    nameBoxVisible_ = getValue(L"name_visible").empty() ? nameBoxVisible_ : (getValue(L"name_visible") != L"0");
    nameWindowOffsetX_ = ParseIntValue(getValue(L"name_x"), nameWindowOffsetX_);
    nameWindowOffsetY_ = ParseIntValue(getValue(L"name_y"), nameWindowOffsetY_);
    nameWindowWidth_ = (std::max)(120, ParseIntValue(getValue(L"name_width"), nameWindowWidth_));
    nameWindowHeight_ = (std::max)(28, ParseIntValue(getValue(L"name_height"), nameWindowHeight_));
    nameWindowPadding_ = (std::max)(4, ParseIntValue(getValue(L"name_padding"), nameWindowPadding_));
    nameWindowOpacity_ = ClampByteValue(ParseIntValue(getValue(L"name_opacity"), nameWindowOpacity_));
    if (!getValue(L"name_color").empty())
    {
        nameWindowColor_ = static_cast<COLORREF>(_wtoi(getValue(L"name_color").c_str()));
    }
    if (!getValue(L"name_border").empty())
    {
        nameWindowBorderColor_ = static_cast<COLORREF>(_wtoi(getValue(L"name_border").c_str()));
    }
    nameWindowImagePath_ = getValue(L"name_image");
    nameWindowImage_.reset();
    if (!nameWindowImagePath_.empty())
    {
        nameWindowImage_ = TryLoadImage(nameWindowImagePath_);
        if (!nameWindowImage_)
        {
            nameWindowImage_ = TryLoadImage(CombinePath(scenarioBaseDir_, nameWindowImagePath_));
        }
    }
    currentText_ = getValue(L"current_text");
    displayedText_ = getValue(L"displayed_text");
    statusText_ = getValue(L"status_text");
    backgroundColor_ = static_cast<COLORREF>(_wtoi(getValue(L"background_color").c_str()));
    backgroundPath_ = getValue(L"background_path");
    backgroundDisplayName_ = getValue(L"background_name");
    backgroundVisible_ = getValue(L"background_visible") != L"0";
    backgroundOffsetX_ = _wtoi(getValue(L"background_x").c_str());
    backgroundOffsetY_ = _wtoi(getValue(L"background_y").c_str());
    backgroundScale_ = (std::max)(1, _wtoi(getValue(L"background_scale").c_str()));
    if (backgroundScale_ == 1 && getValue(L"background_scale").empty())
    {
        backgroundScale_ = 100;
    }
    backgroundOpacity_ = ClampByteValue(_wtoi(getValue(L"background_opacity").c_str()));
    if (getValue(L"background_opacity").empty())
    {
        backgroundOpacity_ = 255;
    }
    backgroundImage_.reset();
    if (!backgroundPath_.empty() && backgroundPath_ != L"solid")
    {
        backgroundImage_ = TryLoadImage(backgroundPath_);
        if (!backgroundImage_)
        {
            backgroundImage_ = TryLoadImage(CombinePath(scenarioBaseDir_, backgroundPath_));
        }
    }

    currentCommandIndex_ = static_cast<size_t>(_wtoi(getValue(L"current_command_index").c_str()));
    selectedCommandIndex_ = static_cast<size_t>(_wtoi(getValue(L"selected_command_index").c_str()));
    selectedChoiceLinkIndex_ = static_cast<size_t>(_wtoi(getValue(L"selected_choice_link_index").c_str()));
    waitingForChoice_ = getValue(L"waiting_for_choice") == L"1";
    reachedEnd_ = getValue(L"reached_end") == L"1";
    textRevealActive_ = getValue(L"text_reveal_active") == L"1";
    textRevealIndex_ = static_cast<size_t>(_wtoi(getValue(L"text_reveal_index").c_str()));
    if (displayedText_.empty() && !currentText_.empty())
    {
        displayedText_ = textRevealActive_ ? currentText_.substr(0, (std::min)(textRevealIndex_, currentText_.size())) : currentText_;
    }
    if (!textRevealActive_)
    {
        textRevealIndex_ = displayedText_.size();
        nextTextRevealTick_ = 0;
    }
    else
    {
        nextTextRevealTick_ = GetTickCount() + static_cast<DWORD>((std::max)(0, textSpeedMs_));
    }

    variables_.clear();
    for (const auto& entry : values)
    {
        if (StartsWithText(entry.first, L"var."))
        {
            variables_[entry.first.substr(4)] = entry.second;
        }
    }

    activeChoices_.clear();
    for (size_t i = 0;; ++i)
    {
        const std::wstring text = getValue(L"choice." + std::to_wstring(i) + L".text");
        const std::wstring target = getValue(L"choice." + std::to_wstring(i) + L".target");
        if (text.empty() && target.empty())
        {
            break;
        }
        const bool enabled = getValue(L"choice." + std::to_wstring(i) + L".enabled") != L"0";
        activeChoices_.push_back({ text, target, enabled });
    }
    activeChoiceRects_.assign(activeChoices_.size(), RECT{});

    ApplyLoadedCharacterState(leftCharacter_, L"left", getValue(L"left_name"), getValue(L"left_image"));
    ApplyLoadedCharacterState(centerCharacter_, L"center", getValue(L"center_name"), getValue(L"center_image"));
    ApplyLoadedCharacterState(rightCharacter_, L"right", getValue(L"right_name"), getValue(L"right_image"));
    leftCharacter_.visible = getValue(L"left_visible") != L"0";
    leftCharacter_.offsetX = _wtoi(getValue(L"left_x").c_str());
    leftCharacter_.offsetY = _wtoi(getValue(L"left_y").c_str());
    leftCharacter_.scale = getValue(L"left_scale").empty() ? 100 : (std::max)(1, _wtoi(getValue(L"left_scale").c_str()));
    leftCharacter_.opacity = getValue(L"left_opacity").empty() ? 255 : ClampByteValue(_wtoi(getValue(L"left_opacity").c_str()));
    centerCharacter_.visible = getValue(L"center_visible") != L"0";
    centerCharacter_.offsetX = _wtoi(getValue(L"center_x").c_str());
    centerCharacter_.offsetY = _wtoi(getValue(L"center_y").c_str());
    centerCharacter_.scale = getValue(L"center_scale").empty() ? 100 : (std::max)(1, _wtoi(getValue(L"center_scale").c_str()));
    centerCharacter_.opacity = getValue(L"center_opacity").empty() ? 255 : ClampByteValue(_wtoi(getValue(L"center_opacity").c_str()));
    rightCharacter_.visible = getValue(L"right_visible") != L"0";
    rightCharacter_.offsetX = _wtoi(getValue(L"right_x").c_str());
    rightCharacter_.offsetY = _wtoi(getValue(L"right_y").c_str());
    rightCharacter_.scale = getValue(L"right_scale").empty() ? 100 : (std::max)(1, _wtoi(getValue(L"right_scale").c_str()));
    rightCharacter_.opacity = getValue(L"right_opacity").empty() ? 255 : ClampByteValue(_wtoi(getValue(L"right_opacity").c_str()));

    if (currentCommandIndex_ > scenario_.commands.size())
    {
        currentCommandIndex_ = scenario_.commands.size();
    }
    if (selectedCommandIndex_ >= scenario_.commands.size())
    {
        selectedCommandIndex_ = scenario_.commands.empty() ? 0 : scenario_.commands.size() - 1;
    }
    RefreshPreviewWindow();
    statusText_ = L"\u30ed\u30fc\u30c9\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::LoadRuntimeStateFromDialog()
{
    WCHAR fileBuffer[MAX_PATH] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Save (*.ksav)\0*.ksav\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ksav";
    ofn.lpstrInitialDir = editorSettings_.saveDirectory.empty() ? nullptr : editorSettings_.saveDirectory.c_str();
    if (!GetOpenFileNameW(&ofn))
    {
        statusText_ = L"\u30ed\u30fc\u30c9\u3092\u30ad\u30e3\u30f3\u30bb\u30eb\u3057\u307e\u3057\u305f";
        return false;
    }

    return LoadRuntimeStateFromPath(fileBuffer);
}

std::wstring NovelRuntime::GetAssetsRootDirectory() const
{
    const auto isExistingDirectory = [](const std::wstring& path) -> bool
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    };

    if (!scenarioBaseDir_.empty())
    {
        const std::wstring baseName = GetFileNamePart(scenarioBaseDir_);
        if (baseName == L"scenario")
        {
            const std::wstring parent = GetDirectoryPath(scenarioBaseDir_);
            if (!parent.empty() && isExistingDirectory(parent))
            {
                return parent;
            }
        }
        if (baseName == L"assets" && isExistingDirectory(scenarioBaseDir_))
        {
            return scenarioBaseDir_;
        }
    }

    WCHAR modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
    {
        std::wstring executablePath = modulePath;
        const size_t lastSeparator = executablePath.find_last_of(L"\\/");
        const std::wstring executableDir = lastSeparator == std::wstring::npos ? L"." : executablePath.substr(0, lastSeparator);
        const std::wstring candidates[] =
        {
            executableDir + L"\\assets",
            executableDir + L"\\..\\assets",
            executableDir + L"\\..\\..\\assets",
            executableDir + L"\\..\\..\\..\\assets",
            L"assets",
            L"..\\assets",
            L"..\\..\\assets",
        };

        for (const std::wstring& candidate : candidates)
        {
            if (isExistingDirectory(candidate))
            {
                return candidate;
            }
        }
    }

    return L"assets";
}

std::wstring NovelRuntime::GetScenarioDirectory() const
{
    return CombinePath(GetAssetsRootDirectory(), L"scenario");
}

std::wstring NovelRuntime::ResolveMaterialIconPath(const std::wstring& baseRelativePath) const
{
    std::vector<std::wstring> candidates;
    const std::wstring assetsRoot = GetAssetsRootDirectory();
    candidates.push_back(CombinePath(assetsRoot, baseRelativePath));

    const size_t dot = baseRelativePath.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? baseRelativePath : baseRelativePath.substr(0, dot);
    candidates.push_back(CombinePath(assetsRoot, stem + L".png"));
    candidates.push_back(CombinePath(assetsRoot, stem + L".jpg"));
    candidates.push_back(CombinePath(assetsRoot, stem + L".jpeg"));
    candidates.push_back(CombinePath(assetsRoot, stem + L".bmp"));

    const std::wstring fileName = GetFileNamePart(stem);
    if (!fileName.empty())
    {
        const std::wstring uiDir = CombinePath(assetsRoot, L"ui");
        candidates.push_back(CombinePath(uiDir, fileName + L".png"));
        candidates.push_back(CombinePath(uiDir, fileName + L".jpg"));
        candidates.push_back(CombinePath(uiDir, fileName + L".jpeg"));
        candidates.push_back(CombinePath(uiDir, fileName + L".bmp"));
    }

    for (const std::wstring& candidate : candidates)
    {
        const DWORD attr = GetFileAttributesW(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            return candidate;
        }
    }

    return L"";
}

void NovelRuntime::RefreshSceneList()
{
    sceneItems_.clear();
    const std::wstring directory = GetScenarioDirectory();
    for (const std::wstring& path : EnumerateFiles(directory, L"*.ks"))
    {
        sceneItems_.push_back(SceneListItem{ path, GetFileStemPart(path), {} });
    }
    if (selectedScenePath_.empty() || std::none_of(sceneItems_.begin(), sceneItems_.end(), [&](const SceneListItem& item) { return item.path == selectedScenePath_; }))
    {
        selectedScenePath_ = scenarioPath_;
    }
}

void NovelRuntime::RefreshAssetList()
{
    RefreshAvailableFonts();
    assetItems_.clear();
    const std::wstring baseAssetsDir = GetAssetsRootDirectory();
    const struct
    {
        const wchar_t* category;
        const wchar_t* subdir;
    } assetDirs[] =
    {
        { L"background", L"background" },
        { L"picture", L"picture" },
        { L"character", L"character" },
        { L"se", L"se" },
        { L"bgm", L"bgm" },
    };

    for (const auto& assetDir : assetDirs)
    {
        const std::wstring dir = CombinePath(baseAssetsDir, assetDir.subdir);
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        assetItems_.push_back(AssetListItem{ assetDir.category, dir, GetFileNamePart(dir), L"", 0, true, {} });

        std::function<void(const std::wstring&, const std::wstring&, int)> appendTree =
            [&](const std::wstring& currentDir, const std::wstring& relativeRoot, int depth)
        {
            std::vector<std::wstring> directories;
            std::vector<std::wstring> files;

            WIN32_FIND_DATAW data = {};
            HANDLE findHandle = FindFirstFileW((currentDir + L"\\*").c_str(), &data);
            if (findHandle == INVALID_HANDLE_VALUE)
            {
                return;
            }

            do
            {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..")
                {
                    continue;
                }

                const std::wstring fullPath = currentDir + L"\\" + name;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    directories.push_back(fullPath);
                }
                else
                {
                    files.push_back(fullPath);
                }
            } while (FindNextFileW(findHandle, &data));
            FindClose(findHandle);

            std::sort(directories.begin(), directories.end());
            std::sort(files.begin(), files.end());

            for (const std::wstring& childDir : directories)
            {
                const std::wstring childName = GetFileNamePart(childDir);
                const std::wstring childRelative = relativeRoot.empty() ? childName : relativeRoot + L"\\" + childName;
                assetItems_.push_back(AssetListItem{ assetDir.category, childDir, childName, childRelative, depth, true, {} });
                appendTree(childDir, childRelative, depth + 1);
            }

            for (const std::wstring& filePath : files)
            {
                const std::wstring fileName = GetFileNamePart(filePath);
                const std::wstring fileRelative = relativeRoot.empty() ? fileName : relativeRoot + L"\\" + fileName;
                assetItems_.push_back(AssetListItem{ assetDir.category, filePath, fileName, fileRelative, depth, false, {} });
            }
        };

        appendTree(dir, L"", 1);
    }
}

bool NovelRuntime::AddMaterialFile()
{
    struct MaterialDirectory
    {
        const wchar_t* category;
        const wchar_t* subdir;
        const wchar_t* filter;
    };

    static const MaterialDirectory directories[] =
    {
        { L"background", L"background", L"画像ファイル (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0すべてのファイル (*.*)\0*.*\0" },
        { L"picture", L"picture", L"画像ファイル (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0すべてのファイル (*.*)\0*.*\0" },
        { L"character", L"character", L"画像ファイル (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0すべてのファイル (*.*)\0*.*\0" },
        { L"se", L"se", L"音声ファイル (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0すべてのファイル (*.*)\0*.*\0" },
        { L"bgm", L"bgm", L"音声ファイル (*.mp3;*.ogg;*.wav)\0*.mp3;*.ogg;*.wav\0すべてのファイル (*.*)\0*.*\0" },
    };

    const MaterialDirectory* selectedDirectory = nullptr;
    for (const auto& directory : directories)
    {
        if (selectedAssetCategory_ == directory.category)
        {
            selectedDirectory = &directory;
            break;
        }
    }

    if (!selectedDirectory)
    {
        statusText_ = L"追加先の素材カテゴリが見つかりません";
        return false;
    }

    WCHAR fileBuffer[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = selectedDirectory->filter;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn))
    {
        statusText_ = L"素材追加をキャンセルしました";
        return false;
    }

    const std::wstring targetDirectory = CombinePath(GetAssetsRootDirectory(), selectedDirectory->subdir);
    if (!EnsureDirectoryExists(targetDirectory))
    {
        statusText_ = L"素材フォルダの作成に失敗しました";
        return false;
    }

    const std::wstring targetPath = CombinePath(targetDirectory, GetFileNamePart(fileBuffer));
    if (!CopyFileW(fileBuffer, targetPath.c_str(), FALSE))
    {
        if (GetLastError() == ERROR_FILE_EXISTS)
        {
            statusText_ = L"同名ファイルが既にあります";
        }
        else
        {
            statusText_ = L"素材ファイルの追加に失敗しました";
        }
        return false;
    }

    RefreshAssetList();
    statusText_ = L"素材ファイルを追加しました";
    return true;
}

bool NovelRuntime::ImportMaterialFiles(const std::vector<std::wstring>& paths, const std::wstring& category)
{
    struct MaterialDirectory
    {
        const wchar_t* category;
        const wchar_t* subdir;
        const wchar_t* extensions;
    };

    static const MaterialDirectory directories[] =
    {
        { L"background", L"background", L".png;.jpg;.jpeg;.bmp" },
        { L"picture", L"picture", L".png;.jpg;.jpeg;.bmp" },
        { L"character", L"character", L".png;.jpg;.jpeg;.bmp" },
        { L"se", L"se", L".wav;.ogg;.mp3" },
        { L"bgm", L"bgm", L".wav;.ogg;.mp3" },
    };

    const MaterialDirectory* selectedDirectory = nullptr;
    for (const auto& directory : directories)
    {
        if (category == directory.category)
        {
            selectedDirectory = &directory;
            break;
        }
    }
    if (!selectedDirectory)
    {
        statusText_ = L"追加先カテゴリが不正です";
        return false;
    }

    const std::wstring targetDirectory = CombinePath(GetAssetsRootDirectory(), selectedDirectory->subdir);
    if (!EnsureDirectoryExists(targetDirectory))
    {
        statusText_ = L"素材フォルダの作成に失敗しました";
        return false;
    }

    int importedCount = 0;
    int skippedCount = 0;
    std::wstring extensionList = selectedDirectory->extensions;
    CharLowerBuffW(&extensionList[0], static_cast<DWORD>(extensionList.size()));

    for (const std::wstring& sourcePath : paths)
    {
        const DWORD attr = GetFileAttributesW(sourcePath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ++skippedCount;
            continue;
        }

        const size_t dot = sourcePath.find_last_of(L'.');
        if (dot == std::wstring::npos)
        {
            ++skippedCount;
            continue;
        }
        std::wstring extension = sourcePath.substr(dot);
        CharLowerBuffW(&extension[0], static_cast<DWORD>(extension.size()));
        if (extensionList.find(extension) == std::wstring::npos)
        {
            ++skippedCount;
            continue;
        }

        const std::wstring targetPath = CombinePath(targetDirectory, GetFileNamePart(sourcePath));
        if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE))
        {
            ++skippedCount;
            continue;
        }
        ++importedCount;
    }

    RefreshAssetList();
    if (importedCount > 0)
    {
        statusText_ = L"素材を " + std::to_wstring(importedCount) + L" 件追加しました";
        if (skippedCount > 0)
        {
            statusText_ += L" / " + std::to_wstring(skippedCount) + L" 件スキップ";
        }
        return true;
    }

    statusText_ = L"追加できる素材がありませんでした";
    return false;
}

bool NovelRuntime::ApplyAssetToCommand(size_t commandIndex, const AssetListItem& item)
{
    if (commandIndex >= scenario_.commands.size() || item.isDirectory)
    {
        return false;
    }

    ScriptCommand& command = scenario_.commands[commandIndex];
    const std::wstring relative = MakeRelativePath(item.path, scenarioBaseDir_);

    if (item.category == L"background" && command.type == ScriptCommand::Type::Background)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"背景画像を差し込みました";
        return true;
    }
    if ((item.category == L"character" || item.category == L"picture") && command.type == ScriptCommand::Type::Character)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"立ち絵画像を差し込みました";
        return true;
    }
    if ((item.category == L"background" || item.category == L"picture") && command.type == ScriptCommand::Type::Title)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"画像を差し込みました";
        return true;
    }
    if (item.category == L"bgm" && command.type == ScriptCommand::Type::Bgm)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"BGM を差し込みました";
        return true;
    }
    if (item.category == L"se" && command.type == ScriptCommand::Type::Se)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"SE を差し込みました";
        return true;
    }
    if ((item.category == L"se" || item.category == L"bgm") && command.type == ScriptCommand::Type::Voice)
    {
        command.parameters[L"storage"] = relative;
        statusText_ = L"ボイスを差し込みました";
        return true;
    }

    statusText_ = L"このイベントには対応しない素材です";
    return false;
}

bool NovelRuntime::ApplyEffectToCharacterDrop(size_t effectCommandIndex, size_t characterCommandIndex)
{
    if (effectCommandIndex >= scenario_.commands.size() || characterCommandIndex >= scenario_.commands.size() || effectCommandIndex == characterCommandIndex)
    {
        return false;
    }

    ScriptCommand& effect = scenario_.commands[effectCommandIndex];
    const ScriptCommand& character = scenario_.commands[characterCommandIndex];
    if ((effect.type != ScriptCommand::Type::Fade && effect.type != ScriptCommand::Type::Transition) ||
        character.type != ScriptCommand::Type::Character)
    {
        return false;
    }

    std::wstring position = GetCommandParameter(character, L"pos");
    if (position != L"left" && position != L"right" && position != L"center")
    {
        position = L"center";
    }
    effect.parameters[L"target"] = L"character:" + position;

    size_t insertIndex = characterCommandIndex + 1;
    ScriptCommand movedEffect = effect;
    scenario_.commands.erase(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(effectCommandIndex));
    if (effectCommandIndex < insertIndex)
    {
        --insertIndex;
    }
    insertIndex = (std::min)(insertIndex, scenario_.commands.size());
    scenario_.commands.insert(scenario_.commands.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(movedEffect));
    selectedCommandIndex_ = insertIndex;
    SyncDocumentMetadata();
    statusText_ = L"演出対象を" + GetEffectTargetLabel(L"character:" + position) + L"に設定しました";
    RefreshPreviewIfActive();
    return true;
}

void NovelRuntime::ResetPlaybackState()
{
    StopBgmPlayback();
    StopAudioChannel(AudioChannel::Se);
    StopAudioChannel(AudioChannel::Voice);
    StopAudioChannel(AudioChannel::Preview);
    currentBgmPath_.clear();
    backgroundPath_.clear();
    backgroundDisplayName_.clear();
    backgroundImage_.reset();
    backgroundColor_ = RGB(28, 36, 48);
    backgroundVisible_ = true;
    backgroundOffsetX_ = 0;
    backgroundOffsetY_ = 0;
    backgroundScale_ = 100;
    backgroundOpacity_ = 255;
    leftCharacter_ = { L"left" };
    centerCharacter_ = { L"center" };
    rightCharacter_ = { L"right" };
    speakerName_.clear();
    currentText_.clear();
    displayedText_.clear();
    messageTextColor_ = RGB(242, 244, 247);
    nameTextColor_ = RGB(123, 203, 255);
    ApplyEditorUiDefaults();
    verticalTextEnabled_ = false;
    textRevealActive_ = false;
    textRevealIndex_ = 0;
    nextTextRevealTick_ = 0;
    statusText_ = L"\u30d7\u30ec\u30d3\u30e5\u30fc\u3092\u521d\u671f\u5316\u3057\u307e\u3057\u305f";
    variables_.clear();
    variableHistory_.clear();
    activeChoices_.clear();
    activeChoiceRects_.clear();
    waitingForChoice_ = false;
    reachedEnd_ = false;
    previewMenuVisible_ = false;
    previewLogVisible_ = false;
    previewSkipMode_ = false;
    previewAutoMode_ = false;
    autoAdvanceTick_ = 0;
    waitUntilTick_ = 0;
    fadeStartTick_ = 0;
    fadeEndTick_ = 0;
    fadeOpacity_ = 255;
    fadeTarget_ = L"stage";
    shakeEndTick_ = 0;
    shakePower_ = 0;
    flashStartTick_ = 0;
    flashEndTick_ = 0;
    flashColor_ = RGB(255, 255, 255);
    flashOpacity_ = 220;
    tintColor_ = RGB(255, 255, 255);
    tintOpacity_ = 0;
    stageOffsetX_ = 0;
    stageOffsetY_ = 0;
    stageScale_ = 100;
    zoomStartTick_ = 0;
    zoomEndTick_ = 0;
    zoomStartScale_ = 100;
    zoomTargetScale_ = 100;
    panStartTick_ = 0;
    panEndTick_ = 0;
    panStartX_ = 0;
    panStartY_ = 0;
    panTargetX_ = 0;
    panTargetY_ = 0;
}

void NovelRuntime::PrimePreviewState(size_t startIndex)
{
    const size_t clampedIndex = (std::min)(startIndex, scenario_.commands.size());
    for (size_t i = 0; i < clampedIndex; ++i)
    {
        const ScriptCommand& command = scenario_.commands[i];
        if (ParseBoolValue(GetCommandParameter(command, L"disabled"), false))
        {
            continue;
        }

        switch (command.type)
        {
        case ScriptCommand::Type::Title:
        {
            const std::wstring title = GetCommandParameter(command, L"name");
            if (!title.empty())
            {
                storyTitle_ = title;
            }
            break;
        }
        case ScriptCommand::Type::Background:
            ApplyBackgroundCommand(command);
            break;
        case ScriptCommand::Type::Character:
            ApplyCharacterCommand(command);
            break;
        case ScriptCommand::Type::HideCharacter:
            ApplyHideCharacterCommand(command);
            break;
        case ScriptCommand::Type::Speaker:
            speakerName_ = GetCommandParameter(command, L"name");
            break;
        case ScriptCommand::Type::ClearSpeaker:
            speakerName_.clear();
            break;
        case ScriptCommand::Type::Bgm:
            ApplyBgmCommand(command);
            break;
        case ScriptCommand::Type::StopBgm:
            StopBgmPlayback();
            break;
        case ScriptCommand::Type::MessageWindow:
            ApplyMessageWindowCommand(command);
            break;
        case ScriptCommand::Type::TextSpeed:
            ApplyTextSpeedCommand(command);
            break;
        case ScriptCommand::Type::MessageFont:
            ApplyMessageFontCommand(command);
            break;
        case ScriptCommand::Type::MessageFontReset:
            ApplyMessageFontResetCommand();
            break;
        case ScriptCommand::Type::MessageStyle:
            ApplyMessageStyleCommand(command);
            break;
        case ScriptCommand::Type::TextColor:
            ApplyTextColorCommand(command);
            break;
        case ScriptCommand::Type::NameColor:
            ApplyNameColorCommand(command);
            break;
        case ScriptCommand::Type::NameWindow:
            ApplyNameWindowCommand(command);
            break;
        case ScriptCommand::Type::VerticalText:
            ApplyVerticalTextCommand(command);
            break;
        case ScriptCommand::Type::PageBreak:
            ApplyPageBreakCommand();
            break;
        case ScriptCommand::Type::Shake:
            ApplyShakeCommand(command);
            break;
        case ScriptCommand::Type::Fade:
            ApplyFadeCommand(command);
            break;
        case ScriptCommand::Type::Transition:
            ApplyTransitionCommand(command);
            break;
        case ScriptCommand::Type::Zoom:
            ApplyZoomCommand(command);
            break;
        case ScriptCommand::Type::Pan:
            ApplyPanCommand(command);
            break;
        case ScriptCommand::Type::Flash:
            ApplyFlashCommand(command);
            break;
        case ScriptCommand::Type::Tint:
            ApplyTintCommand(command);
            break;
        case ScriptCommand::Type::SetValue:
            ApplySetValueCommand(command);
            break;
        case ScriptCommand::Type::AddValue:
            ApplyAddValueCommand(command);
            break;
        default:
            break;
        }
    }
}

void NovelRuntime::StartPreviewFromIndex(size_t startIndex)
{
    ResetPlaybackState();
    const size_t clampedIndex = (std::min)(startIndex, scenario_.commands.size());
    PrimePreviewState(clampedIndex);
    currentCommandIndex_ = clampedIndex;
    if (!scenario_.commands.empty())
    {
        Advance();
    }
    else
    {
        reachedEnd_ = true;
    }
    RefreshPreviewWindow();
}

void NovelRuntime::StartPreviewFromSelection()
{
    const size_t startIndex = selectedCommandIndex_ < scenario_.commands.size() ? selectedCommandIndex_ : 0;
    StartPreviewFromIndex(startIndex);
}

void NovelRuntime::RefreshPreviewIfActive()
{
    if (previewVisible_ || showPreviewPanel_)
    {
        StartPreviewFromSelection();
        if (hostWindow_)
        {
            InvalidateRect(hostWindow_, nullptr, TRUE);
        }
    }
}

void NovelRuntime::ToggleSkipMode()
{
    previewSkipMode_ = !previewSkipMode_;
    if (previewSkipMode_)
    {
        previewAutoMode_ = false;
        autoAdvanceTick_ = 0;
        if (textRevealActive_)
        {
            displayedText_ = currentText_;
            textRevealIndex_ = currentText_.size();
            textRevealActive_ = false;
            nextTextRevealTick_ = 0;
        }
    }
    statusText_ = previewSkipMode_ ? L"スキップを開始しました" : L"スキップを停止しました";
}

void NovelRuntime::ToggleAutoMode()
{
    previewAutoMode_ = !previewAutoMode_;
    if (previewAutoMode_)
    {
        previewSkipMode_ = false;
        autoAdvanceTick_ = GetTickCount() + 900;
    }
    else
    {
        autoAdvanceTick_ = 0;
    }
    statusText_ = previewAutoMode_ ? L"オートを開始しました" : L"オートを停止しました";
}

void NovelRuntime::TogglePreviewFullscreen()
{
    if (!hostWindow_)
    {
        return;
    }

    if (!previewFullscreen_)
    {
        windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hostWindow_, GWL_STYLE));
        windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hostWindow_, &windowedPlacement_);

        MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
        GetMonitorInfoW(MonitorFromWindow(hostWindow_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        SetWindowLongPtrW(hostWindow_, GWL_STYLE, windowedStyle_ & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hostWindow_, HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        previewFullscreen_ = true;
        statusText_ = L"全画面表示に切り替えました";
        return;
    }

    SetWindowLongPtrW(hostWindow_, GWL_STYLE, windowedStyle_ == 0 ? WS_OVERLAPPEDWINDOW : windowedStyle_);
    SetWindowPlacement(hostWindow_, &windowedPlacement_);
    SetWindowPos(hostWindow_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    previewFullscreen_ = false;
    statusText_ = L"ウィンドウ表示に戻しました";
}

void NovelRuntime::ShowCreateSceneDialog()
{
    sceneDialogVisible_ = true;
    if (sceneNameEdit_)
    {
        SetWindowTextW(sceneNameEdit_, L"");
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::HideCreateSceneDialog()
{
    sceneDialogVisible_ = false;
    if (sceneNameEdit_)
    {
        ShowWindow(sceneNameEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::ShowProjectLauncher()
{
    LoadRecentProjects();
    projectLauncherVisible_ = true;
    projectDialogVisible_ = false;
    sceneDialogVisible_ = false;
    characterManagerVisible_ = false;
    variableManagerVisible_ = false;
    settingsDialogVisible_ = false;
    inspectorEditing_ = false;
    if (sceneNameEdit_)
    {
        ShowWindow(sceneNameEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::ShowProjectDialog()
{
    projectDialogVisible_ = true;
    if (sceneNameEdit_)
    {
        SetWindowTextW(sceneNameEdit_, L"NewProject");
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::HideProjectDialog()
{
    projectDialogVisible_ = false;
    if (sceneNameEdit_)
    {
        ShowWindow(sceneNameEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::ShowCharacterManagerDialog()
{
    characterManagerVisible_ = true;
    characterFieldDialogVisible_ = false;
    if (sceneNameEdit_)
    {
        SetWindowTextW(sceneNameEdit_, L"");
    }
    if (selectedCharacterDefinitionIndex_ >= characterDefinitions_.size())
    {
        selectedCharacterDefinitionIndex_ = characterDefinitions_.empty() ? static_cast<size_t>(-1) : 0;
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::HideCharacterManagerDialog()
{
    characterManagerVisible_ = false;
    characterFieldDialogVisible_ = false;
    if (sceneNameEdit_)
    {
        ShowWindow(sceneNameEdit_, SW_HIDE);
    }
    if (characterFieldEdit_)
    {
        ShowWindow(characterFieldEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::ShowSettingsDialog()
{
    settingsDialogVisible_ = true;
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::HideSettingsDialog()
{
    settingsDialogVisible_ = false;
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::BeginCharacterFieldEdit(const std::wstring& title, const std::wstring& action, size_t characterIndex, size_t expressionIndex, const std::wstring& initialValue)
{
    characterFieldDialogVisible_ = true;
    characterFieldDialogTitle_ = title;
    characterFieldDialogAction_ = action;
    selectedCharacterDefinitionIndex_ = characterIndex;
    if (characterFieldEdit_)
    {
        SetWindowTextW(characterFieldEdit_, initialValue.c_str());
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::CommitCharacterFieldEdit()
{
    if (!characterFieldDialogVisible_ || !characterFieldEdit_ || selectedCharacterDefinitionIndex_ >= characterDefinitions_.size())
    {
        return;
    }

    const int length = GetWindowTextLengthW(characterFieldEdit_);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(characterFieldEdit_, &value[0], length + 1);
    value.resize(length);
    value = Trim(value);

    CharacterDefinition& definition = characterDefinitions_[selectedCharacterDefinitionIndex_];
    if (characterFieldDialogAction_ == L"edit_id")
    {
        if (value.empty())
        {
            statusText_ = L"キャラクターIDは空にできません";
            return;
        }
        for (size_t i = 0; i < characterDefinitions_.size(); ++i)
        {
            if (i != selectedCharacterDefinitionIndex_ && characterDefinitions_[i].id == value)
            {
                statusText_ = L"同じキャラクターIDは使えません";
                return;
            }
        }
        const std::wstring previousId = definition.id;
        definition.id = value;
        for (ScriptCommand& command : scenario_.commands)
        {
            if (command.type == ScriptCommand::Type::Character && GetCommandParameter(command, L"name") == previousId)
            {
                command.parameters[L"name"] = value;
            }
        }
        statusText_ = L"キャラクターIDを更新しました";
    }
    else if (characterFieldDialogAction_ == L"edit_display_name")
    {
        definition.displayName = value;
        statusText_ = L"表示名を更新しました";
    }
    else if (characterFieldDialogAction_ == L"edit_color")
    {
        definition.color = value.empty() ? L"#ffffff" : value;
        statusText_ = L"テーマ色を更新しました";
    }
    else if (StartsWithText(characterFieldDialogAction_, L"edit_expression_name:"))
    {
        const size_t expressionIndex = static_cast<size_t>(_wtoi(characterFieldDialogAction_.substr(21).c_str()));
        if (expressionIndex < definition.expressions.size())
        {
            definition.expressions[expressionIndex].name = value.empty() ? L"差分" : value;
            statusText_ = L"表情名を更新しました";
        }
    }

    characterFieldDialogVisible_ = false;
    SaveProject();
    RefreshPreviewIfActive();
}

void NovelRuntime::CancelCharacterFieldEdit()
{
    characterFieldDialogVisible_ = false;
    if (characterFieldEdit_)
    {
        ShowWindow(characterFieldEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

const CharacterDefinition* NovelRuntime::FindCharacterDefinition(const std::wstring& id) const
{
    for (const CharacterDefinition& definition : characterDefinitions_)
    {
        if (definition.id == id)
        {
            return &definition;
        }
    }
    return nullptr;
}

CharacterDefinition* NovelRuntime::FindCharacterDefinition(const std::wstring& id)
{
    for (CharacterDefinition& definition : characterDefinitions_)
    {
        if (definition.id == id)
        {
            return &definition;
        }
    }
    return nullptr;
}

std::wstring NovelRuntime::GetCharacterDefinitionLabel(const CharacterDefinition& definition) const
{
    if (!definition.displayName.empty())
    {
        return definition.displayName + L" (" + definition.id + L")";
    }
    return definition.id;
}

std::wstring NovelRuntime::GetEffectTargetLabel(const std::wstring& target) const
{
    if (target == L"message")
    {
        return L"メッセージ";
    }
    if (target == L"all")
    {
        return L"全体";
    }
    if (target == L"background")
    {
        return L"背景";
    }
    if (target == L"character:left")
    {
        return L"左キャラ";
    }
    if (target == L"character:center")
    {
        return L"中央キャラ";
    }
    if (target == L"character:right")
    {
        return L"右キャラ";
    }
    return L"ステージ";
}

std::wstring NovelRuntime::GetVariableTypeLabel(VariableType type) const
{
    switch (type)
    {
    case VariableType::Bool: return L"フラグ";
    case VariableType::Integer: return L"数値";
    default: return L"文字列";
    }
}

void NovelRuntime::SyncVariableDefinitions()
{
    auto ensureVariable = [&](const std::wstring& name, const std::wstring& sampleValue)
    {
        const std::wstring trimmed = Trim(name);
        if (trimmed.empty())
        {
            return;
        }
        for (VariableDefinition& definition : variableDefinitions_)
        {
            if (definition.name == trimmed)
            {
                if (definition.initialValue.empty() && !sampleValue.empty())
                {
                    definition.initialValue = sampleValue;
                }
                return;
            }
        }
        VariableDefinition definition;
        definition.name = trimmed;
        definition.initialValue = sampleValue.empty() ? L"0" : sampleValue;
        definition.type = InferVariableType(definition.initialValue);
        variableDefinitions_.push_back(definition);
    };

    for (const auto& variable : variables_)
    {
        ensureVariable(variable.first, variable.second);
    }
    for (const ScriptCommand& command : scenario_.commands)
    {
        if (command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue || command.type == ScriptCommand::Type::IfJump)
        {
            ensureVariable(GetCommandParameter(command, L"name"), GetCommandParameter(command, L"value"));
        }
        if (command.type == ScriptCommand::Type::Choice)
        {
            for (size_t i = 0; i < command.links.size(); ++i)
            {
                ensureVariable(GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_name_", i)), GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_value_", i)));
            }
        }
    }

    std::sort(variableDefinitions_.begin(), variableDefinitions_.end(), [](const VariableDefinition& a, const VariableDefinition& b)
    {
        return a.name < b.name;
    });

    for (const VariableDefinition& definition : variableDefinitions_)
    {
        if (variables_.find(definition.name) == variables_.end())
        {
            variables_[definition.name] = definition.initialValue;
        }
    }
}

size_t NovelRuntime::GetVariableUsageCount(const std::wstring& name) const
{
    size_t count = 0;
    for (const ScriptCommand& command : scenario_.commands)
    {
        if ((command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue || command.type == ScriptCommand::Type::IfJump) &&
            GetCommandParameter(command, L"name") == name)
        {
            ++count;
        }
        if (command.type == ScriptCommand::Type::Choice)
        {
            for (size_t i = 0; i < command.links.size(); ++i)
            {
                if (GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_name_", i)) == name)
                {
                    ++count;
                }
            }
        }
    }
    return count;
}

std::wstring NovelRuntime::ShowVariableSelectionMenu(POINT point, const std::wstring& currentName) const
{
    if (!hostWindow_ || variableDefinitions_.empty())
    {
        return currentName;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return currentName;
    }

    const UINT kBaseId = 50000;
    for (size_t i = 0; i < variableDefinitions_.size(); ++i)
    {
        UINT flags = MF_STRING;
        if (variableDefinitions_[i].name == currentName)
        {
            flags |= MF_CHECKED;
        }
        AppendMenuW(menu, flags, kBaseId + static_cast<UINT>(i), variableDefinitions_[i].name.c_str());
    }

    POINT screenPoint = point;
    ClientToScreen(hostWindow_, &screenPoint);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hostWindow_, nullptr);
    DestroyMenu(menu);

    if (command >= kBaseId && command < kBaseId + variableDefinitions_.size())
    {
        return variableDefinitions_[command - kBaseId].name;
    }
    return currentName;
}

std::wstring NovelRuntime::ShowFontSelectionMenu(POINT point, const std::wstring& currentName) const
{
    if (!hostWindow_ || availableFonts_.empty())
    {
        return currentName;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return currentName;
    }

    const UINT kBaseId = 51000;
    for (size_t i = 0; i < availableFonts_.size(); ++i)
    {
        UINT flags = MF_STRING;
        if (availableFonts_[i].family == currentName)
        {
            flags |= MF_CHECKED;
        }
        const std::wstring label = availableFonts_[i].family + L"  [" + availableFonts_[i].label + L"]";
        AppendMenuW(menu, flags, kBaseId + static_cast<UINT>(i), label.c_str());
    }

    POINT screenPoint = point;
    ClientToScreen(hostWindow_, &screenPoint);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hostWindow_, nullptr);
    DestroyMenu(menu);

    if (command >= kBaseId && command < kBaseId + availableFonts_.size())
    {
        return availableFonts_[command - kBaseId].family;
    }
    return currentName;
}

void NovelRuntime::ShowVariableManagerDialog()
{
    SyncVariableDefinitions();
    variableManagerVisible_ = true;
    if (selectedVariableDefinitionIndex_ >= variableDefinitions_.size())
    {
        selectedVariableDefinitionIndex_ = variableDefinitions_.empty() ? static_cast<size_t>(-1) : 0;
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::HideVariableManagerDialog()
{
    variableManagerVisible_ = false;
    variableFieldDialogVisible_ = false;
    if (variableFieldEdit_)
    {
        ShowWindow(variableFieldEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::BeginVariableFieldEdit(const std::wstring& title, const std::wstring& action, size_t variableIndex, const std::wstring& initialValue)
{
    variableFieldDialogVisible_ = true;
    variableFieldDialogTitle_ = title;
    variableFieldDialogAction_ = action;
    selectedVariableDefinitionIndex_ = variableIndex;
    if (variableFieldEdit_)
    {
        SetWindowTextW(variableFieldEdit_, initialValue.c_str());
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

void NovelRuntime::CommitVariableFieldEdit()
{
    if (!variableFieldDialogVisible_ || !variableFieldEdit_ || selectedVariableDefinitionIndex_ >= variableDefinitions_.size())
    {
        return;
    }

    const int length = GetWindowTextLengthW(variableFieldEdit_);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(variableFieldEdit_, &value[0], length + 1);
    value.resize(length);
    value = Trim(value);

    VariableDefinition& definition = variableDefinitions_[selectedVariableDefinitionIndex_];
    if (variableFieldDialogAction_ == L"edit_name")
    {
        if (value.empty())
        {
            statusText_ = L"変数名は空にできません";
            return;
        }
        for (size_t i = 0; i < variableDefinitions_.size(); ++i)
        {
            if (i != selectedVariableDefinitionIndex_ && variableDefinitions_[i].name == value)
            {
                statusText_ = L"同じ変数名は使えません";
                return;
            }
        }
        const std::wstring previousName = definition.name;
        definition.name = value;
        const auto found = variables_.find(previousName);
        if (found != variables_.end())
        {
            variables_[value] = found->second;
            variables_.erase(found);
        }
        for (ScriptCommand& command : scenario_.commands)
        {
            if ((command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue || command.type == ScriptCommand::Type::IfJump) &&
                GetCommandParameter(command, L"name") == previousName)
            {
                command.parameters[L"name"] = value;
            }
            if (command.type == ScriptCommand::Type::Choice)
            {
                for (size_t i = 0; i < command.links.size(); ++i)
                {
                    const std::wstring key = GetChoiceParamKey(L"__choice_cond_name_", i);
                    if (GetCommandParameter(command, key) == previousName)
                    {
                        command.parameters[key] = value;
                    }
                }
            }
        }
        statusText_ = L"変数名を更新しました";
    }
    else if (variableFieldDialogAction_ == L"edit_initial")
    {
        definition.initialValue = value;
        statusText_ = L"初期値を更新しました";
    }
    else if (variableFieldDialogAction_ == L"edit_current")
    {
        variables_[definition.name] = value;
        PushVariableHistory(L"EDIT " + definition.name + L" = " + value);
        statusText_ = L"現在値を更新しました";
    }
    else if (variableFieldDialogAction_ == L"edit_description")
    {
        definition.description = value;
        statusText_ = L"説明を更新しました";
    }

    variableFieldDialogVisible_ = false;
    SaveProject();
    RefreshPreviewIfActive();
}

void NovelRuntime::CancelVariableFieldEdit()
{
    variableFieldDialogVisible_ = false;
    if (variableFieldEdit_)
    {
        ShowWindow(variableFieldEdit_, SW_HIDE);
    }
    if (hostWindow_)
    {
        InvalidateRect(hostWindow_, nullptr, TRUE);
    }
}

bool NovelRuntime::CreateNewScene()
{
    if (!ConfirmDiscardUnsavedChanges())
    {
        return false;
    }
    if (!sceneNameEdit_)
    {
        return false;
    }

    const int length = GetWindowTextLengthW(sceneNameEdit_);
    std::wstring sceneName(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(sceneNameEdit_, &sceneName[0], length + 1);
    sceneName.resize(length);

    sceneName.erase(std::remove_if(sceneName.begin(), sceneName.end(), [](wchar_t ch)
    {
        return ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch < 32;
    }), sceneName.end());

    while (!sceneName.empty() && iswspace(sceneName.front()))
    {
        sceneName.erase(sceneName.begin());
    }
    while (!sceneName.empty() && iswspace(sceneName.back()))
    {
        sceneName.pop_back();
    }

    if (sceneName.empty())
    {
        statusText_ = L"シナリオ名を入力してください";
        SetFocus(sceneNameEdit_);
        return false;
    }

    const std::wstring scenarioDirectory = GetScenarioDirectory();
    EnsureDirectoryExists(scenarioDirectory);
    const std::wstring newPath = CombinePath(scenarioDirectory, sceneName + L".ks");
    if (GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        statusText_ = L"同名のシナリオが既にあります";
        SetFocus(sceneNameEdit_);
        SendMessageW(sceneNameEdit_, EM_SETSEL, 0, -1);
        return false;
    }

    if (!TryWriteTextFile(newPath, L"[title name=\"New Scene\"]\r\n[text value=\"New Text\"]\r\n"))
    {
        statusText_ = L"\u65b0\u898f\u30b7\u30fc\u30f3\u4f5c\u6210\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    HideCreateSceneDialog();
    LoadScenario(newPath);
    RefreshSceneList();
    MarkCurrentStateSaved();
    statusText_ = L"\u65b0\u898f\u30b7\u30fc\u30f3\u3092\u4f5c\u6210\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::RenameCurrentScene()
{
    if (scenarioPath_.empty())
    {
        return false;
    }

    WCHAR fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, GetFileNamePart(scenarioPath_).c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    const std::wstring newPath = fileBuffer;
    if (!MoveFileExW(scenarioPath_.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        statusText_ = L"\u30b7\u30fc\u30f3\u540d\u5909\u66f4\u306b\u5931\u6557\u3057\u307e\u3057\u305f";
        return false;
    }

    scenarioPath_ = newPath;
    scenarioBaseDir_ = GetDirectoryPath(newPath);
    projectPath_ = CombinePath(GetAssetsRootDirectory(), L"project.kproj");
    RefreshSceneList();
    statusText_ = L"\u30b7\u30fc\u30f3\u540d\u3092\u5909\u66f4\u3057\u307e\u3057\u305f";
    return true;
}

bool NovelRuntime::DuplicateCurrentScene()
{
    if (scenarioPath_.empty())
    {
        return false;
    }

    WCHAR fileBuffer[MAX_PATH] = {};
    const std::wstring defaultName = GetFileStemPart(scenarioPath_) + L"_copy.ks";
    wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hostWindow_;
    ofn.lpstrFilter = L"Kaktos Scenario (*.ks)\0*.ks\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"ks";
    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    const std::wstring newPath = fileBuffer;
    if (!CopyFileW(scenarioPath_.c_str(), newPath.c_str(), FALSE))
    {
        statusText_ = L"シナリオ複製に失敗しました";
        return false;
    }

    RefreshSceneList();
    statusText_ = L"シナリオを複製しました";
    return true;
}

bool NovelRuntime::DeleteCurrentScene()
{
    if (scenarioPath_.empty())
    {
        return false;
    }
    if (!ConfirmDiscardUnsavedChanges())
    {
        return false;
    }

    if (MessageBoxW(hostWindow_, (L"シナリオを削除しますか?\n" + GetFileNamePart(scenarioPath_)).c_str(), L"確認", MB_ICONWARNING | MB_YESNO) != IDYES)
    {
        return false;
    }

    const std::wstring deletingPath = scenarioPath_;
    if (!DeleteFileW(deletingPath.c_str()))
    {
        statusText_ = L"シナリオ削除に失敗しました";
        return false;
    }

    RefreshSceneList();
    if (!sceneItems_.empty())
    {
        LoadScenario(sceneItems_.front().path);
        MarkCurrentStateSaved();
    }
    else
    {
        scenarioPath_.clear();
        scenarioBaseDir_ = GetScenarioDirectory();
        scenario_.commands.clear();
        selectedCommandIndex_ = 0;
        currentText_ = L"シナリオがありません";
    }
    statusText_ = L"シナリオを削除しました";
    return true;
}

bool NovelRuntime::HandleSceneClick(POINT point)
{
    if (sceneDialogVisible_)
    {
        if (PtInRect(&sceneDialogCreateRect_, point))
        {
            return CreateNewScene();
        }
        if (PtInRect(&sceneDialogCancelRect_, point) || !PtInRect(&sceneDialogRect_, point))
        {
            HideCreateSceneDialog();
            statusText_ = L"新規シナリオ作成をキャンセルしました";
            return true;
        }
        return true;
    }

    for (const LeftTabItem& tab : leftTabs_)
    {
        if (PtInRect(&tab.rect, point))
        {
            if (tab.id == L"components") leftPanelTab_ = LeftPanelTab::Components;
            else if (tab.id == L"materials")
            {
                leftPanelTab_ = LeftPanelTab::Materials;
                RefreshAssetList();
            }
            else if (tab.id == L"scenario")
            {
                leftPanelTab_ = LeftPanelTab::Scenario;
                RefreshSceneList();
            }
            statusText_ = tab.label + L" \u30bf\u30d6\u3092\u958b\u304d\u307e\u3057\u305f";
            return true;
        }
    }

    if (leftPanelTab_ == LeftPanelTab::Materials)
    {
        if (PtInRect(&materialAddRect_, point))
        {
            return AddMaterialFile();
        }
        for (const AssetCategoryItem& category : assetCategories_)
        {
            if (PtInRect(&category.rect, point))
            {
                selectedAssetCategory_ = category.id;
                statusText_ = category.label + L" \u7d20\u6750\u3092\u8868\u793a\u3057\u307e\u3057\u305f";
                return true;
            }
        }
        return false;
    }

    if (leftPanelTab_ == LeftPanelTab::Scenario)
    {
        if (PtInRect(&sceneAddRect_, point))
        {
            ShowCreateSceneDialog();
            return true;
        }
        if (PtInRect(&sceneRenameRect_, point))
        {
            return RenameCurrentScene();
        }
        if (PtInRect(&sceneDuplicateRect_, point))
        {
            return DuplicateCurrentScene();
        }
        if (PtInRect(&sceneDeleteRect_, point))
        {
            return DeleteCurrentScene();
        }
        for (const SceneListItem& item : sceneItems_)
        {
            if (PtInRect(&item.rect, point))
            {
                return SwitchScenarioFile(item.path);
            }
        }
        return false;
    }

    for (PaletteSectionItem& section : paletteSections_)
    {
        RECT headerRect = section.rect;
        headerRect.bottom = headerRect.top + 24;
        if (PtInRect(&section.toggleRect, point) || PtInRect(&headerRect, point))
        {
            section.expanded = !section.expanded;
            statusText_ = section.title + (section.expanded ? L"\u3092\u5c55\u958b\u3057\u307e\u3057\u305f" : L"\u3092\u6298\u308a\u305f\u305f\u307f\u307e\u3057\u305f");
            return true;
        }
    }
    return false;
}

bool NovelRuntime::HandleAssetClick(POINT point)
{
    if (leftPanelTab_ != LeftPanelTab::Materials)
    {
        return false;
    }

    if (!selectedAssetPath_.empty() && (selectedAssetPreviewCategory_ == L"bgm" || selectedAssetPreviewCategory_ == L"se"))
    {
        if (PtInRect(&materialPreviewVolumeDownRect_, point))
        {
            assetPreviewVolume_ = (std::max)(0, assetPreviewVolume_ - 5);
            SetAudioChannelVolume(AudioChannel::Preview, assetPreviewVolume_);
            statusText_ = L"素材プレビュー音量: " + std::to_wstring(assetPreviewVolume_) + L"%";
            return true;
        }
        if (PtInRect(&materialPreviewVolumeUpRect_, point))
        {
            assetPreviewVolume_ = (std::min)(100, assetPreviewVolume_ + 5);
            SetAudioChannelVolume(AudioChannel::Preview, assetPreviewVolume_);
            statusText_ = L"素材プレビュー音量: " + std::to_wstring(assetPreviewVolume_) + L"%";
            return true;
        }
        if (PtInRect(&materialPreviewPlayRect_, point))
        {
            AssetListItem previewItem{ selectedAssetPreviewCategory_, selectedAssetPath_, selectedAssetLabel_, {} };
            StartAssetPreview(previewItem);
            return true;
        }
        if (PtInRect(&materialPreviewStopRect_, point))
        {
            StopAssetPreviewAudio();
            statusText_ = L"音声プレビューを停止しました";
            return true;
        }
    }

    ScriptCommand* selected = GetSelectedCommand();

    for (const AssetListItem& item : assetItems_)
    {
        if (!PtInRect(&item.rect, point))
        {
            continue;
        }

        if (item.isDirectory)
        {
            selectedAssetPath_.clear();
            selectedAssetLabel_ = item.label;
            selectedAssetPreviewCategory_.clear();
            statusText_ = item.label + L" フォルダを選択しました";
            return true;
        }

        StartAssetPreview(item);

        if (!selected)
        {
            return true;
        }

        PushUndoSnapshot();
        if (ApplyAssetToCommand(selectedCommandIndex_, item))
        {
            RefreshPreviewIfActive();
            return true;
        }
        undoStack_.pop_back();
        return true;
    }
    return false;
}

void NovelRuntime::ResetLayout()
{
    leftPanelWidth_ = 280;
    rightPanelWidth_ = 320;
    graphHeight_ = 162;
    eventListHeight_ = 208;
    activeDragHandle_ = DragHandle::None;
    statusText_ = L"\u30ec\u30a4\u30a2\u30a6\u30c8\u3092\u521d\u671f\u5316\u3057\u307e\u3057\u305f";
}

bool NovelRuntime::HandleViewMenuCommand(UINT commandId)
{
    switch (commandId)
    {
    case IDM_VIEW_COMPONENTS:
        showComponents_ = !showComponents_;
        statusText_ = showComponents_ ? L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u3092\u8868\u793a" : L"\u30b3\u30f3\u30dd\u30fc\u30cd\u30f3\u30c8\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_INSPECTOR:
        showInspector_ = !showInspector_;
        statusText_ = showInspector_ ? L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u3092\u8868\u793a" : L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_FLOWGRAPH:
        showFlowGraph_ = !showFlowGraph_;
        statusText_ = showFlowGraph_ ? L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u3092\u8868\u793a" : L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_PREVIEW:
        showPreviewPanel_ = !showPreviewPanel_;
        statusText_ = showPreviewPanel_ ? L"\u30d7\u30ec\u30d3\u30e5\u30fc\u9818\u57df\u3092\u8868\u793a" : L"\u30d7\u30ec\u30d3\u30e5\u30fc\u9818\u57df\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_EVENTLIST:
        showEventList_ = !showEventList_;
        statusText_ = showEventList_ ? L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7\u3092\u8868\u793a" : L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7\u3092\u975e\u8868\u793a";
        return true;
    case IDM_VIEW_VARIABLES:
        ShowVariableManagerDialog();
        return true;
    case IDM_VIEW_RESET_LAYOUT:
        ResetLayout();
        return true;
    default:
        return false;
    }
}

bool NovelRuntime::IsViewMenuChecked(UINT commandId) const
{
    switch (commandId)
    {
    case IDM_VIEW_COMPONENTS: return showComponents_;
    case IDM_VIEW_INSPECTOR: return showInspector_;
    case IDM_VIEW_FLOWGRAPH: return showFlowGraph_;
    case IDM_VIEW_PREVIEW: return showPreviewPanel_;
    case IDM_VIEW_EVENTLIST: return showEventList_;
    default: return false;
    }
}

bool NovelRuntime::HandleKeyDown(WPARAM key)
{
    if (projectDialogVisible_)
    {
        if (key == VK_ESCAPE)
        {
            HideProjectDialog();
            statusText_ = L"プロジェクト操作を閉じました";
            return true;
        }
        if (key == VK_RETURN)
        {
            return CreateProjectFromDialog();
        }
        return false;
    }

    if (projectLauncherVisible_)
    {
        if (key == VK_RETURN)
        {
            return LoadProjectFromDialog();
        }
        return true;
    }

    if (playerMode_ || previewVisible_)
    {
        if (key == VK_ESCAPE)
        {
            if (settingsDialogVisible_)
            {
                HideSettingsDialog();
                return true;
            }
            if (previewLogVisible_)
            {
                previewLogVisible_ = false;
            }
            else
            {
                previewMenuVisible_ = !previewMenuVisible_;
            }
            return true;
        }
        if (key == 'L')
        {
            previewLogVisible_ = !previewLogVisible_;
            previewMenuVisible_ = false;
            return true;
        }
        if (key == 'S')
        {
            ToggleSkipMode();
            return true;
        }
        if (key == 'A')
        {
            ToggleAutoMode();
            return true;
        }
        if (key == 'C')
        {
            ShowSettingsDialog();
            previewMenuVisible_ = false;
            previewLogVisible_ = false;
            return true;
        }
        if (key == VK_F11 || key == 'F')
        {
            TogglePreviewFullscreen();
            return true;
        }
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        if (key == 'R')
        {
            return ExecuteEditorCommand(IDM_EDIT_RELOAD);
        }
        if (key == 'Z')
        {
            return ExecuteEditorCommand(IDM_EDIT_UNDO);
        }
        if (key == 'Y')
        {
            return ExecuteEditorCommand(IDM_EDIT_REDO);
        }
        if (key == 'X')
        {
            return ExecuteEditorCommand(IDM_EDIT_CUT);
        }
        if (key == 'C')
        {
            return ExecuteEditorCommand(IDM_EDIT_COPY);
        }
        if (key == 'V')
        {
            return ExecuteEditorCommand(IDM_EDIT_PASTE);
        }
        if (key == 'A')
        {
            return ExecuteEditorCommand(IDM_EDIT_SELECT_ALL);
        }
    }

    if (inspectorEditing_)
    {
        if (key == VK_RETURN)
        {
            CommitInspectorEdit();
            return true;
        }
        if (key == VK_ESCAPE)
        {
            CancelInspectorEdit();
            return true;
        }
        return false;
    }

    if (variableFieldDialogVisible_)
    {
        if (key == VK_RETURN)
        {
            CommitVariableFieldEdit();
            return true;
        }
        if (key == VK_ESCAPE)
        {
            CancelVariableFieldEdit();
            return true;
        }
        return false;
    }

    if (waitingForChoice_)
    {
        if (key >= '1' && key < '1' + static_cast<WPARAM>(activeChoices_.size()))
        {
            SelectChoice(static_cast<size_t>(key - '1'));
            return true;
        }
        return false;
    }

    if (!scenario_.commands.empty() &&
        selectedCommandIndex_ < scenario_.commands.size() &&
        scenario_.commands[selectedCommandIndex_].type == ScriptCommand::Type::Choice &&
        key >= '1' && key <= '9')
    {
        const size_t requestedIndex = static_cast<size_t>(key - '1');
        if (requestedIndex < scenario_.commands[selectedCommandIndex_].links.size())
        {
            selectedChoiceLinkIndex_ = requestedIndex;
            statusText_ = L"choice link selected: " + std::to_wstring(requestedIndex + 1);
            return true;
        }
    }

    if (key == VK_RETURN || key == VK_SPACE || key == VK_RIGHT)
    {
        if (textRevealActive_)
        {
            displayedText_ = currentText_;
            textRevealIndex_ = currentText_.size();
            textRevealActive_ = false;
            nextTextRevealTick_ = 0;
            return true;
        }
        if (!reachedEnd_)
        {
            Advance();
            return true;
        }
    }
    if (key == VK_DELETE)
    {
        DeleteSelectedCommand();
        return true;
    }

    return false;
}

bool NovelRuntime::IsEditableSourceNode(size_t commandIndex) const
{
    if (commandIndex >= scenario_.commands.size())
    {
        return false;
    }

    const ScriptCommand::Type type = scenario_.commands[commandIndex].type;
    return type == ScriptCommand::Type::Choice ||
        type == ScriptCommand::Type::Jump ||
        type == ScriptCommand::Type::IfJump;
}

bool NovelRuntime::IsLabelNode(size_t commandIndex) const
{
    return commandIndex < scenario_.commands.size() &&
        scenario_.commands[commandIndex].type == ScriptCommand::Type::Label;
}

void NovelRuntime::RewireSelectedSourceToLabel(size_t labelCommandIndex)
{
    if (selectedCommandIndex_ >= scenario_.commands.size() || !IsLabelNode(labelCommandIndex))
    {
        return;
    }

    const std::wstring labelName = GetCommandParameter(scenario_.commands[labelCommandIndex], L"name");
    ScriptCommand& selectedCommand = scenario_.commands[selectedCommandIndex_];

    if (selectedCommand.type == ScriptCommand::Type::Choice)
    {
        if (selectedChoiceLinkIndex_ < selectedCommand.links.size())
        {
            selectedCommand.links[selectedChoiceLinkIndex_].second = labelName;
            statusText_ = L"choice link rewired to: " + labelName;
        }
        return;
    }

    selectedCommand.parameters[L"target"] = labelName;
    statusText_ = L"target rewired to: " + labelName;
}

std::wstring NovelRuntime::MakeUniqueLabelName(const std::wstring& prefix) const
{
    const std::wstring base = prefix.empty() ? L"label" : prefix;
    auto labelExists = [&](const std::wstring& candidate)
    {
        if (scenario_.labels.find(candidate) != scenario_.labels.end())
        {
            return true;
        }
        for (const ScriptCommand& command : scenario_.commands)
        {
            if (command.type == ScriptCommand::Type::Label && GetCommandParameter(command, L"name") == candidate)
            {
                return true;
            }
        }
        return false;
    };

    if (!labelExists(base))
    {
        return base;
    }

    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::wstring candidate = base + L"_" + std::to_wstring(suffix);
        if (!labelExists(candidate))
        {
            return candidate;
        }
    }
    return base + L"_" + std::to_wstring(GetTickCount());
}

bool NovelRuntime::HandleGraphNodeSelection(size_t commandIndex)
{
    if (IsLabelNode(commandIndex) && IsEditableSourceNode(selectedCommandIndex_))
    {
        RewireSelectedSourceToLabel(commandIndex);
        return true;
    }

    selectedCommandIndex_ = commandIndex;
    if (selectedCommandIndex_ < scenario_.commands.size() &&
        scenario_.commands[selectedCommandIndex_].type == ScriptCommand::Type::Choice)
    {
        selectedChoiceLinkIndex_ = 0;
    }
    return true;
}

void NovelRuntime::DrawWrappedText(HDC hdc, const RECT& bounds, const std::wstring& text, UINT format) const
{
    RECT rect = bounds;
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
}

void NovelRuntime::DrawVerticalText(HDC hdc, const RECT& bounds, const std::wstring& text, int lineHeight) const
{
    int columnX = bounds.right - lineHeight;
    int cursorY = bounds.top;
    for (wchar_t ch : text)
    {
        if (ch == L'\r')
        {
            continue;
        }
        if (ch == L'\n' || cursorY + lineHeight > bounds.bottom)
        {
            columnX -= lineHeight;
            cursorY = bounds.top;
            if (ch == L'\n')
            {
                continue;
            }
        }
        if (columnX < bounds.left)
        {
            break;
        }

        RECT charRect = { columnX, cursorY, columnX + lineHeight, cursorY + lineHeight };
        const wchar_t glyph[2] = { ch, L'\0' };
        DrawTextW(hdc, glyph, 1, &charRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cursorY += lineHeight;
    }
}

void NovelRuntime::DrawCharacterSlot(HDC hdc, const RECT& stageRect, CharacterSlot& slot, int centerX) const
{
    if (!slot.visible || (slot.displayName.empty() && !slot.image))
    {
        return;
    }

    const int slotWidth = (260 * slot.scale * stageScale_) / 10000;
    const int slotHeight = (520 * slot.scale * stageScale_) / 10000;
    const RECT characterRect = {
        centerX - slotWidth / 2 + slot.offsetX,
        stageRect.bottom - slotHeight - 20 + slot.offsetY,
        centerX + slotWidth / 2 + slot.offsetX,
        stageRect.bottom - 20 + slot.offsetY
    };

    if (slot.image)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        Gdiplus::ImageAttributes attributes;
        Gdiplus::ColorMatrix matrix =
        {
            1.0f, 0, 0, 0, 0,
            0, 1.0f, 0, 0, 0,
            0, 0, 1.0f, 0, 0,
            0, 0, 0, static_cast<Gdiplus::REAL>(slot.opacity) / 255.0f, 0,
            0, 0, 0, 0, 1.0f
        };
        attributes.SetColorMatrix(&matrix);
        graphics.DrawImage(
            slot.image.get(),
            Gdiplus::RectF(static_cast<Gdiplus::REAL>(characterRect.left), static_cast<Gdiplus::REAL>(characterRect.top), static_cast<Gdiplus::REAL>(characterRect.right - characterRect.left), static_cast<Gdiplus::REAL>(characterRect.bottom - characterRect.top)),
            0.0f,
            0.0f,
            static_cast<Gdiplus::REAL>(slot.image->GetWidth()),
            static_cast<Gdiplus::REAL>(slot.image->GetHeight()),
            Gdiplus::UnitPixel,
            &attributes);
    }
    else
    {
        HBRUSH fillBrush = CreateSolidBrush(RGB(74, 91, 118));
        FillRect(hdc, &characterRect, fillBrush);
        DeleteObject(fillBrush);
        FrameRect(hdc, &characterRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 236, 242));
        RECT labelRect = characterRect;
        labelRect.left += 18;
        labelRect.top += 18;
        labelRect.right -= 18;
        DrawTextW(hdc, slot.displayName.c_str(), -1, &labelRect, DT_CENTER | DT_TOP | DT_WORDBREAK);
    }

}

void NovelRuntime::DrawChoices(HDC hdc, const RECT& messageRect)
{
    activeChoiceRects_.assign(activeChoices_.size(), RECT{});
    if (activeChoices_.empty())
    {
        return;
    }

    const int buttonTop = messageRect.top - static_cast<int>(activeChoices_.size()) * 56 - 18;
    for (size_t index = 0; index < activeChoices_.size(); ++index)
    {
        RECT optionRect = { messageRect.left + 64, buttonTop + static_cast<int>(index) * 56, messageRect.right - 64, buttonTop + static_cast<int>(index) * 56 + 42 };
        activeChoiceRects_[index] = optionRect;

        const bool enabled = activeChoices_[index].enabled;
        HBRUSH optionBrush = CreateSolidBrush(enabled ? RGB(32, 40, 56) : RGB(52, 52, 56));
        FillRect(hdc, &optionRect, optionBrush);
        DeleteObject(optionBrush);
        FrameRect(hdc, &optionRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT textRect = optionRect;
        textRect.left += 16;
        textRect.right -= 16;
        SetTextColor(hdc, enabled ? RGB(238, 242, 247) : RGB(170, 174, 180));
        std::wstring label = std::to_wstring(index + 1) + L". " + activeChoices_[index].text;
        if (!enabled)
        {
            label += L" [条件未達]";
        }
        DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

bool NovelRuntime::HandleToolbarClick(POINT point)
{
    for (size_t i = 0; i < toolbarButtonRects_.size() && i < toolbarItems_.size(); ++i)
    {
        if (!PtInRect(&toolbarButtonRects_[i], point))
        {
            continue;
        }

        const ToolbarItem& item = toolbarItems_[i];
        if (item.id == L"project")
        {
            ShowProjectDialog();
            return true;
        }
        if (item.id == L"preview")
        {
            TogglePreviewWindow();
            statusText_ = L"\u30d7\u30ec\u30d3\u30e5\u30fc\u30a6\u30a3\u30f3\u30c9\u30a6\u3092\u5207\u308a\u66ff\u3048\u307e\u3057\u305f";
            return true;
        }
        if (item.id == L"save")
        {
            SaveProject();
            return true;
        }
        if (item.id == L"characters")
        {
            ShowCharacterManagerDialog();
            return true;
        }
        if (item.id == L"config")
        {
            ShowSettingsDialog();
            return true;
        }
        if (item.id == L"build")
        {
            ExportBuild();
            return true;
        }

        statusText_ = item.label + L"\u0020\u306f\u3053\u308c\u304b\u3089\u5b9f\u88c5\u3057\u307e\u3059";
        return true;
    }

    return false;
}

void NovelRuntime::DrawSplitter(HDC hdc, const RECT& rect, bool active) const
{
    HBRUSH brush = CreateSolidBrush(active ? RGB(110, 176, 214) : RGB(48, 58, 72));
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void NovelRuntime::DrawPreviewSurface(HDC hdc, const RECT& clientRect, bool standalone)
{
    RECT stageRect = clientRect;
    RECT messageRect = clientRect;
    previewMenuButtonRect_ = {};
    previewMenuPanelRect_ = {};
    previewMenuSaveRect_ = {};
    previewMenuLoadRect_ = {};
    previewMenuLogRect_ = {};
    previewMenuSkipRect_ = {};
    previewMenuAutoRect_ = {};
    previewMenuConfigRect_ = {};
    previewMenuFullscreenRect_ = {};
    previewMenuCloseRect_ = {};
    previewMenuTitleRect_ = {};
    previewLogCloseRect_ = {};
    previewUiButtonRects_.clear();
    if (!standalone)
    {
        hoveredPreviewUiButtonIndex_ = -1;
    }
    if (standalone)
    {
        stageRect = clientRect;
        messageRect = { clientRect.left, clientRect.bottom - 170, clientRect.right, clientRect.bottom };
    }
    else
    {
        stageRect = GetStageRect(clientRect);
        messageRect = GetMessageRect(clientRect);
    }

    if (!HasVisibleArea(stageRect) || !HasVisibleArea(messageRect))
    {
        return;
    }

    if (shakePower_ > 0 && shakeEndTick_ != 0)
    {
        const int shakeX = ((GetTickCount() / 16) % 2 == 0 ? shakePower_ : -shakePower_);
        const int shakeY = ((GetTickCount() / 24) % 2 == 0 ? shakePower_ / 2 : -(shakePower_ / 2));
        OffsetRect(&stageRect, shakeX, shakeY);
    }

    SetBkMode(hdc, TRANSPARENT);
    const wchar_t* messageFont = messageFontFace_.empty() ? L"Yu Gothic UI" : messageFontFace_.c_str();
    HFONT titleFont = CreateFontW(28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, messageFont);
    HFONT bodyFont = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, messageFont);
    HFONT speakerFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, messageFont);
    HFONT hintFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, messageFont);
    HFONT originalFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    HBRUSH stageBrush = CreateSolidBrush(backgroundColor_);
    FillRect(hdc, &stageRect, stageBrush);
    DeleteObject(stageBrush);

    if (backgroundImage_ && backgroundVisible_)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const int stageWidth = stageRect.right - stageRect.left;
        const int stageHeight = stageRect.bottom - stageRect.top;
        const int drawWidth = (stageWidth * backgroundScale_ * stageScale_) / 10000;
        const int drawHeight = (stageHeight * backgroundScale_ * stageScale_) / 10000;
        const int drawLeft = stageRect.left + ((stageWidth - drawWidth) / 2) + backgroundOffsetX_ + stageOffsetX_;
        const int drawTop = stageRect.top + ((stageHeight - drawHeight) / 2) + backgroundOffsetY_ + stageOffsetY_;
        Gdiplus::ImageAttributes attributes;
        Gdiplus::ColorMatrix matrix =
        {
            1.0f, 0, 0, 0, 0,
            0, 1.0f, 0, 0, 0,
            0, 0, 1.0f, 0, 0,
            0, 0, 0, static_cast<Gdiplus::REAL>(backgroundOpacity_) / 255.0f, 0,
            0, 0, 0, 0, 1.0f
        };
        attributes.SetColorMatrix(&matrix);
        graphics.DrawImage(
            backgroundImage_.get(),
            Gdiplus::RectF(static_cast<Gdiplus::REAL>(drawLeft), static_cast<Gdiplus::REAL>(drawTop), static_cast<Gdiplus::REAL>(drawWidth), static_cast<Gdiplus::REAL>(drawHeight)),
            0.0f,
            0.0f,
            static_cast<Gdiplus::REAL>(backgroundImage_->GetWidth()),
            static_cast<Gdiplus::REAL>(backgroundImage_->GetHeight()),
            Gdiplus::UnitPixel,
            &attributes);
    }

    HBRUSH shadeBrush = CreateSolidBrush(RGB(0, 0, 0));
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 45, 0 };
    HDC memoryDc = CreateCompatibleDC(hdc);
    HBITMAP overlayBitmap = CreateCompatibleBitmap(hdc, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top);
    HGDIOBJ originalBitmap = SelectObject(memoryDc, overlayBitmap);
    RECT overlayRect = { 0, 0, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top };
    FillRect(memoryDc, &overlayRect, shadeBrush);
    AlphaBlend(hdc, stageRect.left, stageRect.top, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top, memoryDc, 0, 0, stageRect.right - stageRect.left, stageRect.bottom - stageRect.top, blend);
    SelectObject(memoryDc, originalBitmap);
    DeleteObject(overlayBitmap);
    DeleteDC(memoryDc);
    DeleteObject(shadeBrush);

    const int stageWidth = stageRect.right - stageRect.left;
    if (fadeTarget_ == L"background" && fadeEndTick_ != 0 && fadeEndTick_ > fadeStartTick_)
    {
        const DWORD now = GetTickCount();
        const double progress = static_cast<double>((std::min)(now, fadeEndTick_) - fadeStartTick_) / static_cast<double>((std::max<DWORD>)(1, fadeEndTick_ - fadeStartTick_));
        DrawAlphaOverlay(hdc, stageRect, fadeColor_, static_cast<int>((1.0 - progress) * fadeOpacity_));
    }
    DrawCharacterSlot(hdc, stageRect, leftCharacter_, stageRect.left + stageWidth / 4 + stageOffsetX_);
    DrawCharacterSlot(hdc, stageRect, centerCharacter_, stageRect.left + stageWidth / 2 + stageOffsetX_);
    DrawCharacterSlot(hdc, stageRect, rightCharacter_, stageRect.left + (stageWidth * 3) / 4 + stageOffsetX_);

    if (messageWindowVisible_)
    {
        if (messageWindowImage_)
        {
            Gdiplus::Graphics messageGraphics(hdc);
            messageGraphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            messageGraphics.DrawImage(messageWindowImage_.get(), Gdiplus::Rect(messageRect.left, messageRect.top, messageRect.right - messageRect.left, messageRect.bottom - messageRect.top));
        }
        else
        {
            HBRUSH messageBrush = CreateSolidBrush(messageWindowColor_);
            HDC messageDc = CreateCompatibleDC(hdc);
            HBITMAP messageBitmap = CreateCompatibleBitmap(hdc, messageRect.right - messageRect.left, messageRect.bottom - messageRect.top);
            HGDIOBJ originalMessageBitmap = SelectObject(messageDc, messageBitmap);
            RECT localMessageRect = { 0, 0, messageRect.right - messageRect.left, messageRect.bottom - messageRect.top };
            FillRect(messageDc, &localMessageRect, messageBrush);
            BLENDFUNCTION messageBlend = { AC_SRC_OVER, 0, static_cast<BYTE>(messageWindowOpacity_), 0 };
            AlphaBlend(hdc, messageRect.left, messageRect.top, messageRect.right - messageRect.left, messageRect.bottom - messageRect.top, messageDc, 0, 0, messageRect.right - messageRect.left, messageRect.bottom - messageRect.top, messageBlend);
            SelectObject(messageDc, originalMessageBitmap);
            DeleteObject(messageBitmap);
            DeleteDC(messageDc);
            DeleteObject(messageBrush);
            HBRUSH borderBrush = CreateSolidBrush(messageWindowBorderColor_);
            FrameRect(hdc, &messageRect, borderBrush);
            DeleteObject(borderBrush);
        }

        DrawChoices(hdc, messageRect);

        const int messagePadding = (std::max)(8, messageWindowPadding_);
        int bodyTop = messageRect.top + messagePadding;
        if (nameBoxVisible_ && !speakerName_.empty())
        {
            RECT nameRect = {
                messageRect.left + messagePadding + nameWindowOffsetX_,
                messageRect.top + messagePadding + nameWindowOffsetY_,
                messageRect.left + messagePadding + nameWindowOffsetX_ + (std::max)(120, nameWindowWidth_),
                messageRect.top + messagePadding + nameWindowOffsetY_ + (std::max)(28, nameWindowHeight_)
            };
            if (nameRect.right > messageRect.right - messagePadding)
            {
                nameRect.right = messageRect.right - messagePadding;
            }
            if (nameRect.bottom > messageRect.bottom - messagePadding)
            {
                nameRect.bottom = messageRect.bottom - messagePadding;
            }

            RECT namePaintRect = nameRect;
            if (nameWindowImage_)
            {
                Gdiplus::Graphics nameGraphics(hdc);
                nameGraphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                nameGraphics.DrawImage(nameWindowImage_.get(), Gdiplus::Rect(namePaintRect.left, namePaintRect.top, namePaintRect.right - namePaintRect.left, namePaintRect.bottom - namePaintRect.top));
            }
            else
            {
                HDC nameDc = CreateCompatibleDC(hdc);
                HBITMAP nameBitmap = CreateCompatibleBitmap(hdc, namePaintRect.right - namePaintRect.left, namePaintRect.bottom - namePaintRect.top);
                HBITMAP originalNameBitmap = static_cast<HBITMAP>(SelectObject(nameDc, nameBitmap));
                RECT localNameRect = { 0, 0, namePaintRect.right - namePaintRect.left, namePaintRect.bottom - namePaintRect.top };
                HBRUSH nameBrush = CreateSolidBrush(nameWindowColor_);
                FillRect(nameDc, &localNameRect, nameBrush);
                BLENDFUNCTION nameBlend = { AC_SRC_OVER, 0, static_cast<BYTE>(nameWindowOpacity_), 0 };
                AlphaBlend(hdc, namePaintRect.left, namePaintRect.top, namePaintRect.right - namePaintRect.left, namePaintRect.bottom - namePaintRect.top,
                    nameDc, 0, 0, localNameRect.right, localNameRect.bottom, nameBlend);
                SelectObject(nameDc, originalNameBitmap);
                DeleteObject(nameBitmap);
                DeleteDC(nameDc);
                DeleteObject(nameBrush);

                HBRUSH nameBorderBrush = CreateSolidBrush(nameWindowBorderColor_);
                FrameRect(hdc, &namePaintRect, nameBorderBrush);
                DeleteObject(nameBorderBrush);
            }

            SelectObject(hdc, speakerFont);
            const int namePadding = (std::max)(4, nameWindowPadding_);
            RECT nameTextRect = { namePaintRect.left + namePadding, namePaintRect.top, namePaintRect.right - namePadding, namePaintRect.bottom };
            SetTextColor(hdc, nameTextColor_);
            DrawWrappedText(hdc, nameTextRect, speakerName_, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            bodyTop = (std::max)(bodyTop, static_cast<int>(namePaintRect.bottom) + 10);
        }

        SelectObject(hdc, bodyFont);
        SetTextColor(hdc, messageTextColor_);
        RECT bodyRect = { messageRect.left + messagePadding, bodyTop, messageRect.right - messagePadding, messageRect.bottom - messagePadding };
        const std::wstring visibleText = textRevealActive_ ? displayedText_ : (displayedText_.empty() ? currentText_ : displayedText_);
        if (verticalTextEnabled_)
        {
            DrawVerticalText(hdc, bodyRect, visibleText, 26);
        }
        else
        {
            DrawWrappedText(hdc, bodyRect, visibleText, DT_LEFT | DT_WORDBREAK);
        }

    }
    else
    {
        DrawChoices(hdc, messageRect);
    }

    if (standalone)
    {
        previewUiButtonRects_.assign(uiButtons_.size(), RECT{});
        auto resolveButtonRect = [&](const UiButtonDefinition& button) -> RECT
        {
            const int left = button.x >= 0 ? stageRect.left + button.x : stageRect.right + button.x;
            const int top = button.y >= 0 ? stageRect.top + button.y : stageRect.bottom + button.y;
            return { left, top, left + (std::max)(36, button.width), top + (std::max)(20, button.height) };
        };

        auto drawStandaloneButton = [&](const RECT& rect, const std::wstring& label, bool active)
        {
            const bool hovered = hoveredPreviewUiButtonIndex_ >= 0 && previewUiButtonRects_[hoveredPreviewUiButtonIndex_].left == rect.left && previewUiButtonRects_[hoveredPreviewUiButtonIndex_].top == rect.top && previewUiButtonRects_[hoveredPreviewUiButtonIndex_].right == rect.right && previewUiButtonRects_[hoveredPreviewUiButtonIndex_].bottom == rect.bottom;
            HBRUSH brush = CreateSolidBrush(active ? RGB(72, 108, 146) : (hovered ? RGB(50, 60, 72) : RGB(34, 40, 48)));
            FillRect(hdc, &rect, brush);
            DeleteObject(brush);
            FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetTextColor(hdc, RGB(244, 248, 252));
            DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };

        for (size_t i = 0; i < uiButtons_.size(); ++i)
        {
            const UiButtonDefinition& button = uiButtons_[i];
            if (!button.visible)
            {
                continue;
            }

            const RECT rect = resolveButtonRect(button);
            previewUiButtonRects_[i] = rect;
            const bool active = (button.id == L"menu" && previewMenuVisible_) || (button.id == L"log" && previewLogVisible_);
            if (button.iconImage)
            {
                HBRUSH buttonBrush = CreateSolidBrush(active ? RGB(72, 108, 146) : (static_cast<int>(i) == hoveredPreviewUiButtonIndex_ ? RGB(50, 60, 72) : RGB(34, 40, 48)));
                FillRect(hdc, &rect, buttonBrush);
                DeleteObject(buttonBrush);
                FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(button.iconImage.get(), Gdiplus::Rect(rect.left + 4, rect.top + 4, (rect.right - rect.left) - 8, (rect.bottom - rect.top) - 8));
            }
            else
            {
                drawStandaloneButton(rect, button.label.empty() ? button.id : button.label, active);
            }
            if (button.id == L"menu")
            {
                previewMenuButtonRect_ = rect;
            }
        }

        if (hoveredPreviewUiButtonIndex_ >= 0 && hoveredPreviewUiButtonIndex_ < static_cast<int>(uiButtons_.size()) && hoveredPreviewUiButtonIndex_ < static_cast<int>(previewUiButtonRects_.size()))
        {
            const UiButtonDefinition& hoveredButton = uiButtons_[hoveredPreviewUiButtonIndex_];
            const RECT anchor = previewUiButtonRects_[hoveredPreviewUiButtonIndex_];
            RECT tipRect = { anchor.left - 24, anchor.top - 34, anchor.right + 24, anchor.top - 10 };
            HBRUSH tipBrush = CreateSolidBrush(RGB(16, 22, 30));
            FillRect(hdc, &tipRect, tipBrush);
            DeleteObject(tipBrush);
            FrameRect(hdc, &tipRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetTextColor(hdc, RGB(244, 248, 252));
            DrawWrappedText(hdc, tipRect, hoveredButton.label.empty() ? hoveredButton.id : hoveredButton.label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        if (previewMenuVisible_ || previewLogVisible_)
        {
            previewMenuPanelRect_ = { stageRect.left + 120, stageRect.top + 70, stageRect.right - 120, stageRect.bottom - 70 };
            HBRUSH overlayBrush = CreateSolidBrush(RGB(16, 20, 26));
            FillRect(hdc, &previewMenuPanelRect_, overlayBrush);
            DeleteObject(overlayBrush);
            FrameRect(hdc, &previewMenuPanelRect_, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }

        if (previewMenuVisible_)
        {
            auto drawMenuButton = [&](RECT rect, const std::wstring& label, bool active)
            {
                HBRUSH brush = CreateSolidBrush(active ? RGB(72, 112, 74) : RGB(38, 48, 62));
                FillRect(hdc, &rect, brush);
                DeleteObject(brush);
                FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                SetTextColor(hdc, RGB(244, 248, 252));
                DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            };

            RECT titleRect = { previewMenuPanelRect_.left + 20, previewMenuPanelRect_.top + 18, previewMenuPanelRect_.right - 20, previewMenuPanelRect_.top + 46 };
            SetTextColor(hdc, RGB(244, 248, 252));
            DrawWrappedText(hdc, titleRect, L"システムメニュー", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            const int left = previewMenuPanelRect_.left + 40;
            const int right = previewMenuPanelRect_.right - 40;
            const int rowHeight = 38;
            const int rowGap = 10;
            int top = previewMenuPanelRect_.top + 74;
            auto nextRow = [&]() -> RECT
            {
                RECT rect = { left, top, right, top + rowHeight };
                top += rowHeight + rowGap;
                return rect;
            };

            previewMenuSaveRect_ = nextRow();
            previewMenuLoadRect_ = nextRow();
            previewMenuLogRect_ = nextRow();
            previewMenuSkipRect_ = nextRow();
            previewMenuAutoRect_ = nextRow();
            previewMenuConfigRect_ = nextRow();
            previewMenuFullscreenRect_ = nextRow();
            previewMenuTitleRect_ = nextRow();
            previewMenuCloseRect_ = nextRow();

            drawMenuButton(previewMenuSaveRect_, L"セーブ", false);
            drawMenuButton(previewMenuLoadRect_, L"ロード", false);
            drawMenuButton(previewMenuLogRect_, L"バックログ", previewLogVisible_);
            drawMenuButton(previewMenuSkipRect_, previewSkipMode_ ? L"スキップ: ON" : L"スキップ: OFF", previewSkipMode_);
            drawMenuButton(previewMenuAutoRect_, previewAutoMode_ ? L"オート: ON" : L"オート: OFF", previewAutoMode_);
            drawMenuButton(previewMenuConfigRect_, L"コンフィグ", settingsDialogVisible_);
            drawMenuButton(previewMenuFullscreenRect_, previewFullscreen_ ? L"全画面解除" : L"フルスクリーン", previewFullscreen_);
            drawMenuButton(previewMenuTitleRect_, L"タイトルへ戻る", false);
            drawMenuButton(previewMenuCloseRect_, L"閉じる", false);
        }

        if (previewLogVisible_)
        {
            RECT titleRect = { previewMenuPanelRect_.left + 20, previewMenuPanelRect_.top + 18, previewMenuPanelRect_.right - 100, previewMenuPanelRect_.top + 46 };
            SetTextColor(hdc, RGB(244, 248, 252));
            DrawWrappedText(hdc, titleRect, L"バックログ", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            previewLogCloseRect_ = { previewMenuPanelRect_.right - 92, previewMenuPanelRect_.top + 16, previewMenuPanelRect_.right - 20, previewMenuPanelRect_.top + 42 };
            HBRUSH closeBrush = CreateSolidBrush(RGB(38, 48, 62));
            FillRect(hdc, &previewLogCloseRect_, closeBrush);
            DeleteObject(closeBrush);
            FrameRect(hdc, &previewLogCloseRect_, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            DrawWrappedText(hdc, previewLogCloseRect_, L"閉じる", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            RECT logRect = { previewMenuPanelRect_.left + 20, previewMenuPanelRect_.top + 56, previewMenuPanelRect_.right - 20, previewMenuPanelRect_.bottom - 20 };
            std::wstring logText;
            const size_t start = backlogEntries_.size() > 24 ? backlogEntries_.size() - 24 : 0;
            for (size_t i = start; i < backlogEntries_.size(); ++i)
            {
                if (!logText.empty())
                {
                    logText += L"\r\n";
                }
                logText += backlogEntries_[i];
            }
            if (logText.empty())
            {
                logText = L"まだログがありません";
            }
            SetTextColor(hdc, RGB(220, 228, 236));
            DrawWrappedText(hdc, logRect, logText, DT_LEFT | DT_WORDBREAK);
        }
    }

    auto drawOverlayAlpha = [&](const RECT& targetRect, COLORREF color, int alpha)
    {
        DrawAlphaOverlay(hdc, targetRect, color, alpha);
    };

    if (tintOpacity_ > 0)
    {
        drawOverlayAlpha(stageRect, tintColor_, tintOpacity_);
    }

    if (fadeEndTick_ != 0 && fadeEndTick_ > fadeStartTick_)
    {
        const DWORD now = GetTickCount();
        const double progress = static_cast<double>((std::min)(now, fadeEndTick_) - fadeStartTick_) / static_cast<double>((std::max<DWORD>)(1, fadeEndTick_ - fadeStartTick_));
        RECT fadeRect = stageRect;
        if (fadeTarget_ == L"message" && HasVisibleArea(messageRect))
        {
            fadeRect = messageRect;
        }
        else if (fadeTarget_ == L"all")
        {
            fadeRect = clientRect;
        }
        else if (fadeTarget_ == L"background")
        {
            fadeRect = {};
        }
        else if (fadeTarget_ == L"character:left" || fadeTarget_ == L"character:center" || fadeTarget_ == L"character:right")
        {
            const CharacterSlot* slot = &centerCharacter_;
            int centerX = stageRect.left + stageWidth / 2 + stageOffsetX_;
            if (fadeTarget_ == L"character:left")
            {
                slot = &leftCharacter_;
                centerX = stageRect.left + stageWidth / 4 + stageOffsetX_;
            }
            else if (fadeTarget_ == L"character:right")
            {
                slot = &rightCharacter_;
                centerX = stageRect.left + (stageWidth * 3) / 4 + stageOffsetX_;
            }

            if (!slot->visible || (slot->displayName.empty() && !slot->image))
            {
                fadeRect = {};
            }
            else
            {
                const int slotWidth = (260 * slot->scale * stageScale_) / 10000;
                const int slotHeight = (520 * slot->scale * stageScale_) / 10000;
                fadeRect =
                {
                    centerX - slotWidth / 2 + slot->offsetX,
                    stageRect.bottom - slotHeight - 20 + slot->offsetY,
                    centerX + slotWidth / 2 + slot->offsetX,
                    stageRect.bottom - 20 + slot->offsetY
                };
            }
        }
        drawOverlayAlpha(fadeRect, fadeColor_, static_cast<int>((1.0 - progress) * fadeOpacity_));
    }

    if (flashEndTick_ != 0 && flashEndTick_ > flashStartTick_)
    {
        const DWORD now = GetTickCount();
        const double progress = static_cast<double>((std::min)(now, flashEndTick_) - flashStartTick_) / static_cast<double>((std::max<DWORD>)(1, flashEndTick_ - flashStartTick_));
        drawOverlayAlpha(stageRect, flashColor_, static_cast<int>((1.0 - progress) * flashOpacity_));
    }

    SelectObject(hdc, originalFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
    DeleteObject(speakerFont);
    DeleteObject(hintFont);
}

void NovelRuntime::DrawPreviewWindow(HDC hdc, const RECT& clientRect)
{
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 14, 18));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    DrawPreviewSurface(hdc, clientRect, true);
    DrawToast(hdc, clientRect);
}

void NovelRuntime::DrawToolbar(HDC hdc, const RECT& previewRect)
{
    toolbarButtonRects_.clear();

    const RECT toolbarRect = GetToolbarRect(previewRect);
    HBRUSH toolbarBrush = CreateSolidBrush(RGB(62, 164, 222));
    FillRect(hdc, &toolbarRect, toolbarBrush);
    DeleteObject(toolbarBrush);
    FrameRect(hdc, &toolbarRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT buttonFont = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    RECT logoRect = { toolbarRect.left + 10, toolbarRect.top + 6, toolbarRect.left + 34, toolbarRect.top + 30 };
    HBRUSH logoBrush = CreateSolidBrush(RGB(42, 118, 178));
    FillRect(hdc, &logoRect, logoBrush);
    DeleteObject(logoBrush);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, logoRect, L"K", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const std::wstring projectName = scenarioPath_.empty() ? L"Kaktos Project" : GetFileStemPart(scenarioPath_);
    RECT titleRect = { toolbarRect.left + 42, toolbarRect.top + 2, toolbarRect.left + 340, toolbarRect.bottom - 2 };
    DrawWrappedText(hdc, titleRect, projectName, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, buttonFont);
    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    int buttonRight = toolbarRect.right - 10;
    const int buttonWidth = 30;
    const int buttonHeight = 30;
    for (size_t index = toolbarItems_.size(); index-- > 0;)
    {
        ToolbarItem& item = toolbarItems_[index];
        RECT buttonRect = {
            buttonRight - buttonWidth,
            toolbarRect.top + 8,
            buttonRight,
            toolbarRect.top + 8 + buttonHeight
        };
        toolbarButtonRects_.insert(toolbarButtonRects_.begin(), buttonRect);
        buttonRight -= buttonWidth + 10;

        const bool active = item.id == L"preview" && previewVisible_;
        const bool hovered = static_cast<int>(index) == hoveredToolbarIndex_;
        HBRUSH buttonBrush = CreateSolidBrush(active ? RGB(238, 248, 255) : (hovered ? RGB(200, 233, 248) : RGB(62, 164, 222)));
        FillRect(hdc, &buttonRect, buttonBrush);
        DeleteObject(buttonBrush);
        if (!item.iconImage)
        {
            std::vector<std::wstring> candidates;
            const std::wstring assetsRoot = GetAssetsRootDirectory();
            candidates.push_back(CombinePath(assetsRoot, item.iconPath));
            candidates.push_back(item.iconPath);
            for (const std::wstring& candidate : candidates)
            {
                auto image = TryLoadImage(candidate);
                if (image)
                {
                    item.iconImage = std::move(image);
                    break;
                }
            }
        }
        if (item.iconImage)
        {
            const int iconSize = 18;
            const int iconTop = buttonRect.top + (buttonHeight - iconSize) / 2;
            graphics.DrawImage(item.iconImage.get(), Gdiplus::Rect(buttonRect.left + 6, iconTop, iconSize, iconSize));
        }
        else
        {
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawWrappedText(hdc, buttonRect, item.label.substr(0, 1), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }

    if (hoveredToolbarIndex_ >= 0 && hoveredToolbarIndex_ < static_cast<int>(toolbarItems_.size()) && hoveredToolbarIndex_ < static_cast<int>(toolbarButtonRects_.size()))
    {
        const RECT anchor = toolbarButtonRects_[hoveredToolbarIndex_];
        RECT tipRect = { anchor.left - 28, anchor.bottom + 4, anchor.right + 28, anchor.bottom + 24 };
        HBRUSH tipBrush = CreateSolidBrush(RGB(34, 40, 48));
        FillRect(hdc, &tipRect, tipBrush);
        DeleteObject(tipBrush);
        FrameRect(hdc, &tipRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetTextColor(hdc, RGB(242, 242, 240));
        DrawWrappedText(hdc, tipRect, toolbarItems_[hoveredToolbarIndex_].label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(buttonFont);
}

void NovelRuntime::Draw(HDC hdc, const RECT& clientRect)
{
    lastClientRect_ = clientRect;
    commandRowRects_.clear();
    commandRowIndices_.clear();
    graphNodeRects_.clear();
    graphNodeIndices_.clear();
    eventRowRects_.clear();
    eventRowIndices_.clear();
    eventExpandRects_.clear();
    eventExpandIndices_.clear();
    toolbarButtonRects_.clear();
    currentEventListRect_ = {};
    currentInspectorRect_ = {};
    eventAddTextRect_ = {};
    eventAddChoiceRect_ = {};
    eventValidateRect_ = {};
    eventDuplicateRect_ = {};
    eventDeleteRect_ = {};

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(10, 14, 18));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (projectLauncherVisible_)
    {
        DrawProjectLauncher(hdc, clientRect);
        if (projectDialogVisible_)
        {
            DrawProjectDialog(hdc, clientRect);
        }
        UpdateChildControls();
        return;
    }

    const RECT leftPanelRect = GetLeftPanelRect(clientRect);
    const RECT rightPanelRect = GetRightPanelRect(clientRect);
    const RECT previewRect = GetPreviewRect(clientRect);
    const RECT graphRect = GetGraphRect(previewRect);
    const RECT eventRect = GetEventListRect(previewRect);

    HBRUSH leftBrush = CreateSolidBrush(RGB(18, 24, 32));
    HBRUSH rightBrush = CreateSolidBrush(RGB(18, 24, 32));
    HBRUSH centerBrush = CreateSolidBrush(RGB(14, 20, 28));
    if (HasVisibleArea(leftPanelRect))
    {
        FillRect(hdc, &leftPanelRect, leftBrush);
    }
    if (HasVisibleArea(rightPanelRect))
    {
        FillRect(hdc, &rightPanelRect, rightBrush);
    }
    if (HasVisibleArea(previewRect))
    {
        FillRect(hdc, &previewRect, centerBrush);
    }
    DeleteObject(leftBrush);
    DeleteObject(rightBrush);
    DeleteObject(centerBrush);

    leftSplitterRect_ = showComponents_ ? RECT{ leftPanelRect.right, clientRect.top + 12, leftPanelRect.right + 6, clientRect.bottom - 12 } : RECT{};
    rightSplitterRect_ = showInspector_ ? RECT{ rightPanelRect.left - 6, clientRect.top + 12, rightPanelRect.left, clientRect.bottom - 12 } : RECT{};
    graphSplitterRect_ = showFlowGraph_ ? RECT{ graphRect.left, graphRect.bottom + 6, graphRect.right, graphRect.bottom + 10 } : RECT{};
    eventSplitterRect_ = showEventList_ ? RECT{ eventRect.left, eventRect.bottom + 6, eventRect.right, eventRect.bottom + 10 } : RECT{};

    if (showComponents_ && HasVisibleArea(leftPanelRect))
    {
        DrawCommandPalette(hdc, leftPanelRect);
        DrawSplitter(hdc, leftSplitterRect_, activeDragHandle_ == DragHandle::LeftPanel);
    }
    if (showInspector_ && HasVisibleArea(rightPanelRect))
    {
        currentInspectorRect_ = rightPanelRect;
        DrawInspector(hdc, rightPanelRect);
        DrawSplitter(hdc, rightSplitterRect_, activeDragHandle_ == DragHandle::RightPanel);
    }
    if (HasVisibleArea(previewRect))
    {
        DrawToolbar(hdc, previewRect);
        if (showFlowGraph_ && HasVisibleArea(graphRect))
        {
            DrawNodeGraph(hdc, graphRect);
            DrawSplitter(hdc, graphSplitterRect_, activeDragHandle_ == DragHandle::GraphHeight);
        }
        if (showEventList_ && HasVisibleArea(eventRect))
        {
            DrawEventList(hdc, eventRect);
        }
    }

    if (paletteDragActive_)
    {
        RECT dragRect = { dragPoint_.x + 12, dragPoint_.y + 12, dragPoint_.x + 144, dragPoint_.y + 40 };
        HBRUSH dragBrush = CreateSolidBrush(RGB(52, 60, 70));
        FillRect(hdc, &dragRect, dragBrush);
        DeleteObject(dragBrush);
        HBRUSH dragFrame = CreateSolidBrush(RGB(112, 184, 255));
        FrameRect(hdc, &dragRect, dragFrame);
        DeleteObject(dragFrame);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(236, 242, 248));
        DrawWrappedText(hdc, dragRect, draggedPaletteLabel_, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    else if (eventReorderDragActive_ && eventReorderMoved_ && eventDragSourceIndex_ < scenario_.commands.size())
    {
        RECT dragRect = { dragPoint_.x + 12, dragPoint_.y + 12, dragPoint_.x + 220, dragPoint_.y + 40 };
        HBRUSH dragBrush = CreateSolidBrush(RGB(52, 60, 70));
        FillRect(hdc, &dragRect, dragBrush);
        DeleteObject(dragBrush);
        HBRUSH dragFrame = CreateSolidBrush(RGB(112, 184, 255));
        FrameRect(hdc, &dragRect, dragFrame);
        DeleteObject(dragFrame);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(236, 242, 248));
        DrawWrappedText(hdc, dragRect, GetCommandTypeLabel(scenario_.commands[eventDragSourceIndex_]), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    else if (assetDragActive_ && assetDragMoved_ && assetDragSourceIndex_ < assetItems_.size())
    {
        RECT dragRect = { dragPoint_.x + 12, dragPoint_.y + 12, dragPoint_.x + 220, dragPoint_.y + 40 };
        HBRUSH dragBrush = CreateSolidBrush(RGB(52, 60, 70));
        FillRect(hdc, &dragRect, dragBrush);
        DeleteObject(dragBrush);
        HBRUSH dragFrame = CreateSolidBrush(RGB(112, 220, 148));
        FrameRect(hdc, &dragRect, dragFrame);
        DeleteObject(dragFrame);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(236, 242, 248));
        DrawWrappedText(hdc, dragRect, assetItems_[assetDragSourceIndex_].label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    if (sceneDialogVisible_)
    {
        DrawSceneCreateDialog(hdc, clientRect);
    }
    if (projectDialogVisible_)
    {
        DrawProjectDialog(hdc, clientRect);
    }
    if (characterManagerVisible_)
    {
        DrawCharacterManagerDialog(hdc, clientRect);
    }
    if (variableManagerVisible_)
    {
        DrawVariableManagerDialog(hdc, clientRect);
    }
    if (settingsDialogVisible_)
    {
        DrawSettingsDialog(hdc, clientRect);
    }
    DrawToast(hdc, clientRect);
    UpdateChildControls();
}

void NovelRuntime::DrawSceneCreateDialog(HDC hdc, const RECT& clientRect)
{
    const int dialogWidth = 700;
    const int dialogHeight = 170;
    sceneDialogRect_ =
    {
        clientRect.left + ((clientRect.right - clientRect.left) - dialogWidth) / 2,
        clientRect.top + 70,
        clientRect.left + ((clientRect.right - clientRect.left) + dialogWidth) / 2,
        clientRect.top + 70 + dialogHeight
    };

    HBRUSH overlayBrush = CreateSolidBrush(RGB(32, 36, 42));
    FillRect(hdc, &clientRect, overlayBrush);
    DeleteObject(overlayBrush);

    HBRUSH dialogBrush = CreateSolidBrush(RGB(245, 245, 241));
    FillRect(hdc, &sceneDialogRect_, dialogBrush);
    DeleteObject(dialogBrush);
    FrameRect(hdc, &sceneDialogRect_, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    RECT headerRect = { sceneDialogRect_.left, sceneDialogRect_.top, sceneDialogRect_.right, sceneDialogRect_.top + 34 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(76, 74, 68));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT headerTextRect = { headerRect.left + 10, headerRect.top, headerRect.right - 10, headerRect.bottom };
    DrawWrappedText(hdc, headerTextRect, L"新規シナリオファイル作成", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect = { sceneDialogRect_.left + 36, sceneDialogRect_.top + 56, sceneDialogRect_.left + 160, sceneDialogRect_.top + 86 };
    SetTextColor(hdc, RGB(72, 72, 72));
    DrawWrappedText(hdc, labelRect, L"新規シナリオ名", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    sceneDialogEditRect_ = { sceneDialogRect_.left + 138, sceneDialogRect_.top + 50, sceneDialogRect_.right - 24, sceneDialogRect_.top + 82 };
    sceneDialogCancelRect_ = { sceneDialogRect_.left + 278, sceneDialogRect_.bottom - 50, sceneDialogRect_.left + 358, sceneDialogRect_.bottom - 18 };
    sceneDialogCreateRect_ = { sceneDialogRect_.left + 370, sceneDialogRect_.bottom - 50, sceneDialogRect_.left + 420, sceneDialogRect_.bottom - 18 };

    HBRUSH cancelBrush = CreateSolidBrush(RGB(210, 210, 206));
    HBRUSH createBrush = CreateSolidBrush(RGB(79, 169, 230));
    FillRect(hdc, &sceneDialogCancelRect_, cancelBrush);
    FillRect(hdc, &sceneDialogCreateRect_, createBrush);
    DeleteObject(cancelBrush);
    DeleteObject(createBrush);
    FrameRect(hdc, &sceneDialogCancelRect_, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    FrameRect(hdc, &sceneDialogCreateRect_, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    SetTextColor(hdc, RGB(64, 64, 64));
    DrawWrappedText(hdc, sceneDialogCancelRect_, L"キャンセル", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, sceneDialogCreateRect_, L"作成", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void NovelRuntime::DrawProjectLauncher(HDC hdc, const RECT& clientRect)
{
    projectLauncherRows_.clear();
    projectLauncherPanelRect_ = {};
    projectLauncherCreateRect_ = {};
    projectLauncherOpenRect_ = {};

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    const int panelWidth = (std::min)(clientWidth - 64, 920);
    const int panelHeight = (std::min)(clientHeight - 70, 620);
    projectLauncherPanelRect_ =
    {
        clientRect.left + (clientWidth - panelWidth) / 2,
        clientRect.top + 44,
        clientRect.left + (clientWidth + panelWidth) / 2,
        clientRect.top + 44 + panelHeight
    };

    HBRUSH shadowBrush = CreateSolidBrush(RGB(7, 9, 12));
    RECT shadowRect = { projectLauncherPanelRect_.left + 8, projectLauncherPanelRect_.top + 8, projectLauncherPanelRect_.right + 8, projectLauncherPanelRect_.bottom + 8 };
    FillRect(hdc, &shadowRect, shadowBrush);
    DeleteObject(shadowBrush);

    HBRUSH panelBrush = CreateSolidBrush(RGB(30, 35, 43));
    FillRect(hdc, &projectLauncherPanelRect_, panelBrush);
    DeleteObject(panelBrush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(72, 84, 98));
    FrameRect(hdc, &projectLauncherPanelRect_, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hdc, TRANSPARENT);
    RECT titleRect = { projectLauncherPanelRect_.left + 28, projectLauncherPanelRect_.top + 24, projectLauncherPanelRect_.right - 320, projectLauncherPanelRect_.top + 62 };
    SetTextColor(hdc, RGB(232, 238, 246));
    DrawWrappedText(hdc, titleRect, L"プロジェクト一覧", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT logoRect = { projectLauncherPanelRect_.right - 320, projectLauncherPanelRect_.top + 20, projectLauncherPanelRect_.right - 30, projectLauncherPanelRect_.top + 74 };
    SetTextColor(hdc, RGB(96, 190, 244));
    DrawWrappedText(hdc, logoRect, L"Kaktos Engine", DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    projectLauncherCreateRect_ = { projectLauncherPanelRect_.left + 28, projectLauncherPanelRect_.top + 92, projectLauncherPanelRect_.left + 218, projectLauncherPanelRect_.top + 128 };
    projectLauncherOpenRect_ = { projectLauncherPanelRect_.left + 230, projectLauncherPanelRect_.top + 92, projectLauncherPanelRect_.left + 390, projectLauncherPanelRect_.top + 128 };

    auto drawButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        HBRUSH frameBrush = CreateSolidBrush(RGB(90, 116, 136));
        FrameRect(hdc, &rect, frameBrush);
        DeleteObject(frameBrush);
        SetTextColor(hdc, RGB(246, 250, 255));
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };

    drawButton(projectLauncherCreateRect_, L"+ 新規プロジェクト作成", RGB(42, 142, 202));
    drawButton(projectLauncherOpenRect_, L"プロジェクトを開く", RGB(52, 62, 74));

    const int tableLeft = projectLauncherPanelRect_.left + 28;
    const int tableRight = projectLauncherPanelRect_.right - 28;
    const int tableTop = projectLauncherPanelRect_.top + 150;
    const int headerHeight = 36;
    RECT headerRect = { tableLeft, tableTop, tableRight, tableTop + headerHeight };
    HBRUSH headerBrush = CreateSolidBrush(RGB(64, 68, 64));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    const int dataLeft = tableRight - 138;
    const int deleteLeft = tableRight - 64;
    const int typeLeft = tableLeft + ((tableRight - tableLeft) * 52) / 100;

    SetTextColor(hdc, RGB(248, 250, 252));
    DrawWrappedText(hdc, RECT{ tableLeft + 14, headerRect.top, typeLeft - 8, headerRect.bottom }, L"プロジェクト", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawWrappedText(hdc, RECT{ typeLeft, headerRect.top, dataLeft - 8, headerRect.bottom }, L"種類", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawWrappedText(hdc, RECT{ dataLeft, headerRect.top, deleteLeft - 8, headerRect.bottom }, L"データ", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DrawWrappedText(hdc, RECT{ deleteLeft, headerRect.top, tableRight, headerRect.bottom }, L"削除", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const int rowHeight = 42;
    int y = headerRect.bottom;
    if (recentProjects_.empty())
    {
        RECT emptyRect = { tableLeft + 14, y + 18, tableRight - 14, y + 58 };
        SetTextColor(hdc, RGB(154, 166, 180));
        DrawWrappedText(hdc, emptyRect, L"最近使ったプロジェクトはまだありません。新規作成またはプロジェクトを開くから開始してください。", DT_LEFT | DT_WORDBREAK | DT_VCENTER);
    }

    for (const std::wstring& projectPath : recentProjects_)
    {
        if (y + rowHeight > projectLauncherPanelRect_.bottom - 28)
        {
            break;
        }

        ProjectLauncherRow row;
        row.projectPath = projectPath;
        row.rowRect = { tableLeft, y, tableRight, y + rowHeight };
        row.dataRect = { dataLeft + 16, y + 8, dataLeft + 46, y + 34 };
        row.deleteRect = { deleteLeft + 18, y + 8, deleteLeft + 48, y + 34 };

        const bool exists = GetFileAttributesW(projectPath.c_str()) != INVALID_FILE_ATTRIBUTES;
        HBRUSH rowBrush = CreateSolidBrush(projectLauncherRows_.size() % 2 == 0 ? RGB(34, 40, 48) : RGB(30, 36, 44));
        FillRect(hdc, &row.rowRect, rowBrush);
        DeleteObject(rowBrush);
        HBRUSH lineBrush = CreateSolidBrush(RGB(68, 76, 86));
        FrameRect(hdc, &row.rowRect, lineBrush);
        DeleteObject(lineBrush);

        const std::wstring projectRoot = GetProjectRootFromProjectPath(projectPath);
        const std::wstring displayName = projectRoot.empty() ? GetFileStemPart(projectPath) : GetFileNamePart(projectRoot);
        SetTextColor(hdc, exists ? RGB(90, 190, 244) : RGB(128, 138, 148));
        DrawWrappedText(hdc, RECT{ tableLeft + 14, y, typeLeft - 8, y + rowHeight }, displayName.empty() ? GetFileNamePart(projectPath) : displayName, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, exists ? RGB(214, 220, 228) : RGB(128, 138, 148));
        DrawWrappedText(hdc, RECT{ typeLeft, y, dataLeft - 8, y + rowHeight }, L"Kaktos Engine Project", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, exists ? RGB(92, 192, 242) : RGB(128, 138, 148));
        DrawWrappedText(hdc, row.dataRect, L"□", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, RGB(242, 102, 118));
        DrawWrappedText(hdc, row.deleteRect, L"×", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        projectLauncherRows_.push_back(row);
        y += rowHeight;
    }
}

void NovelRuntime::DrawProjectDialog(HDC hdc, const RECT& clientRect)
{
    const int dialogWidth = 760;
    const int dialogHeight = 240;
    projectDialogRect_ =
    {
        clientRect.left + ((clientRect.right - clientRect.left) - dialogWidth) / 2,
        clientRect.top + 78,
        clientRect.left + ((clientRect.right - clientRect.left) + dialogWidth) / 2,
        clientRect.top + 78 + dialogHeight
    };

    HBRUSH overlayBrush = CreateSolidBrush(RGB(18, 22, 28));
    FillRect(hdc, &clientRect, overlayBrush);
    DeleteObject(overlayBrush);

    HBRUSH dialogBrush = CreateSolidBrush(RGB(34, 40, 48));
    FillRect(hdc, &projectDialogRect_, dialogBrush);
    DeleteObject(dialogBrush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(82, 92, 104));
    FrameRect(hdc, &projectDialogRect_, borderBrush);
    DeleteObject(borderBrush);

    RECT headerRect = { projectDialogRect_.left, projectDialogRect_.top, projectDialogRect_.right, projectDialogRect_.top + 40 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(58, 56, 52));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(246, 248, 250));
    RECT titleRect = { headerRect.left + 14, headerRect.top, headerRect.right - 14, headerRect.bottom };
    DrawWrappedText(hdc, titleRect, L"プロジェクト", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT descriptionRect = { projectDialogRect_.left + 28, projectDialogRect_.top + 58, projectDialogRect_.right - 28, projectDialogRect_.top + 88 };
    SetTextColor(hdc, RGB(202, 210, 220));
    DrawWrappedText(hdc, descriptionRect, L"新しいプロジェクトを作成するか、既存の .kproj を読み込みます。", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect = { projectDialogRect_.left + 36, projectDialogRect_.top + 112, projectDialogRect_.left + 190, projectDialogRect_.top + 142 };
    SetTextColor(hdc, RGB(226, 232, 240));
    DrawWrappedText(hdc, labelRect, L"新規プロジェクト名", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    projectDialogEditRect_ = { projectDialogRect_.left + 196, projectDialogRect_.top + 108, projectDialogRect_.right - 34, projectDialogRect_.top + 142 };
    RECT editFrameRect = { projectDialogEditRect_.left - 1, projectDialogEditRect_.top - 1, projectDialogEditRect_.right + 1, projectDialogEditRect_.bottom + 1 };
    HBRUSH editFrameBrush = CreateSolidBrush(RGB(86, 150, 220));
    FrameRect(hdc, &editFrameRect, editFrameBrush);
    DeleteObject(editFrameBrush);

    projectDialogOpenRect_ = { projectDialogRect_.left + 196, projectDialogRect_.bottom - 62, projectDialogRect_.left + 316, projectDialogRect_.bottom - 24 };
    projectDialogCancelRect_ = { projectDialogRect_.left + 330, projectDialogRect_.bottom - 62, projectDialogRect_.left + 450, projectDialogRect_.bottom - 24 };
    projectDialogCreateRect_ = { projectDialogRect_.right - 154, projectDialogRect_.bottom - 62, projectDialogRect_.right - 34, projectDialogRect_.bottom - 24 };

    auto drawButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill, COLORREF text)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        HBRUSH frameBrush = CreateSolidBrush(RGB(100, 112, 126));
        FrameRect(hdc, &rect, frameBrush);
        DeleteObject(frameBrush);
        SetTextColor(hdc, text);
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };

    drawButton(projectDialogOpenRect_, L"読み込み", RGB(48, 56, 66), RGB(236, 242, 248));
    drawButton(projectDialogCancelRect_, L"閉じる", RGB(48, 56, 66), RGB(236, 242, 248));
    drawButton(projectDialogCreateRect_, L"作成", RGB(50, 150, 210), RGB(255, 255, 255));

    RECT noteRect = { projectDialogRect_.left + 36, projectDialogRect_.bottom - 98, projectDialogRect_.right - 34, projectDialogRect_.bottom - 72 };
    SetTextColor(hdc, RGB(160, 170, 182));
    DrawWrappedText(hdc, noteRect, L"作成時に assets / scenario / ui / textbox / fonts などの基本フォルダを自動で用意します。", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void NovelRuntime::DrawCharacterManagerDialog(HDC hdc, const RECT& clientRect)
{
    characterDefinitionRects_.clear();
    characterManagerActionTargets_.clear();
    characterDialogRect_ = { clientRect.left + 28, clientRect.top + 24, clientRect.left + 880, clientRect.top + 610 };
    HBRUSH overlayBrush = CreateSolidBrush(RGB(24, 28, 34));
    FillRect(hdc, &clientRect, overlayBrush);
    DeleteObject(overlayBrush);

    HBRUSH dialogBrush = CreateSolidBrush(RGB(36, 42, 50));
    FillRect(hdc, &characterDialogRect_, dialogBrush);
    DeleteObject(dialogBrush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(74, 82, 92));
    FrameRect(hdc, &characterDialogRect_, borderBrush);
    DeleteObject(borderBrush);

    RECT headerRect = { characterDialogRect_.left, characterDialogRect_.top, characterDialogRect_.right, characterDialogRect_.top + 40 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(58, 56, 52));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);
    RECT titleRect = { headerRect.left + 10, headerRect.top, headerRect.right - 50, headerRect.bottom };
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, titleRect, L"キャラクター管理", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    characterDialogCloseRect_ = { headerRect.right - 30, headerRect.top + 8, headerRect.right - 10, headerRect.top + 28 };
    DrawWrappedText(hdc, characterDialogCloseRect_, L"×", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    auto drawButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        HBRUSH frameBrush = CreateSolidBrush(RGB(98, 106, 118));
        FrameRect(hdc, &rect, frameBrush);
        DeleteObject(frameBrush);
        SetTextColor(hdc, RGB(240, 244, 248));
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };

    RECT listTitleRect = { characterDialogRect_.left + 16, characterDialogRect_.top + 52, characterDialogRect_.left + 220, characterDialogRect_.top + 74 };
    SetTextColor(hdc, RGB(210, 216, 224));
    DrawWrappedText(hdc, listTitleRect, L"キャラクター一覧", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    characterDialogEditRect_ = { characterDialogRect_.left + 16, characterDialogRect_.top + 82, characterDialogRect_.left + 176, characterDialogRect_.top + 112 };
    characterDialogAddRect_ = { characterDialogRect_.left + 182, characterDialogRect_.top + 82, characterDialogRect_.left + 232, characterDialogRect_.top + 112 };
    drawButton(characterDialogAddRect_, L"追加", RGB(70, 118, 178));

    RECT listRect = { characterDialogRect_.left + 16, characterDialogRect_.top + 120, characterDialogRect_.left + 250, characterDialogRect_.bottom - 78 };
    HBRUSH listBrush = CreateSolidBrush(RGB(26, 32, 38));
    FillRect(hdc, &listRect, listBrush);
    DeleteObject(listBrush);
    HBRUSH listBorderBrush = CreateSolidBrush(RGB(72, 80, 90));
    FrameRect(hdc, &listRect, listBorderBrush);
    DeleteObject(listBorderBrush);

    int rowY = listRect.top + 4;
    for (size_t i = 0; i < characterDefinitions_.size(); ++i)
    {
        RECT rowRect = { listRect.left + 4, rowY, listRect.right - 4, rowY + 28 };
        characterDefinitionRects_.push_back(rowRect);
        if (i == selectedCharacterDefinitionIndex_)
        {
            HBRUSH selectedBrush = CreateSolidBrush(RGB(70, 102, 144));
            FillRect(hdc, &rowRect, selectedBrush);
            DeleteObject(selectedBrush);
        }
        SetTextColor(hdc, RGB(224, 230, 236));
        DrawWrappedText(hdc, rowRect, GetCharacterDefinitionLabel(characterDefinitions_[i]), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        rowY += 30;
    }

    characterDialogDeleteRect_ = { characterDialogRect_.left + 16, characterDialogRect_.bottom - 64, characterDialogRect_.left + 132, characterDialogRect_.bottom - 34 };
    drawButton(characterDialogDeleteRect_, L"キャラクター削除", RGB(120, 70, 78));

    RECT detailRect = { characterDialogRect_.left + 270, characterDialogRect_.top + 52, characterDialogRect_.right - 20, characterDialogRect_.bottom - 24 };
    HBRUSH detailBrush = CreateSolidBrush(RGB(30, 36, 44));
    FillRect(hdc, &detailRect, detailBrush);
    DeleteObject(detailBrush);
    HBRUSH detailBorderBrush = CreateSolidBrush(RGB(74, 82, 92));
    FrameRect(hdc, &detailRect, detailBorderBrush);
    DeleteObject(detailBorderBrush);

    if (selectedCharacterDefinitionIndex_ < characterDefinitions_.size())
    {
        CharacterDefinition& definition = characterDefinitions_[selectedCharacterDefinitionIndex_];
        int cursorY = detailRect.top + 16;
        RECT headingRect = { detailRect.left + 16, cursorY, detailRect.right - 16, cursorY + 24 };
        SetTextColor(hdc, RGB(238, 242, 246));
        DrawWrappedText(hdc, headingRect, GetCharacterDefinitionLabel(definition), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cursorY += 34;

        auto drawFieldRow = [&](const std::wstring& label, const std::wstring& value, const std::wstring& action)
        {
            RECT labelRect = { detailRect.left + 16, cursorY, detailRect.left + 110, cursorY + 24 };
            RECT valueRect = { detailRect.left + 116, cursorY, detailRect.right - 120, cursorY + 24 };
            RECT buttonRect = { detailRect.right - 104, cursorY - 2, detailRect.right - 18, cursorY + 26 };
            SetTextColor(hdc, RGB(188, 196, 204));
            DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SetTextColor(hdc, RGB(232, 236, 240));
            DrawWrappedText(hdc, valueRect, value.empty() ? L"(未設定)" : value, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            drawButton(buttonRect, L"編集", RGB(78, 92, 108));
            characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ action, selectedCharacterDefinitionIndex_, static_cast<size_t>(-1), buttonRect });
            cursorY += 34;
        };

        drawFieldRow(L"キャラID", definition.id, L"edit_id");
        drawFieldRow(L"表示名", definition.displayName, L"edit_display_name");
        drawFieldRow(L"テーマ色", definition.color, L"edit_color");

        RECT imageTitleRect = { detailRect.left + 16, cursorY, detailRect.right - 16, cursorY + 22 };
        SetTextColor(hdc, RGB(188, 196, 204));
        DrawWrappedText(hdc, imageTitleRect, L"基準画像", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cursorY += 26;

        RECT previewRect = { detailRect.left + 16, cursorY, detailRect.left + 220, cursorY + 236 };
        HBRUSH previewBrush = CreateSolidBrush(RGB(220, 224, 228));
        FillRect(hdc, &previewRect, previewBrush);
        DeleteObject(previewBrush);
        HBRUSH previewBorder = CreateSolidBrush(RGB(90, 96, 102));
        FrameRect(hdc, &previewRect, previewBorder);
        DeleteObject(previewBorder);
        if (!definition.baseImagePath.empty())
        {
            std::unique_ptr<Gdiplus::Image> previewImage = TryLoadImage(definition.baseImagePath);
            if (!previewImage)
            {
                previewImage = TryLoadImage(CombinePath(scenarioBaseDir_, definition.baseImagePath));
            }
            if (previewImage)
            {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(previewImage.get(), Gdiplus::Rect(previewRect.left + 4, previewRect.top + 4, (previewRect.right - previewRect.left) - 8, (previewRect.bottom - previewRect.top) - 8));
            }
        }
        else
        {
            RECT emptyRect = { previewRect.left + 10, previewRect.top + 10, previewRect.right - 10, previewRect.bottom - 10 };
            SetTextColor(hdc, RGB(90, 96, 104));
            DrawWrappedText(hdc, emptyRect, L"画像未設定", DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }

        RECT imagePathRect = { previewRect.right + 16, previewRect.top, detailRect.right - 20, previewRect.top + 42 };
        SetTextColor(hdc, RGB(224, 228, 232));
        DrawWrappedText(hdc, imagePathRect, definition.baseImagePath.empty() ? L"(未設定)" : definition.baseImagePath, DT_LEFT | DT_WORDBREAK);
        RECT browseBaseRect = { previewRect.right + 16, previewRect.top + 58, previewRect.right + 136, previewRect.top + 88 };
        RECT clearBaseRect = { previewRect.right + 146, previewRect.top + 58, previewRect.right + 230, previewRect.top + 88 };
        drawButton(browseBaseRect, L"画像参照", RGB(70, 118, 178));
        drawButton(clearBaseRect, L"解除", RGB(92, 80, 88));
        characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"browse_base", selectedCharacterDefinitionIndex_, static_cast<size_t>(-1), browseBaseRect });
        characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"clear_base", selectedCharacterDefinitionIndex_, static_cast<size_t>(-1), clearBaseRect });
        cursorY = previewRect.bottom + 20;

        RECT exprTitleRect = { detailRect.left + 16, cursorY, detailRect.right - 130, cursorY + 22 };
        SetTextColor(hdc, RGB(188, 196, 204));
        DrawWrappedText(hdc, exprTitleRect, L"表情差分", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT addExpressionRect = { detailRect.right - 112, cursorY - 2, detailRect.right - 18, cursorY + 26 };
        drawButton(addExpressionRect, L"差分追加", RGB(92, 112, 72));
        characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"add_expression", selectedCharacterDefinitionIndex_, static_cast<size_t>(-1), addExpressionRect });
        cursorY += 30;

        for (size_t expressionIndex = 0; expressionIndex < definition.expressions.size(); ++expressionIndex)
        {
            const CharacterExpressionDefinition& expression = definition.expressions[expressionIndex];
            RECT rowRect = { detailRect.left + 16, cursorY, detailRect.right - 16, cursorY + 30 };
            HBRUSH rowBrush = CreateSolidBrush(RGB(38, 46, 56));
            FillRect(hdc, &rowRect, rowBrush);
            DeleteObject(rowBrush);
            HBRUSH rowBorder = CreateSolidBrush(RGB(70, 78, 88));
            FrameRect(hdc, &rowRect, rowBorder);
            DeleteObject(rowBorder);
            RECT nameRect = { rowRect.left + 10, rowRect.top, rowRect.left + 160, rowRect.bottom };
            RECT pathRect = { rowRect.left + 164, rowRect.top, rowRect.right - 268, rowRect.bottom };
            RECT editRect = { rowRect.right - 258, rowRect.top + 2, rowRect.right - 198, rowRect.bottom - 2 };
            RECT browseRect = { rowRect.right - 192, rowRect.top + 2, rowRect.right - 112, rowRect.bottom - 2 };
            RECT clearRect = { rowRect.right - 106, rowRect.top + 2, rowRect.right - 58, rowRect.bottom - 2 };
            RECT removeRect = { rowRect.right - 52, rowRect.top + 2, rowRect.right - 6, rowRect.bottom - 2 };
            SetTextColor(hdc, RGB(236, 240, 244));
            DrawWrappedText(hdc, nameRect, expression.name.empty() ? L"差分" : expression.name, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SetTextColor(hdc, RGB(188, 196, 204));
            DrawWrappedText(hdc, pathRect, expression.imagePath.empty() ? L"(未設定)" : expression.imagePath, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            drawButton(editRect, L"名前", RGB(86, 96, 112));
            drawButton(browseRect, L"画像", RGB(70, 118, 178));
            drawButton(clearRect, L"解除", RGB(92, 80, 88));
            drawButton(removeRect, L"×", RGB(120, 70, 78));
            characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"edit_expression_name", selectedCharacterDefinitionIndex_, expressionIndex, editRect });
            characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"browse_expression", selectedCharacterDefinitionIndex_, expressionIndex, browseRect });
            characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"clear_expression", selectedCharacterDefinitionIndex_, expressionIndex, clearRect });
            characterManagerActionTargets_.push_back(CharacterManagerActionTarget{ L"remove_expression", selectedCharacterDefinitionIndex_, expressionIndex, removeRect });
            cursorY += 36;
        }
    }
    else
    {
        RECT infoRect = { detailRect.left + 20, detailRect.top + 20, detailRect.right - 20, detailRect.bottom - 20 };
        SetTextColor(hdc, RGB(188, 196, 204));
        DrawWrappedText(hdc, infoRect, L"左の一覧からキャラクターを選ぶか、新規追加してください。", DT_LEFT | DT_WORDBREAK);
    }

    if (characterFieldDialogVisible_)
    {
        characterDialogFieldDialogRect_ = { characterDialogRect_.left + 220, characterDialogRect_.top + 160, characterDialogRect_.right - 220, characterDialogRect_.top + 300 };
        HBRUSH modalBrush = CreateSolidBrush(RGB(48, 54, 62));
        FillRect(hdc, &characterDialogFieldDialogRect_, modalBrush);
        DeleteObject(modalBrush);
        HBRUSH modalBorder = CreateSolidBrush(RGB(96, 106, 118));
        FrameRect(hdc, &characterDialogFieldDialogRect_, modalBorder);
        DeleteObject(modalBorder);
        RECT modalTitleRect = { characterDialogFieldDialogRect_.left + 18, characterDialogFieldDialogRect_.top + 16, characterDialogFieldDialogRect_.right - 18, characterDialogFieldDialogRect_.top + 42 };
        SetTextColor(hdc, RGB(244, 248, 252));
        DrawWrappedText(hdc, modalTitleRect, characterFieldDialogTitle_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        characterDialogFieldEditRect_ = { characterDialogFieldDialogRect_.left + 18, characterDialogFieldDialogRect_.top + 54, characterDialogFieldDialogRect_.right - 18, characterDialogFieldDialogRect_.top + 84 };
        characterDialogFieldOkRect_ = { characterDialogFieldDialogRect_.right - 196, characterDialogFieldDialogRect_.bottom - 44, characterDialogFieldDialogRect_.right - 110, characterDialogFieldDialogRect_.bottom - 14 };
        characterDialogFieldCancelRect_ = { characterDialogFieldDialogRect_.right - 100, characterDialogFieldDialogRect_.bottom - 44, characterDialogFieldDialogRect_.right - 18, characterDialogFieldDialogRect_.bottom - 14 };
        drawButton(characterDialogFieldOkRect_, L"保存", RGB(70, 118, 178));
        drawButton(characterDialogFieldCancelRect_, L"閉じる", RGB(84, 90, 100));
    }
}

void NovelRuntime::DrawVariableManagerDialog(HDC hdc, const RECT& clientRect)
{
    variableDefinitionRects_.clear();
    variableManagerActionTargets_.clear();
    variableDialogRect_ = { clientRect.left + 40, clientRect.top + 36, clientRect.right - 40, clientRect.top + 620 };
    HBRUSH overlayBrush = CreateSolidBrush(RGB(24, 28, 34));
    FillRect(hdc, &clientRect, overlayBrush);
    DeleteObject(overlayBrush);

    HBRUSH dialogBrush = CreateSolidBrush(RGB(36, 42, 50));
    FillRect(hdc, &variableDialogRect_, dialogBrush);
    DeleteObject(dialogBrush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(74, 82, 92));
    FrameRect(hdc, &variableDialogRect_, borderBrush);
    DeleteObject(borderBrush);

    RECT headerRect = { variableDialogRect_.left, variableDialogRect_.top, variableDialogRect_.right, variableDialogRect_.top + 40 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(58, 56, 52));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);
    RECT titleRect = { headerRect.left + 10, headerRect.top, headerRect.right - 50, headerRect.bottom };
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, titleRect, L"変数管理", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    variableDialogCloseRect_ = { headerRect.right - 30, headerRect.top + 8, headerRect.right - 10, headerRect.top + 28 };
    DrawWrappedText(hdc, variableDialogCloseRect_, L"×", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    auto drawButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(246, 248, 250));
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };

    RECT listRect = { variableDialogRect_.left + 16, variableDialogRect_.top + 56, variableDialogRect_.left + 320, variableDialogRect_.bottom - 16 };
    RECT detailRect = { listRect.right + 12, listRect.top, variableDialogRect_.right - 16, listRect.bottom };
    HBRUSH panelBrush = CreateSolidBrush(RGB(28, 34, 42));
    FillRect(hdc, &listRect, panelBrush);
    FillRect(hdc, &detailRect, panelBrush);
    DeleteObject(panelBrush);
    FrameRect(hdc, &listRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
    FrameRect(hdc, &detailRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    variableDialogEditRect_ = { listRect.left + 8, listRect.top + 8, listRect.right - 96, listRect.top + 34 };
    variableDialogAddRect_ = { listRect.right - 82, listRect.top + 8, listRect.right - 8, listRect.top + 34 };
    variableDialogDeleteRect_ = { listRect.left + 8, listRect.bottom - 34, listRect.left + 120, listRect.bottom - 8 };
    drawButton(variableDialogAddRect_, L"追加", RGB(70, 118, 178));
    drawButton(variableDialogDeleteRect_, L"変数削除", RGB(120, 70, 78));

    int rowY = listRect.top + 46;
    for (size_t i = 0; i < variableDefinitions_.size(); ++i)
    {
        RECT rowRect = { listRect.left + 8, rowY, listRect.right - 8, rowY + 30 };
        variableDefinitionRects_.push_back(rowRect);
        HBRUSH rowBrush = CreateSolidBrush(i == selectedVariableDefinitionIndex_ ? RGB(58, 78, 104) : RGB(38, 46, 56));
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);
        FrameRect(hdc, &rowRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        RECT nameRect = { rowRect.left + 10, rowRect.top, rowRect.right - 80, rowRect.bottom };
        RECT typeRect = { rowRect.right - 76, rowRect.top, rowRect.right - 8, rowRect.bottom };
        SetTextColor(hdc, RGB(236, 240, 244));
        DrawWrappedText(hdc, nameRect, variableDefinitions_[i].name, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SetTextColor(hdc, RGB(176, 206, 232));
        DrawWrappedText(hdc, typeRect, GetVariableTypeLabel(variableDefinitions_[i].type), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        rowY += 34;
    }

    if (selectedVariableDefinitionIndex_ < variableDefinitions_.size())
    {
        const VariableDefinition& definition = variableDefinitions_[selectedVariableDefinitionIndex_];
        RECT infoRect = { detailRect.left + 18, detailRect.top + 18, detailRect.right - 18, detailRect.top + 44 };
        SetTextColor(hdc, RGB(244, 248, 252));
        DrawWrappedText(hdc, infoRect, definition.name, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        auto drawRow = [&](const std::wstring& label, const std::wstring& value, const std::wstring& action, int& cursorY, bool compact = false)
        {
            RECT labelRect = { detailRect.left + 18, cursorY, detailRect.left + 120, cursorY + 26 };
            RECT valueRect = { detailRect.left + 128, cursorY, detailRect.right - 110, cursorY + 26 };
            RECT buttonRect = { detailRect.right - 96, cursorY, detailRect.right - 18, cursorY + 26 };
            SetTextColor(hdc, RGB(188, 196, 204));
            DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SetTextColor(hdc, RGB(236, 240, 244));
            DrawWrappedText(hdc, valueRect, value.empty() ? L"(未設定)" : value, compact ? (DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) : (DT_LEFT | DT_WORDBREAK));
            drawButton(buttonRect, action == L"cycle_type" ? L"切替" : L"編集", RGB(86, 96, 112));
            variableManagerActionTargets_.push_back(VariableManagerActionTarget{ action, selectedVariableDefinitionIndex_, buttonRect });
            cursorY += compact ? 34 : 56;
        };

        int cursorY = detailRect.top + 60;
        drawRow(L"変数名", definition.name, L"edit_name", cursorY, true);
        drawRow(L"型", GetVariableTypeLabel(definition.type), L"cycle_type", cursorY, true);
        drawRow(L"初期値", definition.initialValue, L"edit_initial", cursorY, true);
        const auto currentIt = variables_.find(definition.name);
        drawRow(L"現在値", currentIt == variables_.end() ? definition.initialValue : currentIt->second, L"edit_current", cursorY, true);
        drawRow(L"説明", definition.description, L"edit_description", cursorY, false);

        RECT usageRect = { detailRect.left + 18, cursorY + 4, detailRect.right - 18, cursorY + 32 };
        SetTextColor(hdc, RGB(196, 204, 212));
        DrawWrappedText(hdc, usageRect, L"使用箇所: " + std::to_wstring(GetVariableUsageCount(definition.name)) + L" 件", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    else
    {
        RECT infoRect = { detailRect.left + 20, detailRect.top + 20, detailRect.right - 20, detailRect.bottom - 20 };
        SetTextColor(hdc, RGB(188, 196, 204));
        DrawWrappedText(hdc, infoRect, L"左の一覧から変数を選ぶか、新規追加してください。", DT_LEFT | DT_WORDBREAK);
    }

    if (variableFieldDialogVisible_)
    {
        variableFieldDialogRect_ = { variableDialogRect_.left + 220, variableDialogRect_.top + 170, variableDialogRect_.right - 220, variableDialogRect_.top + 310 };
        HBRUSH modalBrush = CreateSolidBrush(RGB(48, 54, 62));
        FillRect(hdc, &variableFieldDialogRect_, modalBrush);
        DeleteObject(modalBrush);
        HBRUSH modalBorder = CreateSolidBrush(RGB(96, 106, 118));
        FrameRect(hdc, &variableFieldDialogRect_, modalBorder);
        DeleteObject(modalBorder);
        RECT modalTitleRect = { variableFieldDialogRect_.left + 18, variableFieldDialogRect_.top + 16, variableFieldDialogRect_.right - 18, variableFieldDialogRect_.top + 42 };
        SetTextColor(hdc, RGB(244, 248, 252));
        DrawWrappedText(hdc, modalTitleRect, variableFieldDialogTitle_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        variableFieldEditRect_ = { variableFieldDialogRect_.left + 18, variableFieldDialogRect_.top + 54, variableFieldDialogRect_.right - 18, variableFieldDialogRect_.top + 84 };
        variableFieldOkRect_ = { variableFieldDialogRect_.right - 196, variableFieldDialogRect_.bottom - 44, variableFieldDialogRect_.right - 110, variableFieldDialogRect_.bottom - 14 };
        variableFieldCancelRect_ = { variableFieldDialogRect_.right - 100, variableFieldDialogRect_.bottom - 44, variableFieldDialogRect_.right - 18, variableFieldDialogRect_.bottom - 14 };
        drawButton(variableFieldOkRect_, L"保存", RGB(70, 118, 178));
        drawButton(variableFieldCancelRect_, L"閉じる", RGB(84, 90, 100));
    }
}

void NovelRuntime::DrawSettingsDialog(HDC hdc, const RECT& clientRect)
{
    settingsActionTargets_.clear();
    settingsCategoryRects_.clear();
    settingsDialogRect_ = { clientRect.left + 80, clientRect.top + 48, clientRect.right - 80, clientRect.bottom - 36 };
    HBRUSH overlayBrush = CreateSolidBrush(RGB(24, 28, 34));
    FillRect(hdc, &clientRect, overlayBrush);
    DeleteObject(overlayBrush);

    HBRUSH dialogBrush = CreateSolidBrush(RGB(36, 42, 50));
    FillRect(hdc, &settingsDialogRect_, dialogBrush);
    DeleteObject(dialogBrush);
    HBRUSH borderBrush = CreateSolidBrush(RGB(74, 82, 92));
    FrameRect(hdc, &settingsDialogRect_, borderBrush);
    DeleteObject(borderBrush);

    RECT headerRect = { settingsDialogRect_.left, settingsDialogRect_.top, settingsDialogRect_.right, settingsDialogRect_.top + 40 };
    HBRUSH headerBrush = CreateSolidBrush(RGB(58, 56, 52));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);
    RECT titleRect = { headerRect.left + 12, headerRect.top, headerRect.right - 52, headerRect.bottom };
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawWrappedText(hdc, titleRect, L"ゲームセッティング", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    settingsDialogCloseRect_ = { headerRect.right - 30, headerRect.top + 8, headerRect.right - 10, headerRect.top + 28 };
    DrawWrappedText(hdc, settingsDialogCloseRect_, L"×", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    settingsNavRect_ = { settingsDialogRect_.left + 20, headerRect.bottom + 12, settingsDialogRect_.left + 168, settingsDialogRect_.bottom - 14 };
    settingsContentRect_ = { settingsNavRect_.right + 12, headerRect.bottom + 12, settingsDialogRect_.right - 20, settingsDialogRect_.bottom - 14 };

    HBRUSH navBrush = CreateSolidBrush(RGB(34, 38, 44));
    FillRect(hdc, &settingsNavRect_, navBrush);
    DeleteObject(navBrush);
    HBRUSH navBorderBrush = CreateSolidBrush(RGB(82, 88, 96));
    FrameRect(hdc, &settingsNavRect_, navBorderBrush);
    DeleteObject(navBorderBrush);

    HBRUSH contentBrush = CreateSolidBrush(RGB(40, 44, 50));
    FillRect(hdc, &settingsContentRect_, contentBrush);
    DeleteObject(contentBrush);
    HBRUSH contentBorderBrush = CreateSolidBrush(RGB(82, 88, 96));
    FrameRect(hdc, &settingsContentRect_, contentBorderBrush);
    DeleteObject(contentBorderBrush);

    auto drawButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill, const std::wstring& action)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        HBRUSH frameBrush = CreateSolidBrush(RGB(98, 106, 118));
        FrameRect(hdc, &rect, frameBrush);
        DeleteObject(frameBrush);
        SetTextColor(hdc, RGB(240, 244, 248));
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (!action.empty())
        {
            settingsActionTargets_.push_back(SettingsActionTarget{ action, rect });
        }
    };

    const std::vector<std::wstring> settingsCategories =
    {
        L"ゲーム全体", L"画面", L"フォントスタイル", L"キャラクター", L"メッセージウィンドウ",
        L"メニュー", L"カーソル", L"キーボード・マウス", L"既読管理", L"オーディオ",
        L"バックログ", L"UIテーマ一括変換", L"CGモード", L"回想モード", L"Kaktos Engine"
    };
    const int categoryRowHeight = 28;
    for (size_t i = 0; i < settingsCategories.size(); ++i)
    {
        RECT rowRect = { settingsNavRect_.left + 8, settingsNavRect_.top + 8 + static_cast<int>(i) * categoryRowHeight, settingsNavRect_.right - 8, settingsNavRect_.top + 8 + static_cast<int>(i + 1) * categoryRowHeight - 2 };
        settingsCategoryRects_.push_back(rowRect);
        if (i == selectedSettingsCategoryIndex_)
        {
            HBRUSH activeBrush = CreateSolidBrush(RGB(52, 122, 188));
            FillRect(hdc, &rowRect, activeBrush);
            DeleteObject(activeBrush);
        }
        SetTextColor(hdc, i == selectedSettingsCategoryIndex_ ? RGB(255, 255, 255) : RGB(214, 218, 224));
        DrawWrappedText(hdc, rowRect, settingsCategories[i], DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        settingsActionTargets_.push_back(SettingsActionTarget{ L"settings_tab:" + std::to_wstring(i), rowRect });
    }

    const int scrollBarWidth = 12;
    const RECT contentInnerRect = { settingsContentRect_.left + 12, settingsContentRect_.top + 12, settingsContentRect_.right - 20, settingsContentRect_.bottom - 12 };
    const RECT scrollTrackRect = { settingsContentRect_.right - scrollBarWidth - 4, settingsContentRect_.top + 8, settingsContentRect_.right - 4, settingsContentRect_.bottom - 8 };
    const int contentStartY = contentInnerRect.top - settingsScrollOffset_;
    int cursorY = contentStartY;
    auto drawRow = [&](const std::wstring& label, const std::wstring& value, const std::wstring& mainAction, bool hasPlusMinus)
    {
        RECT labelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 160, cursorY + 26 };
        RECT valueRect = { contentInnerRect.left + 170, cursorY, contentInnerRect.right - 170, cursorY + 26 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, RGB(238, 242, 246));
        DrawWrappedText(hdc, valueRect, value, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (hasPlusMinus)
        {
            RECT minusRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right - 102, cursorY + 24 };
            RECT plusRect = { contentInnerRect.right - 94, cursorY - 2, contentInnerRect.right - 56, cursorY + 24 };
            drawButton(minusRect, L"-", RGB(78, 92, 108), mainAction + L"_minus");
            drawButton(plusRect, L"+", RGB(78, 92, 108), mainAction + L"_plus");
        }
        else
        {
            RECT buttonRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right, cursorY + 24 };
            drawButton(buttonRect, L"変更", RGB(70, 118, 178), mainAction);
        }
        cursorY += 38;
    };
    auto drawToggleRow = [&](const std::wstring& label, const std::wstring& value, const std::wstring& action, const std::wstring& buttonLabel)
    {
        RECT labelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 160, cursorY + 26 };
        RECT valueRect = { contentInnerRect.left + 170, cursorY, contentInnerRect.right - 170, cursorY + 26 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, RGB(238, 242, 246));
        DrawWrappedText(hdc, valueRect, value, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT buttonRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right, cursorY + 24 };
        drawButton(buttonRect, buttonLabel, RGB(70, 118, 178), action);
        cursorY += 38;
    };
    auto drawPathRow = [&](const std::wstring& label, const std::wstring& value, const std::wstring& browseAction, const std::wstring& clearAction)
    {
        RECT labelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 160, cursorY + 26 };
        RECT valueRect = { contentInnerRect.left + 170, cursorY, contentInnerRect.right - 250, cursorY + 40 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, RGB(176, 184, 194));
        DrawWrappedText(hdc, valueRect, value.empty() ? L"(画像未設定)" : value, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        RECT browseRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right - 70, cursorY + 24 };
        RECT clearRect = { contentInnerRect.right - 64, cursorY - 2, contentInnerRect.right, cursorY + 24 };
        drawButton(browseRect, L"参照", RGB(70, 118, 178), browseAction);
        drawButton(clearRect, L"解除", RGB(92, 80, 88), clearAction);
        cursorY += 48;
    };
    auto drawColorRow = [&](const std::wstring& label, COLORREF color, const std::wstring& action)
    {
        RECT labelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 160, cursorY + 26 };
        RECT valueRect = { contentInnerRect.left + 170, cursorY, contentInnerRect.right - 170, cursorY + 26 };
        RECT swatchRect = { valueRect.left, cursorY + 3, valueRect.left + 26, cursorY + 23 };
        RECT textRect = { swatchRect.right + 8, cursorY, valueRect.right, cursorY + 26 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, labelRect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH swatchBrush = CreateSolidBrush(color);
        FillRect(hdc, &swatchRect, swatchBrush);
        DeleteObject(swatchBrush);
        HBRUSH swatchBorderBrush = CreateSolidBrush(RGB(98, 106, 118));
        FrameRect(hdc, &swatchRect, swatchBorderBrush);
        DeleteObject(swatchBorderBrush);
        SetTextColor(hdc, RGB(238, 242, 246));
        DrawWrappedText(hdc, textRect, FormatHexColor(color), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT buttonRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right, cursorY + 24 };
        drawButton(buttonRect, L"色選択", RGB(70, 118, 178), action);
        cursorY += 38;
    };
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, settingsContentRect_.left + 1, settingsContentRect_.top + 1, settingsContentRect_.right - 1, settingsContentRect_.bottom - 1);

    auto drawSectionTitle = [&](const std::wstring& label)
    {
        RECT rect = { contentInnerRect.left, cursorY, contentInnerRect.right, cursorY + 28 };
        SetTextColor(hdc, RGB(196, 222, 248));
        DrawWrappedText(hdc, rect, label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        cursorY += 34;
    };

    auto drawPlaceholder = [&](const std::wstring& title, const std::vector<std::wstring>& lines)
    {
        drawSectionTitle(title);
        for (const std::wstring& line : lines)
        {
            RECT rect = { contentInnerRect.left, cursorY, contentInnerRect.right, cursorY + 24 };
            SetTextColor(hdc, RGB(186, 192, 198));
            DrawWrappedText(hdc, rect, line, DT_LEFT | DT_WORDBREAK);
            cursorY += 28;
        }
    };

    if (selectedSettingsCategoryIndex_ == 0)
    {
        drawRow(L"ゲームタイトル", storyTitle_, L"", false);
        drawRow(L"保存先", editorSettings_.saveDirectory.empty() ? GetAssetsRootDirectory() : editorSettings_.saveDirectory, L"browse_save_dir", false);

        RECT autosaveLabelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 160, cursorY + 26 };
        RECT autosaveValueRect = { contentInnerRect.left + 170, cursorY, contentInnerRect.right - 170, cursorY + 26 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, autosaveLabelRect, L"オートセーブ", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(hdc, RGB(238, 242, 246));
        DrawWrappedText(hdc, autosaveValueRect, editorSettings_.autosaveEnabled ? L"有効" : L"無効", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT toggleRect = { contentInnerRect.right - 140, cursorY - 2, contentInnerRect.right, cursorY + 24 };
        drawButton(toggleRect, editorSettings_.autosaveEnabled ? L"無効にする" : L"有効にする", RGB(92, 112, 72), L"toggle_autosave");
        cursorY += 48;

        drawSectionTitle(L"プレイヤーUIボタン");
        for (size_t i = 0; i < uiButtons_.size(); ++i)
        {
            const UiButtonDefinition& button = uiButtons_[i];
            RECT labelRect = { contentInnerRect.left, cursorY, contentInnerRect.left + 150, cursorY + 24 };
            RECT stateRect = { contentInnerRect.left + 156, cursorY, contentInnerRect.left + 260, cursorY + 24 };
            RECT posRect = { contentInnerRect.left + 266, cursorY, contentInnerRect.right - 340, cursorY + 24 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, labelRect, button.label.empty() ? button.id : button.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            DrawWrappedText(hdc, stateRect, button.visible ? L"表示" : L"非表示", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            DrawWrappedText(hdc, posRect, L"X: " + std::to_wstring(button.x) + L" / Y: " + std::to_wstring(button.y), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            RECT toggleButtonRect = { contentInnerRect.right - 334, cursorY - 2, contentInnerRect.right - 256, cursorY + 24 };
            RECT browseButtonRect = { contentInnerRect.right - 248, cursorY - 2, contentInnerRect.right - 170, cursorY + 24 };
            RECT clearButtonRect = { contentInnerRect.right - 162, cursorY - 2, contentInnerRect.right - 108, cursorY + 24 };
            RECT leftButtonRect = { contentInnerRect.right - 104, cursorY - 2, contentInnerRect.right - 78, cursorY + 24 };
            RECT rightButtonRect = { contentInnerRect.right - 74, cursorY - 2, contentInnerRect.right - 48, cursorY + 24 };
            RECT upButtonRect = { contentInnerRect.right - 44, cursorY - 2, contentInnerRect.right - 18, cursorY + 24 };
            RECT downButtonRect = { contentInnerRect.right - 14, cursorY - 2, contentInnerRect.right + 12, cursorY + 24 };
            drawButton(toggleButtonRect, button.visible ? L"隠す" : L"表示", RGB(78, 92, 108), L"ui_toggle:" + std::to_wstring(i));
            drawButton(browseButtonRect, L"画像参照", RGB(70, 118, 178), L"ui_browse_icon:" + std::to_wstring(i));
            drawButton(clearButtonRect, L"解除", RGB(92, 80, 88), L"ui_clear_icon:" + std::to_wstring(i));
            drawButton(leftButtonRect, L"←", RGB(70, 118, 178), L"ui_left:" + std::to_wstring(i));
            drawButton(rightButtonRect, L"→", RGB(70, 118, 178), L"ui_right:" + std::to_wstring(i));
            drawButton(upButtonRect, L"↑", RGB(92, 112, 72), L"ui_up:" + std::to_wstring(i));
            drawButton(downButtonRect, L"↓", RGB(92, 112, 72), L"ui_down:" + std::to_wstring(i));
            cursorY += 28;

            RECT iconRect = { contentInnerRect.left + 28, cursorY, contentInnerRect.left + 68, cursorY + 40 };
            HBRUSH iconBackBrush = CreateSolidBrush(RGB(26, 32, 38));
            FillRect(hdc, &iconRect, iconBackBrush);
            DeleteObject(iconBackBrush);
            HBRUSH iconBorderBrush = CreateSolidBrush(RGB(74, 82, 92));
            FrameRect(hdc, &iconRect, iconBorderBrush);
            DeleteObject(iconBorderBrush);
            if (button.iconImage)
            {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(button.iconImage.get(), Gdiplus::Rect(iconRect.left + 4, iconRect.top + 4, (iconRect.right - iconRect.left) - 8, (iconRect.bottom - iconRect.top) - 8));
            }
            else
            {
                RECT noIconRect = { iconRect.left + 4, iconRect.top + 4, iconRect.right - 4, iconRect.bottom - 4 };
                SetTextColor(hdc, RGB(136, 146, 156));
                DrawWrappedText(hdc, noIconRect, L"なし", DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            }

            RECT pathRect = { contentInnerRect.left + 78, cursorY + 4, contentInnerRect.right, cursorY + 36 };
            SetTextColor(hdc, RGB(176, 184, 194));
            DrawWrappedText(hdc, pathRect, button.iconPath.empty() ? L"(画像未設定)" : button.iconPath, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
            cursorY += 50;
        }

        settingsDialogRestoreRect_ = { contentInnerRect.left, cursorY, contentInnerRect.left + 220, cursorY + 30 };
        drawButton(settingsDialogRestoreRect_, L"オートセーブから復元", RGB(84, 104, 132), L"");
        cursorY += 40;
    }
    else if (selectedSettingsCategoryIndex_ == 1)
    {
        drawSectionTitle(L"画面");
        drawRow(L"画面サイズ", std::to_wstring(editorSettings_.windowWidth) + L" x " + std::to_wstring(editorSettings_.windowHeight), L"cycle_window", false);
        drawPlaceholder(L"今後追加", { L"ビューウィンドウ比率、起動時フルスクリーン、既定拡大率をここへ追加予定です。" });
    }
    else if (selectedSettingsCategoryIndex_ == 2)
    {
        drawSectionTitle(L"フォントスタイル");
        drawRow(L"既定メッセージフォント", editorSettings_.defaultFont, L"cycle_font", false);
        {
            RECT sampleFrame = { contentInnerRect.left + 170, cursorY - 6, contentInnerRect.right, cursorY + 44 };
            HBRUSH sampleBrush = CreateSolidBrush(RGB(28, 34, 42));
            FillRect(hdc, &sampleFrame, sampleBrush);
            DeleteObject(sampleBrush);
            HBRUSH sampleBorder = CreateSolidBrush(RGB(74, 82, 92));
            FrameRect(hdc, &sampleFrame, sampleBorder);
            DeleteObject(sampleBorder);

            const wchar_t* sampleFontFace = editorSettings_.defaultFont.empty() ? L"Yu Gothic UI" : editorSettings_.defaultFont.c_str();
            HFONT sampleFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, sampleFontFace);
            HFONT oldSampleFont = static_cast<HFONT>(SelectObject(hdc, sampleFont));
            RECT sampleTextRect = { sampleFrame.left + 14, sampleFrame.top + 10, sampleFrame.right - 14, sampleFrame.bottom - 10 };
            SetTextColor(hdc, RGB(244, 246, 250));
            DrawWrappedText(hdc, sampleTextRect, L"見本 Aa あア 亜 Sample 0123", DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(hdc, oldSampleFont);
            DeleteObject(sampleFont);
            cursorY += 56;
        }
        drawRow(L"既定テキスト速度", std::to_wstring(editorSettings_.defaultTextSpeed) + L" ms", L"text_speed", true);
        drawPlaceholder(L"今後追加", { L"名前欄フォント、UI用フォント、文字サイズセットをここへ追加予定です。" });
    }
    else if (selectedSettingsCategoryIndex_ == 4)
    {
        drawSectionTitle(L"メッセージウィンドウ");
        drawToggleRow(L"枠表示", editorSettings_.defaultMessageWindowVisible ? L"表示" : L"非表示", L"msg_toggle", editorSettings_.defaultMessageWindowVisible ? L"隠す" : L"表示");
        drawColorRow(L"背景色", editorSettings_.defaultMessageWindowColor, L"msg_color");
        drawColorRow(L"枠線色", editorSettings_.defaultMessageWindowBorderColor, L"msg_border");
        drawRow(L"枠透明度", std::to_wstring((editorSettings_.defaultMessageWindowOpacity * 100) / 255) + L"%", L"msg_opacity", true);
        drawRow(L"本文余白", std::to_wstring(editorSettings_.defaultMessageWindowPadding) + L" px", L"msg_padding", true);
        drawPathRow(L"枠画像", editorSettings_.defaultMessageWindowImage, L"msg_browse_image", L"msg_clear_image");
        drawSectionTitle(L"名前欄");
        drawToggleRow(L"名前欄表示", editorSettings_.defaultNameWindowVisible ? L"表示" : L"非表示", L"name_toggle", editorSettings_.defaultNameWindowVisible ? L"隠す" : L"表示");
        drawColorRow(L"名前欄色", editorSettings_.defaultNameWindowColor, L"name_color_setting");
        drawColorRow(L"名前欄枠線", editorSettings_.defaultNameWindowBorderColor, L"name_border_setting");
        drawRow(L"横位置", std::to_wstring(editorSettings_.defaultNameWindowOffsetX) + L" px", L"name_x", true);
        drawRow(L"縦位置", std::to_wstring(editorSettings_.defaultNameWindowOffsetY) + L" px", L"name_y", true);
        drawRow(L"幅", std::to_wstring(editorSettings_.defaultNameWindowWidth) + L" px", L"name_width", true);
        drawRow(L"高さ", std::to_wstring(editorSettings_.defaultNameWindowHeight) + L" px", L"name_height", true);
        drawRow(L"透明度", std::to_wstring((editorSettings_.defaultNameWindowOpacity * 100) / 255) + L"%", L"name_opacity", true);
        drawPathRow(L"名前欄画像", editorSettings_.defaultNameWindowImage, L"name_browse_image", L"name_clear_image");
        drawPlaceholder(L"補足", { L"プリセット色から即時切替できます。", L"シナリオ内で個別指定した場合は、そのコマンド指定が優先されます。" });
    }
    else if (selectedSettingsCategoryIndex_ == 5)
    {
        drawSectionTitle(L"タイトルメニュー");
        drawToggleRow(L"はじめる", editorSettings_.titleMenuStartEnabled ? L"表示" : L"非表示", L"title_start_toggle", editorSettings_.titleMenuStartEnabled ? L"隠す" : L"表示");
        drawToggleRow(L"ロード", editorSettings_.titleMenuLoadEnabled ? L"表示" : L"非表示", L"title_load_toggle", editorSettings_.titleMenuLoadEnabled ? L"隠す" : L"表示");
        drawToggleRow(L"オプション", editorSettings_.titleMenuOptionsEnabled ? L"表示" : L"非表示", L"title_options_toggle", editorSettings_.titleMenuOptionsEnabled ? L"隠す" : L"表示");
        drawToggleRow(L"終了", editorSettings_.titleMenuExitEnabled ? L"表示" : L"非表示", L"title_exit_toggle", editorSettings_.titleMenuExitEnabled ? L"隠す" : L"表示");
        drawPlaceholder(L"補足", { L"新規プロジェクト作成時の title.ks に反映されます。", L"既存の title.ks はシナリオ側で直接編集してください。" });
    }
    else if (selectedSettingsCategoryIndex_ == 9)
    {
        drawSectionTitle(L"オーディオ");
        drawRow(L"マスター音量", std::to_wstring(editorSettings_.masterVolume) + L"%", L"master", true);
        drawRow(L"BGM音量", std::to_wstring(editorSettings_.bgmVolume) + L"%", L"bgm", true);
        drawRow(L"SE音量", std::to_wstring(editorSettings_.seVolume) + L"%", L"se", true);
        drawRow(L"ボイス音量", std::to_wstring(editorSettings_.voiceVolume) + L"%", L"voice", true);
        drawPlaceholder(L"今後追加", { L"再生デバイス切替、試聴、カテゴリ別ミュートをここへ追加予定です。" });
    }
    else
    {
        drawPlaceholder(settingsCategories[selectedSettingsCategoryIndex_], { L"このカテゴリはまだ土台段階です。", L"左のカテゴリ切替と右のスクロール構造は先に有効化しています。" });
    }

    RestoreDC(hdc, savedDc);

    const int contentHeight = cursorY - contentStartY + 8;
    const int visibleHeight = contentInnerRect.bottom - contentInnerRect.top;
    settingsScrollMax_ = (std::max)(0, contentHeight - visibleHeight);
    settingsScrollOffset_ = (std::max)(0, (std::min)(settingsScrollOffset_, settingsScrollMax_));

    HBRUSH trackBrush = CreateSolidBrush(RGB(32, 36, 42));
    FillRect(hdc, &scrollTrackRect, trackBrush);
    DeleteObject(trackBrush);
    HBRUSH trackBorderBrush = CreateSolidBrush(RGB(82, 88, 96));
    FrameRect(hdc, &scrollTrackRect, trackBorderBrush);
    DeleteObject(trackBorderBrush);
    if (settingsScrollMax_ > 0)
    {
        const int trackHeight = scrollTrackRect.bottom - scrollTrackRect.top - 4;
        const int thumbHeight = (std::max)(28, (visibleHeight * trackHeight) / (contentHeight <= 0 ? 1 : contentHeight));
        const int travel = (std::max)(1, trackHeight - thumbHeight);
        const int thumbTop = scrollTrackRect.top + 2 + (settingsScrollOffset_ * travel) / settingsScrollMax_;
        RECT thumbRect = { scrollTrackRect.left + 2, thumbTop, scrollTrackRect.right - 2, thumbTop + thumbHeight };
        HBRUSH thumbBrush = CreateSolidBrush(RGB(118, 126, 136));
        FillRect(hdc, &thumbRect, thumbBrush);
        DeleteObject(thumbBrush);
    }
}

COLORREF NovelRuntime::GetCommandAccentColor(const ScriptCommand& command) const
{
    switch (command.type)
    {
    case ScriptCommand::Type::Text: return RGB(239, 134, 143);
    case ScriptCommand::Type::MessageWindow: return RGB(153, 120, 236);
    case ScriptCommand::Type::TextSpeed: return RGB(153, 120, 236);
    case ScriptCommand::Type::MessageFont: return RGB(153, 120, 236);
    case ScriptCommand::Type::MessageFontReset: return RGB(153, 120, 236);
    case ScriptCommand::Type::MessageStyle: return RGB(153, 120, 236);
    case ScriptCommand::Type::TextColor: return RGB(153, 120, 236);
    case ScriptCommand::Type::NameColor: return RGB(153, 120, 236);
    case ScriptCommand::Type::NameWindow: return RGB(153, 120, 236);
    case ScriptCommand::Type::VerticalText: return RGB(153, 120, 236);
    case ScriptCommand::Type::PageBreak: return RGB(153, 120, 236);
    case ScriptCommand::Type::Shake: return RGB(50, 147, 255);
    case ScriptCommand::Type::Fade: return RGB(50, 147, 255);
    case ScriptCommand::Type::Transition: return RGB(50, 147, 255);
    case ScriptCommand::Type::Zoom: return RGB(50, 147, 255);
    case ScriptCommand::Type::Pan: return RGB(50, 147, 255);
    case ScriptCommand::Type::Flash: return RGB(50, 147, 255);
    case ScriptCommand::Type::Tint: return RGB(50, 147, 255);
    case ScriptCommand::Type::Choice: return RGB(241, 120, 114);
    case ScriptCommand::Type::Jump: return RGB(241, 120, 114);
    case ScriptCommand::Type::Label: return RGB(241, 120, 114);
    case ScriptCommand::Type::Background: return RGB(120, 201, 63);
    case ScriptCommand::Type::Character: return RGB(241, 172, 59);
    case ScriptCommand::Type::HideCharacter: return RGB(205, 205, 205);
    case ScriptCommand::Type::Speaker: return RGB(170, 135, 232);
    case ScriptCommand::Type::ClearSpeaker: return RGB(170, 135, 232);
    case ScriptCommand::Type::SetValue: return RGB(255, 160, 77);
    case ScriptCommand::Type::AddValue: return RGB(255, 160, 77);
    case ScriptCommand::Type::IfJump: return RGB(80, 158, 238);
    case ScriptCommand::Type::Title: return RGB(120, 120, 120);
    }
    return RGB(150, 150, 150);
}

void NovelRuntime::DrawCommandPalette(HDC hdc, const RECT& panelRect)
{
    SetBkMode(hdc, TRANSPARENT);
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));

    paletteButtons_.clear();
    leftTabs_.clear();
    assetCategories_.clear();
    materialAddRect_ = {};
    materialPreviewRect_ = {};
    materialPreviewPlayRect_ = {};
    materialPreviewStopRect_ = {};
    materialPreviewVolumeDownRect_ = {};
    materialPreviewVolumeUpRect_ = {};
    if (paletteSections_.empty())
    {
        paletteSections_ =
        {
            { L"ストーリー", RGB(182, 88, 96), {}, {}, true },
            { L"演出", RGB(40, 118, 204), {}, {}, true },
            { L"キャラクター", RGB(201, 140, 48), {}, {}, true },
            { L"背景・画像", RGB(96, 168, 48), {}, {}, true },
            { L"メッセージ", RGB(153, 120, 236), {}, {}, true },
            { L"サウンド", RGB(118, 98, 194), {}, {}, true },
            { L"システム", RGB(182, 118, 54), {}, {}, true },
        };
    }
    sceneAddRect_ = {};
    sceneRenameRect_ = {};
    sceneDuplicateRect_ = {};
    sceneDeleteRect_ = {};
    for (SceneListItem& item : sceneItems_)
    {
        item.rect = {};
    }
    for (AssetListItem& item : assetItems_)
    {
        item.rect = {};
    }

    HBRUSH panelBrush = CreateSolidBrush(RGB(20, 24, 30));
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);

    SelectObject(hdc, bodyFont);
    const RECT tabsRect = { panelRect.left + 10, panelRect.top + 10, panelRect.right - 10, panelRect.top + 42 };
    const int tabWidth = (tabsRect.right - tabsRect.left) / 3;
    const struct
    {
        const wchar_t* id;
        const wchar_t* label;
        LeftPanelTab tab;
    } tabs[] =
    {
        { L"components", L"コンポーネント", LeftPanelTab::Components },
        { L"materials", L"素材", LeftPanelTab::Materials },
        { L"scenario", L"シナリオ", LeftPanelTab::Scenario },
    };
    for (int i = 0; i < 3; ++i)
    {
        RECT tabRect = { tabsRect.left + i * tabWidth, tabsRect.top, tabsRect.left + (i + 1) * tabWidth - 4, tabsRect.bottom };
        const bool active = leftPanelTab_ == tabs[i].tab;
        HBRUSH tabBrush = CreateSolidBrush(active ? RGB(236, 236, 232) : RGB(198, 198, 194));
        FillRect(hdc, &tabRect, tabBrush);
        DeleteObject(tabBrush);
        FrameRect(hdc, &tabRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetTextColor(hdc, RGB(54, 58, 64));
        DrawWrappedText(hdc, tabRect, tabs[i].label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        leftTabs_.push_back(LeftTabItem{ tabs[i].id, tabs[i].label, tabRect });
    }

    int cursorY = panelRect.top + 54 - (leftPanelTab_ == LeftPanelTab::Components ? componentScrollOffset_ : 0);
    const int sectionGap = 14;
    const int sectionPadding = 10;
    const int buttonHeight = 34;
    const int buttonGap = 8;

    if (leftPanelTab_ == LeftPanelTab::Materials)
    {
        const struct
        {
            const wchar_t* id;
            const wchar_t* label;
            const wchar_t* icon;
        } categories[] =
        {
            { L"background", L"背景", L"ui\\material_background.png" },
            { L"picture", L"写真", L"ui\\material_picture.png" },
            { L"character", L"キャラクター", L"ui\\material_character.png" },
            { L"se", L"SE", L"ui\\material_se.png" },
            { L"bgm", L"BGM", L"ui\\material_bgm.png" },
        };

        const int iconGap = 10;
        const int iconWidth = (panelRect.right - panelRect.left - 20 - (iconGap * 4)) / 5;
        int iconX = panelRect.left + 10;
        for (const auto& category : categories)
        {
            RECT iconRect = { iconX, cursorY, iconX + iconWidth, cursorY + 34 };
            assetCategories_.push_back(AssetCategoryItem{ category.id, category.label, category.icon, iconRect });
            const bool active = selectedAssetCategory_ == category.id;
            HBRUSH iconBrush = CreateSolidBrush(active ? RGB(255, 255, 255) : RGB(214, 214, 208));
            FillRect(hdc, &iconRect, iconBrush);
            DeleteObject(iconBrush);
            FrameRect(hdc, &iconRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            auto iconImage = TryLoadImage(ResolveMaterialIconPath(category.icon));
            if (iconImage)
            {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(iconImage.get(), Gdiplus::Rect(iconRect.left + 7, iconRect.top + 5, (iconRect.right - iconRect.left) - 14, (iconRect.bottom - iconRect.top) - 10));
            }
            else
            {
                SetTextColor(hdc, RGB(52, 56, 60));
                DrawWrappedText(hdc, iconRect, std::wstring(category.label).substr(0, 1), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            iconX += iconWidth + iconGap;
        }
        cursorY += 46;

        RECT titleRect = { panelRect.left + 12, cursorY, panelRect.right - 120, cursorY + 24 };
        materialAddRect_ = { panelRect.right - 108, cursorY - 2, panelRect.right - 12, cursorY + 24 };
        SetTextColor(hdc, RGB(228, 228, 224));
        DrawWrappedText(hdc, titleRect, [&]() -> std::wstring
        {
            for (const auto& category : categories)
            {
                if (selectedAssetCategory_ == category.id)
                {
                    return category.label;
                }
            }
            return L"背景";
        }(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH addBrush = CreateSolidBrush(RGB(232, 232, 226));
        FillRect(hdc, &materialAddRect_, addBrush);
        DeleteObject(addBrush);
        FrameRect(hdc, &materialAddRect_, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetTextColor(hdc, RGB(84, 88, 92));
        DrawWrappedText(hdc, materialAddRect_, L"ファイル追加", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursorY += 30;

        RECT searchRect = { panelRect.left + 12, cursorY, panelRect.right - 44, cursorY + 30 };
        RECT searchButtonRect = { panelRect.right - 40, cursorY, panelRect.right - 12, cursorY + 30 };
        eventSearchRect_ = searchRect;
        HBRUSH searchBrush = CreateSolidBrush(RGB(242, 242, 238));
        FillRect(hdc, &searchRect, searchBrush);
        DeleteObject(searchBrush);
        FrameRect(hdc, &searchRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        HBRUSH buttonBrush = CreateSolidBrush(RGB(242, 242, 238));
        FillRect(hdc, &searchButtonRect, buttonBrush);
        DeleteObject(buttonBrush);
        FrameRect(hdc, &searchButtonRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetTextColor(hdc, RGB(120, 124, 128));
        DrawWrappedText(hdc, searchRect, L"ファイル名検索", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawWrappedText(hdc, searchButtonRect, L"Q", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursorY += 40;

        const int previewHeight = 150;
        const int previewGap = 12;
        const int listBottom = panelRect.bottom - previewHeight - previewGap;

        SelectObject(hdc, bodyFont);
        std::wstring filter = materialFilterText_;
        if (!filter.empty())
        {
            CharLowerBuffW(&filter[0], static_cast<DWORD>(filter.size()));
        }
        for (AssetListItem& item : assetItems_)
        {
            if (item.category != selectedAssetCategory_)
            {
                item.rect = {};
                continue;
            }
            std::wstring label = item.label;
            if (!label.empty())
            {
                CharLowerBuffW(&label[0], static_cast<DWORD>(label.size()));
            }
            if (!filter.empty() && label.find(filter) == std::wstring::npos)
            {
                item.rect = {};
                continue;
            }

            if (cursorY + 24 > listBottom)
            {
                item.rect = {};
                continue;
            }

            item.rect = { panelRect.left + 12, cursorY, panelRect.right - 12, cursorY + 24 };
            if (!item.isDirectory && item.path == selectedAssetPath_)
            {
                HBRUSH selectedBrush = CreateSolidBrush(RGB(66, 72, 80));
                FillRect(hdc, &item.rect, selectedBrush);
                DeleteObject(selectedBrush);
            }
            RECT textRect = item.rect;
            textRect.left += 12 + item.depth * 18;
            if (item.isDirectory)
            {
                SetTextColor(hdc, RGB(184, 192, 200));
                DrawWrappedText(hdc, textRect, L"[DIR] " + item.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
            else
            {
                SetTextColor(hdc, RGB(214, 214, 210));
                DrawWrappedText(hdc, textRect, L"\u2514 " + item.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
            cursorY += 24;
        }

        materialPreviewRect_ = { panelRect.left + 12, panelRect.bottom - previewHeight, panelRect.right - 12, panelRect.bottom - 12 };
        HBRUSH previewBrush = CreateSolidBrush(RGB(214, 212, 206));
        FillRect(hdc, &materialPreviewRect_, previewBrush);
        DeleteObject(previewBrush);
        FrameRect(hdc, &materialPreviewRect_, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        if (!selectedAssetPath_.empty())
        {
            if (selectedAssetPreviewCategory_ == L"background" || selectedAssetPreviewCategory_ == L"picture" || selectedAssetPreviewCategory_ == L"character")
            {
                auto previewImage = TryLoadImage(selectedAssetPath_);
                if (previewImage)
                {
                    Gdiplus::Graphics graphics(hdc);
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    graphics.DrawImage(previewImage.get(), Gdiplus::Rect(materialPreviewRect_.left + 4, materialPreviewRect_.top + 4, (materialPreviewRect_.right - materialPreviewRect_.left) - 8, (materialPreviewRect_.bottom - materialPreviewRect_.top) - 8));
                }
                else
                {
                    RECT noImageRect = { materialPreviewRect_.left + 12, materialPreviewRect_.top + 12, materialPreviewRect_.right - 12, materialPreviewRect_.bottom - 12 };
                    SetTextColor(hdc, RGB(96, 96, 96));
                    DrawWrappedText(hdc, noImageRect, L"画像プレビューを読み込めません", DT_CENTER | DT_VCENTER | DT_WORDBREAK);
                }
            }
            else
            {
                RECT titleRect = { materialPreviewRect_.left + 14, materialPreviewRect_.top + 18, materialPreviewRect_.right - 14, materialPreviewRect_.top + 42 };
                RECT barRect = { materialPreviewRect_.left + 98, materialPreviewRect_.top + 70, materialPreviewRect_.right - 54, materialPreviewRect_.top + 76 };
                RECT playRect = { materialPreviewRect_.left + 16, materialPreviewRect_.top + 58, materialPreviewRect_.left + 52, materialPreviewRect_.top + 82 };
                RECT stopRect = { materialPreviewRect_.left + 56, materialPreviewRect_.top + 58, materialPreviewRect_.left + 92, materialPreviewRect_.top + 82 };
                RECT speakerRect = { materialPreviewRect_.right - 38, materialPreviewRect_.top + 58, materialPreviewRect_.right - 16, materialPreviewRect_.top + 82 };
                RECT volumeLabelRect = { materialPreviewRect_.left + 14, materialPreviewRect_.top + 92, materialPreviewRect_.left + 88, materialPreviewRect_.top + 116 };
                RECT volumeDownRect = { materialPreviewRect_.left + 98, materialPreviewRect_.top + 90, materialPreviewRect_.left + 126, materialPreviewRect_.top + 114 };
                RECT volumeValueRect = { materialPreviewRect_.left + 130, materialPreviewRect_.top + 90, materialPreviewRect_.left + 186, materialPreviewRect_.top + 114 };
                RECT volumeUpRect = { materialPreviewRect_.left + 190, materialPreviewRect_.top + 90, materialPreviewRect_.left + 218, materialPreviewRect_.top + 114 };
                materialPreviewPlayRect_ = playRect;
                materialPreviewStopRect_ = stopRect;
                materialPreviewVolumeDownRect_ = volumeDownRect;
                materialPreviewVolumeUpRect_ = volumeUpRect;
                SetTextColor(hdc, RGB(66, 70, 76));
                DrawWrappedText(hdc, titleRect, selectedAssetLabel_.empty() ? L"音声プレビュー" : selectedAssetLabel_, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                HBRUSH playBrush = CreateSolidBrush(RGB(244, 244, 240));
                FillRect(hdc, &playRect, playBrush);
                FillRect(hdc, &stopRect, playBrush);
                FillRect(hdc, &volumeDownRect, playBrush);
                FillRect(hdc, &volumeUpRect, playBrush);
                DeleteObject(playBrush);
                FrameRect(hdc, &playRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                FrameRect(hdc, &stopRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                FrameRect(hdc, &volumeDownRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                FrameRect(hdc, &volumeUpRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                DrawWrappedText(hdc, playRect, L"\u25b6", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                DrawWrappedText(hdc, stopRect, L"\u25a0", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                DrawWrappedText(hdc, volumeLabelRect, L"音量", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                DrawWrappedText(hdc, volumeDownRect, L"-", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                DrawWrappedText(hdc, volumeValueRect, std::to_wstring(assetPreviewVolume_) + L"%", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                DrawWrappedText(hdc, volumeUpRect, L"+", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                HBRUSH barBrush = CreateSolidBrush(RGB(108, 112, 118));
                FillRect(hdc, &barRect, barBrush);
                DeleteObject(barBrush);
                DrawWrappedText(hdc, speakerRect, L"VOL", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                RECT hintRect = { materialPreviewRect_.left + 14, materialPreviewRect_.bottom - 28, materialPreviewRect_.right - 14, materialPreviewRect_.bottom - 10 };
                SetTextColor(hdc, RGB(84, 88, 92));
                DrawWrappedText(hdc, hintRect, L"再生 / 停止 / 音量調整で素材プレビューを操作します", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }
    else if (leftPanelTab_ == LeftPanelTab::Scenario)
    {
        const int contentLeft = panelRect.left + 12;
        const int contentRight = panelRect.right - 12;
        const int headerWidth = 74;
        const int buttonGap = 8;
        const int availableWidth = contentRight - (contentLeft + headerWidth + 8);
        const int buttonWidth = (std::max)(56, (availableWidth - buttonGap) / 2);
        RECT titleRect = { contentLeft, cursorY, contentLeft + headerWidth, cursorY + 24 };
        RECT addButtonRect = { titleRect.right + 8, cursorY - 2, titleRect.right + 8 + buttonWidth, cursorY + 24 };
        RECT renameButtonRect = { addButtonRect.right + buttonGap, cursorY - 2, addButtonRect.right + buttonGap + buttonWidth, cursorY + 24 };
        RECT duplicateButtonRect = { addButtonRect.left, cursorY + 28, addButtonRect.left + buttonWidth, cursorY + 54 };
        RECT deleteButtonRect = { renameButtonRect.left, cursorY + 28, renameButtonRect.left + buttonWidth, cursorY + 54 };
        SetTextColor(hdc, RGB(228, 228, 224));
        DrawWrappedText(hdc, titleRect, L"シナリオ", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        const struct { RECT rect; const wchar_t* label; COLORREF fill; } sceneActions[] =
        {
            { addButtonRect, L"追加", RGB(232, 232, 226) },
            { renameButtonRect, L"名前変更", RGB(232, 232, 226) },
            { duplicateButtonRect, L"複製", RGB(232, 232, 226) },
            { deleteButtonRect, L"削除", RGB(232, 196, 196) },
        };
        for (const auto& action : sceneActions)
        {
            HBRUSH brush = CreateSolidBrush(action.fill);
            FillRect(hdc, &action.rect, brush);
            DeleteObject(brush);
            FrameRect(hdc, &action.rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            SetTextColor(hdc, RGB(84, 88, 92));
            DrawWrappedText(hdc, action.rect, action.label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        sceneAddRect_ = addButtonRect;
        sceneRenameRect_ = renameButtonRect;
        sceneDuplicateRect_ = duplicateButtonRect;
        sceneDeleteRect_ = deleteButtonRect;
        cursorY += 64;

        RECT searchRect = { panelRect.left + 12, cursorY, panelRect.right - 44, cursorY + 30 };
        RECT searchButtonRect = { panelRect.right - 40, cursorY, panelRect.right - 12, cursorY + 30 };
        eventSearchRect_ = searchRect;
        HBRUSH searchBrush = CreateSolidBrush(RGB(242, 242, 238));
        FillRect(hdc, &searchRect, searchBrush);
        DeleteObject(searchBrush);
        FrameRect(hdc, &searchRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        HBRUSH buttonBrush = CreateSolidBrush(RGB(242, 242, 238));
        FillRect(hdc, &searchButtonRect, buttonBrush);
        DeleteObject(buttonBrush);
        FrameRect(hdc, &searchButtonRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        SetTextColor(hdc, RGB(120, 124, 128));
        DrawWrappedText(hdc, searchRect, L"シナリオ名検索", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawWrappedText(hdc, searchButtonRect, L"Q", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursorY += 40;

        std::wstring filter = scenarioFilterText_;
        if (!filter.empty())
        {
            CharLowerBuffW(&filter[0], static_cast<DWORD>(filter.size()));
        }
        bool hasVisibleScene = false;
        for (SceneListItem& item : sceneItems_)
        {
            std::wstring label = item.label;
            if (!label.empty())
            {
                CharLowerBuffW(&label[0], static_cast<DWORD>(label.size()));
            }
            if (!filter.empty() && label.find(filter) == std::wstring::npos)
            {
                item.rect = {};
                continue;
            }
            hasVisibleScene = true;
            item.rect = { panelRect.left + 12, cursorY, panelRect.right - 12, cursorY + 24 };
            const bool active = item.path == scenarioPath_;
            if (item.path == selectedScenePath_)
            {
                HBRUSH selectedBrush = CreateSolidBrush(RGB(44, 52, 64));
                FillRect(hdc, &item.rect, selectedBrush);
                DeleteObject(selectedBrush);
            }
            SetTextColor(hdc, active ? RGB(255, 226, 158) : RGB(214, 214, 210));
            DrawWrappedText(hdc, item.rect, L"\u25b8 " + item.label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            cursorY += 24;
        }
        if (!hasVisibleScene)
        {
            RECT emptyRect = { panelRect.left + 12, cursorY + 8, panelRect.right - 12, cursorY + 48 };
            SetTextColor(hdc, RGB(150, 154, 158));
            DrawWrappedText(hdc, emptyRect, sceneItems_.empty() ? L"シナリオがまだありません" : L"条件に合うシナリオがありません", DT_LEFT | DT_WORDBREAK);
        }
    }
    else
    {
        sceneAddRect_ = {};
        sceneRenameRect_ = {};
        sceneDuplicateRect_ = {};
        sceneDeleteRect_ = {};

        const struct PaletteDef
        {
            const wchar_t* title;
            COLORREF color;
            std::initializer_list<std::pair<const wchar_t*, ScriptCommand::Type>> items;
        } sections[] =
        {
            { L"ストーリー", RGB(239, 134, 143), { { L"テキスト", ScriptCommand::Type::Text }, { L"分岐ボタン", ScriptCommand::Type::Choice }, { L"ジャンプ", ScriptCommand::Type::Jump }, { L"ラベル", ScriptCommand::Type::Label } } },
            { L"演出", RGB(50, 147, 255), { { L"ウェイト", ScriptCommand::Type::Wait }, { L"フェード", ScriptCommand::Type::Fade }, { L"トランジション", ScriptCommand::Type::Transition }, { L"ズーム", ScriptCommand::Type::Zoom }, { L"パン", ScriptCommand::Type::Pan }, { L"画面揺れ", ScriptCommand::Type::Shake }, { L"フラッシュ", ScriptCommand::Type::Flash }, { L"画面色調", ScriptCommand::Type::Tint }, { L"テキスト消去", ScriptCommand::Type::ClearText } } },
            { L"キャラクター", RGB(241, 172, 59), { { L"登場", ScriptCommand::Type::Character }, { L"退場", ScriptCommand::Type::HideCharacter }, { L"話者設定", ScriptCommand::Type::Speaker }, { L"話者クリア", ScriptCommand::Type::ClearSpeaker } } },
            { L"背景・画像", RGB(120, 201, 63), { { L"背景変更", ScriptCommand::Type::Background }, { L"タイトル", ScriptCommand::Type::Title } } },
            { L"メッセージ", RGB(153, 120, 236), { { L"メッセージ枠表示", ScriptCommand::Type::MessageWindow }, { L"テキスト消去", ScriptCommand::Type::ClearText }, { L"改ページ", ScriptCommand::Type::PageBreak }, { L"テキスト速度", ScriptCommand::Type::TextSpeed }, { L"フォント", ScriptCommand::Type::MessageFont }, { L"フォントリセット", ScriptCommand::Type::MessageFontReset }, { L"メッセージUI", ScriptCommand::Type::MessageStyle }, { L"本文色", ScriptCommand::Type::TextColor }, { L"名前色", ScriptCommand::Type::NameColor }, { L"名前欄", ScriptCommand::Type::NameWindow }, { L"縦書き", ScriptCommand::Type::VerticalText } } },
            { L"サウンド", RGB(146, 122, 234), { { L"BGM", ScriptCommand::Type::Bgm }, { L"SE", ScriptCommand::Type::Se }, { L"ボイス", ScriptCommand::Type::Voice }, { L"BGM停止", ScriptCommand::Type::StopBgm } } },
            { L"システム", RGB(255, 148, 66), { { L"変数設定", ScriptCommand::Type::SetValue }, { L"変数加算", ScriptCommand::Type::AddValue }, { L"条件分岐", ScriptCommand::Type::IfJump } } },
        };

        auto drawCircleIcon = [&](const RECT& rect, COLORREF accent)
        {
            HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            HPEN accentPen = CreatePen(PS_SOLID, 2, accent);
            HGDIOBJ oldBrush = SelectObject(hdc, whiteBrush);
            HGDIOBJ oldPen = SelectObject(hdc, accentPen);
            Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(accentPen);
            DeleteObject(whiteBrush);
        };

        for (const auto& section : sections)
        {
            const int itemCount = static_cast<int>(section.items.size());
            PaletteSectionItem& sectionState = paletteSections_[&section - sections];
            const int cardInnerWidth = (panelRect.right - panelRect.left) - 44;
            const int columnCount = cardInnerWidth < 240 ? 1 : 2;
            const int rows = itemCount > 0 ? (itemCount + columnCount - 1) / columnCount : 0;
            const int buttonWidth = columnCount == 1
                ? cardInnerWidth
                : (std::max)(56, (cardInnerWidth - buttonGap) / columnCount);
            const int sectionHeight = sectionState.expanded ? 30 + sectionPadding * 2 + rows * buttonHeight + (rows - 1) * buttonGap : 32;
            RECT cardRect = { panelRect.left + 12, cursorY, panelRect.right - 12, cursorY + sectionHeight };
            const int innerLeft = cardRect.left + 10;
            const int innerRight = cardRect.right - 10;
            sectionState.rect = cardRect;
            sectionState.toggleRect = { cardRect.right - 28, cardRect.top + 2, cardRect.right - 8, cardRect.top + 22 };

            HBRUSH cardBrush = CreateSolidBrush(RGB(30, 35, 42));
            FillRect(hdc, &cardRect, cardBrush);
            DeleteObject(cardBrush);
            HBRUSH cardFrameBrush = CreateSolidBrush(RGB(62, 68, 76));
            FrameRect(hdc, &cardRect, cardFrameBrush);
            DeleteObject(cardFrameBrush);

            const int titleBandWidth = (std::min)(120, (std::max)(80, static_cast<int>(cardRect.right - cardRect.left - 32)));
            RECT titleBand = { cardRect.left, cardRect.top, cardRect.left + titleBandWidth, cardRect.top + 24 };
            HBRUSH titleBrush = CreateSolidBrush(section.color);
            FillRect(hdc, &titleBand, titleBrush);
            DeleteObject(titleBrush);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawWrappedText(hdc, titleBand, section.title, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SetTextColor(hdc, RGB(205, 212, 220));
            DrawWrappedText(hdc, sectionState.toggleRect, sectionState.expanded ? L"\u25b2" : L"\u25bc", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            if (!sectionState.expanded)
            {
                cursorY += 32 + sectionGap;
                continue;
            }

            int itemIndex = 0;
            for (const auto& item : section.items)
            {
                const int column = itemIndex % columnCount;
                const int row = itemIndex / columnCount;
                RECT buttonRect =
                {
                    innerLeft + column * (buttonWidth + buttonGap),
                    cardRect.top + 34 + row * (buttonHeight + buttonGap),
                    innerLeft + column * (buttonWidth + buttonGap) + buttonWidth,
                    cardRect.top + 34 + row * (buttonHeight + buttonGap) + buttonHeight
                };
                buttonRect.right = (std::min<LONG>)(buttonRect.right, static_cast<LONG>(innerRight));
                HBRUSH buttonBrush = CreateSolidBrush(RGB(40, 46, 54));
                FillRect(hdc, &buttonRect, buttonBrush);
                DeleteObject(buttonBrush);
                HBRUSH buttonFrameBrush = CreateSolidBrush(RGB(72, 80, 90));
                FrameRect(hdc, &buttonRect, buttonFrameBrush);
                DeleteObject(buttonFrameBrush);

                RECT iconRect = { buttonRect.left + 8, buttonRect.top + 7, buttonRect.left + 28, buttonRect.top + 27 };
                drawCircleIcon(iconRect, section.color);

                RECT labelRect = { buttonRect.left + 34, buttonRect.top, buttonRect.right - 8, buttonRect.bottom };
                SetTextColor(hdc, RGB(220, 225, 232));
                DrawWrappedText(hdc, labelRect, item.first, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                paletteButtons_.push_back(PaletteButtonItem{ item.first, item.second, buttonRect });
                ++itemIndex;
            }

            cursorY += sectionHeight + sectionGap;
        }

        const int visibleHeight = static_cast<int>(panelRect.bottom - (panelRect.top + 54));
        const int contentBottom = cursorY + componentScrollOffset_;
        const int contentHeight = static_cast<int>((contentBottom - (panelRect.top + 54)) - visibleHeight + 12);
        componentScrollMax_ = (std::max)(0, contentHeight);
        componentScrollOffset_ = (std::min)(componentScrollOffset_, componentScrollMax_);
        RECT scrollContentRect = { panelRect.left, panelRect.top + 54, panelRect.right, panelRect.bottom - 10 };
        DrawVerticalScrollbar(hdc, scrollContentRect, componentScrollOffset_, componentScrollMax_, RGB(28, 34, 40), RGB(102, 112, 124));
    }
    if (leftPanelTab_ != LeftPanelTab::Components)
    {
        componentScrollOffset_ = 0;
        componentScrollMax_ = 0;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
}

void NovelRuntime::DrawNodeGraph(HDC hdc, const RECT& panelRect)
{
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(16, 20, 28));
    FillRect(hdc, &panelRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    FrameRect(hdc, &panelRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT nodeFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    RECT titleRect = { panelRect.left + 16, panelRect.top + 12, panelRect.right - 16, panelRect.top + 40 };
    SetTextColor(hdc, RGB(245, 247, 250));
    DrawWrappedText(hdc, titleRect, L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, nodeFont);
    const int topY = panelRect.top + 56;
    const int labelY = panelRect.top + 150;
    const int nodeWidth = 170;
    const int nodeHeight = 52;
    const int labelWidth = 150;
    const int labelHeight = 44;
    const int leftMargin = panelRect.left + 20;
    const int horizontalGap = 24;

    std::unordered_map<size_t, RECT> nodeRects;
    std::unordered_map<std::wstring, RECT> labelRects;
    std::vector<size_t> sourceIndices;
    std::vector<size_t> labelIndices;

    for (size_t i = 0; i < scenario_.commands.size(); ++i)
    {
        const ScriptCommand& command = scenario_.commands[i];
        if (command.type == ScriptCommand::Type::Choice || command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::IfJump)
        {
            sourceIndices.push_back(i);
        }
        if (command.type == ScriptCommand::Type::Label)
        {
            labelIndices.push_back(i);
        }
    }

    for (size_t i = 0; i < sourceIndices.size(); ++i)
    {
        RECT rect = {
            leftMargin + static_cast<int>(i) * (nodeWidth + horizontalGap),
            topY,
            leftMargin + static_cast<int>(i) * (nodeWidth + horizontalGap) + nodeWidth,
            topY + nodeHeight
        };
        nodeRects[sourceIndices[i]] = rect;
        graphNodeRects_.push_back(rect);
        graphNodeIndices_.push_back(sourceIndices[i]);
    }

    for (size_t i = 0; i < labelIndices.size(); ++i)
    {
        RECT rect = {
            leftMargin + static_cast<int>(i) * (labelWidth + horizontalGap),
            labelY,
            leftMargin + static_cast<int>(i) * (labelWidth + horizontalGap) + labelWidth,
            labelY + labelHeight
        };
        const std::wstring name = GetCommandParameter(scenario_.commands[labelIndices[i]], L"name");
        labelRects[name] = rect;
        graphNodeRects_.push_back(rect);
        graphNodeIndices_.push_back(labelIndices[i]);
    }

    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(120, 190, 255));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, linePen));
    for (size_t sourceIndex : sourceIndices)
    {
        const ScriptCommand& command = scenario_.commands[sourceIndex];
        RECT fromRect = nodeRects[sourceIndex];
        POINT from = { (fromRect.left + fromRect.right) / 2, fromRect.bottom };

        if (command.type == ScriptCommand::Type::Choice)
        {
            for (size_t linkIndex = 0; linkIndex < command.links.size(); ++linkIndex)
            {
                const auto& link = command.links[linkIndex];
                const auto found = labelRects.find(link.second);
                if (found == labelRects.end())
                {
                    continue;
                }

                RECT toRect = found->second;
                HPEN choicePen = CreatePen(
                    PS_SOLID,
                    sourceIndex == selectedCommandIndex_ && linkIndex == selectedChoiceLinkIndex_ ? 3 : 2,
                    sourceIndex == selectedCommandIndex_ && linkIndex == selectedChoiceLinkIndex_ ? RGB(255, 215, 120) : RGB(120, 190, 255));
                HPEN previousPen = static_cast<HPEN>(SelectObject(hdc, choicePen));
                MoveToEx(hdc, from.x, from.y, nullptr);
                LineTo(hdc, (toRect.left + toRect.right) / 2, toRect.top);
                SelectObject(hdc, previousPen);
                DeleteObject(choicePen);
            }
        }
        else
        {
            const std::wstring target = GetCommandParameter(command, L"target");
            const auto found = labelRects.find(target);
            if (found != labelRects.end())
            {
                RECT toRect = found->second;
                MoveToEx(hdc, from.x, from.y, nullptr);
                LineTo(hdc, (toRect.left + toRect.right) / 2, toRect.top);
            }
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);

    for (size_t sourceIndex : sourceIndices)
    {
        const ScriptCommand& command = scenario_.commands[sourceIndex];
        RECT rect = nodeRects[sourceIndex];
        COLORREF fill = sourceIndex == selectedCommandIndex_ ? RGB(84, 105, 70) : RGB(38, 48, 64);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT typeRect = { rect.left + 8, rect.top + 6, rect.right - 8, rect.top + 24 };
        SetTextColor(hdc, RGB(255, 225, 160));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT textRect = { rect.left + 8, rect.top + 24, rect.right - 8, rect.bottom - 6 };
        SetTextColor(hdc, RGB(232, 238, 244));
        DrawWrappedText(hdc, textRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_WORDBREAK);

        if (command.type == ScriptCommand::Type::Choice && sourceIndex == selectedCommandIndex_)
        {
            RECT badgeRect = { rect.right - 28, rect.top + 6, rect.right - 8, rect.top + 22 };
            HBRUSH badgeBrush = CreateSolidBrush(RGB(255, 215, 120));
            FillRect(hdc, &badgeRect, badgeBrush);
            DeleteObject(badgeBrush);
            SetTextColor(hdc, RGB(20, 24, 28));
            DrawTextW(hdc, std::to_wstring(selectedChoiceLinkIndex_ + 1).c_str(), -1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    for (size_t labelIndex : labelIndices)
    {
        const ScriptCommand& command = scenario_.commands[labelIndex];
        const std::wstring name = GetCommandParameter(command, L"name");
        RECT rect = labelRects[name];
        COLORREF fill = labelIndex == selectedCommandIndex_ ? RGB(96, 92, 52) : RGB(58, 54, 44);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        RECT textRect = { rect.left + 8, rect.top + 8, rect.right - 8, rect.bottom - 8 };
        SetTextColor(hdc, RGB(245, 238, 216));
        DrawWrappedText(hdc, textRect, L"*" + name, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(nodeFont);
}

std::wstring NovelRuntime::GetCommandTypeLabel(const ScriptCommand& command) const
{
    switch (command.type)
    {
    case ScriptCommand::Type::Title: return L"\u30bf\u30a4\u30c8\u30eb";
    case ScriptCommand::Type::Background: return L"\u80cc\u666f";
    case ScriptCommand::Type::Character: return L"\u7acb\u3061\u7d75";
    case ScriptCommand::Type::HideCharacter: return L"\u7acb\u3061\u7d75\u975e\u8868\u793a";
    case ScriptCommand::Type::Speaker: return L"\u8a71\u8005";
    case ScriptCommand::Type::ClearSpeaker: return L"\u8a71\u8005\u30af\u30ea\u30a2";
    case ScriptCommand::Type::Text: return L"\u672c\u6587";
    case ScriptCommand::Type::MessageWindow: return L"\u30e1\u30c3\u30bb\u30fc\u30b8\u67a0";
    case ScriptCommand::Type::TextSpeed: return L"\u30c6\u30ad\u30b9\u30c8\u901f\u5ea6";
    case ScriptCommand::Type::MessageFont: return L"\u30d5\u30a9\u30f3\u30c8";
    case ScriptCommand::Type::MessageFontReset: return L"\u30d5\u30a9\u30f3\u30c8\u30ea\u30bb\u30c3\u30c8";
    case ScriptCommand::Type::MessageStyle: return L"メッセージUI";
    case ScriptCommand::Type::TextColor: return L"本文色";
    case ScriptCommand::Type::NameColor: return L"名前色";
    case ScriptCommand::Type::NameWindow: return L"名前欄";
    case ScriptCommand::Type::VerticalText: return L"縦書き";
    case ScriptCommand::Type::PageBreak: return L"改ページ";
    case ScriptCommand::Type::Shake: return L"画面揺れ";
    case ScriptCommand::Type::Fade: return L"フェード";
    case ScriptCommand::Type::Transition: return L"トランジション";
    case ScriptCommand::Type::Zoom: return L"ズーム";
    case ScriptCommand::Type::Pan: return L"パン";
    case ScriptCommand::Type::Flash: return L"フラッシュ";
    case ScriptCommand::Type::Tint: return L"画面色調";
    case ScriptCommand::Type::Choice: return L"\u9078\u629e\u80a2";
    case ScriptCommand::Type::Bgm: return L"BGM";
    case ScriptCommand::Type::StopBgm: return L"BGM\u505c\u6b62";
    case ScriptCommand::Type::Se: return L"SE";
    case ScriptCommand::Type::Voice: return L"\u30dc\u30a4\u30b9";
    case ScriptCommand::Type::SetValue: return L"\u5909\u6570\u8a2d\u5b9a";
    case ScriptCommand::Type::AddValue: return L"\u52a0\u7b97";
    case ScriptCommand::Type::IfJump: return L"\u6761\u4ef6\u5206\u5c90";
    case ScriptCommand::Type::Jump: return L"\u30b8\u30e3\u30f3\u30d7";
    case ScriptCommand::Type::Label: return L"\u30e9\u30d9\u30eb";
    }
    return L"\u4e0d\u660e";
}

std::wstring NovelRuntime::GetCommandSummary(const ScriptCommand& command) const
{
    if (command.type == ScriptCommand::Type::Text)
    {
        return GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::Speaker || command.type == ScriptCommand::Type::Title)
    {
        return GetCommandParameter(command, L"name");
    }
    if (command.type == ScriptCommand::Type::MessageWindow)
    {
        return ParseBoolValue(GetCommandParameter(command, L"visible"), true) ? L"表示" : L"非表示";
    }
    if (command.type == ScriptCommand::Type::TextSpeed)
    {
        return GetCommandParameter(command, L"value") + L"ms";
    }
    if (command.type == ScriptCommand::Type::MessageFont)
    {
        return GetCommandParameter(command, L"face");
    }
    if (command.type == ScriptCommand::Type::MessageFontReset)
    {
        return L"標準フォントへ戻す";
    }
    if (command.type == ScriptCommand::Type::MessageStyle)
    {
        const std::wstring image = GetCommandParameter(command, L"image");
        return L"色 " + GetCommandParameter(command, L"color") + L" / " + GetCommandParameter(command, L"opacity") + L"%"
            + (image.empty() ? L"" : L" / 画像あり");
    }
    if (command.type == ScriptCommand::Type::TextColor || command.type == ScriptCommand::Type::NameColor)
    {
        return GetCommandParameter(command, L"color");
    }
    if (command.type == ScriptCommand::Type::NameWindow)
    {
        const std::wstring mode = ParseBoolValue(GetCommandParameter(command, L"visible"), true) ? L"表示" : L"非表示";
        return mode + L" / (" + GetCommandParameter(command, L"x") + L"," + GetCommandParameter(command, L"y") + L")";
    }
    if (command.type == ScriptCommand::Type::VerticalText)
    {
        return ParseBoolValue(GetCommandParameter(command, L"enabled"), true) ? L"有効" : L"無効";
    }
    if (command.type == ScriptCommand::Type::PageBreak)
    {
        return L"本文を区切って次ページへ";
    }
    if (command.type == ScriptCommand::Type::Shake)
    {
        return L"time=" + GetCommandParameter(command, L"time") + L" power=" + GetCommandParameter(command, L"power");
    }
    if (command.type == ScriptCommand::Type::Fade)
    {
        return GetEffectTargetLabel(GetCommandParameter(command, L"target")) + L" / " + GetCommandParameter(command, L"color") + L" / " + GetCommandParameter(command, L"time") + L"ms";
    }
    if (command.type == ScriptCommand::Type::Transition)
    {
        return GetEffectTargetLabel(GetCommandParameter(command, L"target")) + L" / " + GetCommandParameter(command, L"style") + L" / " + GetCommandParameter(command, L"time") + L"ms";
    }
    if (command.type == ScriptCommand::Type::Zoom)
    {
        return GetCommandParameter(command, L"scale") + L"% / " + GetCommandParameter(command, L"time") + L"ms";
    }
    if (command.type == ScriptCommand::Type::Pan)
    {
        return L"X=" + GetCommandParameter(command, L"x") + L" Y=" + GetCommandParameter(command, L"y");
    }
    if (command.type == ScriptCommand::Type::Flash)
    {
        return GetCommandParameter(command, L"color") + L" / " + GetCommandParameter(command, L"time") + L"ms";
    }
    if (command.type == ScriptCommand::Type::Tint)
    {
        return GetCommandParameter(command, L"color") + L" / " + GetCommandParameter(command, L"opacity");
    }
    if (command.type == ScriptCommand::Type::Background)
    {
        const std::wstring color = GetCommandParameter(command, L"color");
        return !color.empty() ? color : GetCommandParameter(command, L"storage");
    }
    if (command.type == ScriptCommand::Type::Character)
    {
        return GetCommandParameter(command, L"pos") + L" : " + GetCommandParameter(command, L"name");
    }
    if (command.type == ScriptCommand::Type::HideCharacter || command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::Label)
    {
        const std::wstring pos = GetCommandParameter(command, L"pos");
        const std::wstring target = GetCommandParameter(command, L"target");
        const std::wstring name = GetCommandParameter(command, L"name");
        if (!pos.empty()) return pos;
        if (!target.empty()) return target;
        return name;
    }
    if (command.type == ScriptCommand::Type::SetValue || command.type == ScriptCommand::Type::AddValue)
    {
        return GetCommandParameter(command, L"name") + L" = " + GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::IfJump)
    {
        return GetCommandParameter(command, L"name") + L" " + GetCommandParameter(command, L"op") + L" " + GetCommandParameter(command, L"value");
    }
    if (command.type == ScriptCommand::Type::Choice)
    {
        return GetCommandParameter(command, L"prompt");
    }
    if (command.type == ScriptCommand::Type::Bgm || command.type == ScriptCommand::Type::Se || command.type == ScriptCommand::Type::Voice)
    {
        return GetCommandParameter(command, L"storage");
    }
    if (command.type == ScriptCommand::Type::StopBgm)
    {
        return L"\u518d\u751f\u4e2d\u306e BGM \u3092\u505c\u6b62";
    }
    return L"";
}

void NovelRuntime::DrawCommandList(HDC hdc, const RECT& panelRect)
{
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT rowFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    SetTextColor(hdc, RGB(240, 242, 246));
    RECT titleRect = { panelRect.left + 20, panelRect.top + 18, panelRect.right - 20, panelRect.top + 52 };
    DrawWrappedText(hdc, titleRect, L"Scenario Outline", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, rowFont);
    const int rowHeight = 42;
    const int headerBottom = panelRect.top + 68;
    const size_t visibleRows = static_cast<size_t>((panelRect.bottom - headerBottom - 12) / rowHeight);
    const size_t total = scenario_.commands.size();
    const size_t lastIndex = total == 0 ? 0ull : total - 1;
    const size_t anchor = (std::min)(selectedCommandIndex_, lastIndex);
    size_t start = anchor > visibleRows / 2 ? anchor - visibleRows / 2 : 0;
    if (start + visibleRows > total)
    {
        start = total > visibleRows ? total - visibleRows : 0;
    }

    for (size_t i = 0; i < visibleRows && start + i < total; ++i)
    {
        const size_t commandIndex = start + i;
        const ScriptCommand& command = scenario_.commands[commandIndex];
        RECT rowRect = { panelRect.left + 12, headerBottom + static_cast<int>(i) * rowHeight, panelRect.right - 12, headerBottom + static_cast<int>(i + 1) * rowHeight - 6 };
        commandRowRects_.push_back(rowRect);
        commandRowIndices_.push_back(commandIndex);

        COLORREF rowColor = RGB(28, 35, 46);
        if (commandIndex == currentCommandIndex_ && !scenario_.commands.empty())
        {
            rowColor = RGB(47, 84, 122);
        }
        if (commandIndex == selectedCommandIndex_)
        {
            rowColor = RGB(83, 104, 66);
        }
        HBRUSH rowBrush = CreateSolidBrush(rowColor);
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);

        RECT typeRect = { rowRect.left + 10, rowRect.top + 4, rowRect.left + 110, rowRect.bottom - 4 };
        SetTextColor(hdc, RGB(255, 225, 160));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT summaryRect = { rowRect.left + 110, rowRect.top + 4, rowRect.right - 10, rowRect.bottom - 4 };
        SetTextColor(hdc, RGB(230, 236, 240));
        DrawWrappedText(hdc, summaryRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(rowFont);
}

void NovelRuntime::DrawEventList(HDC hdc, const RECT& panelRect)
{
    currentEventListRect_ = panelRect;
    eventTextEditRect_ = {};
    const std::vector<ScenarioIssue> scenarioIssues = ValidateScenario();
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    HBRUSH listBrush = CreateSolidBrush(RGB(18, 24, 32));
    FillRect(hdc, &panelRect, listBrush);
    DeleteObject(listBrush);
    FrameRect(hdc, &panelRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    RECT titleRect = { panelRect.left + 12, panelRect.top + 8, panelRect.right - 12, panelRect.top + 34 };
    SetTextColor(hdc, RGB(228, 234, 240));
    DrawWrappedText(hdc, titleRect, L"\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT searchLabelRect = { panelRect.left + 224, panelRect.top + 8, panelRect.left + 320, panelRect.top + 32 };
    SetTextColor(hdc, RGB(168, 178, 188));
    DrawWrappedText(hdc, searchLabelRect, L"\u691c\u7d22", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    eventAddTextRect_ = { panelRect.right - 340, panelRect.top + 8, panelRect.right - 280, panelRect.top + 32 };
    eventAddChoiceRect_ = { panelRect.right - 274, panelRect.top + 8, panelRect.right - 202, panelRect.top + 32 };
    eventValidateRect_ = { panelRect.right - 194, panelRect.top + 8, panelRect.right - 124, panelRect.top + 32 };
    eventDuplicateRect_ = { panelRect.right - 116, panelRect.top + 8, panelRect.right - 56, panelRect.top + 32 };
    eventDeleteRect_ = { panelRect.right - 52, panelRect.top + 8, panelRect.right - 12, panelRect.top + 32 };

    const struct
    {
        RECT rect;
        const wchar_t* label;
        COLORREF fill;
    } actions[] =
    {
        { eventAddTextRect_, L"+ 本文", RGB(68, 92, 118) },
        { eventAddChoiceRect_, L"+ 分岐", RGB(92, 74, 118) },
        { eventValidateRect_, scenarioIssues.empty() ? L"検証" : L"警告", scenarioIssues.empty() ? RGB(58, 88, 118) : RGB(154, 96, 44) },
        { eventDuplicateRect_, L"\u8907\u88fd", RGB(76, 96, 76) },
        { eventDeleteRect_, L"-", RGB(132, 58, 66) },
    };

    for (const auto& action : actions)
    {
        HBRUSH actionBrush = CreateSolidBrush(action.fill);
        FillRect(hdc, &action.rect, actionBrush);
        DeleteObject(actionBrush);
        FrameRect(hdc, &action.rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(245, 248, 250));
        DrawWrappedText(hdc, action.rect, action.label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    SelectObject(hdc, bodyFont);
    const int rowHeight = 32;
    const int expandedHeight = 112;
    const int startY = panelRect.top + 44;
    const std::vector<size_t> filteredIndices = BuildFilteredEventIndices();
    const int visibleHeight = static_cast<int>(panelRect.bottom - startY - 8);
    const int visibleRows = (std::max)(1, visibleHeight / rowHeight);
    const int maxOffset = (std::max)(0, static_cast<int>(filteredIndices.size()) - visibleRows);
    eventListScrollOffset_ = (std::max)(0, (std::min)(eventListScrollOffset_, maxOffset));
    int cursorY = startY - (eventListScrollOffset_ * rowHeight);
    for (size_t filteredIndex = 0; filteredIndex < filteredIndices.size(); ++filteredIndex)
    {
        const size_t commandIndex = filteredIndices[filteredIndex];
        const ScriptCommand& command = scenario_.commands[commandIndex];
        std::wstring issueMessage;
        for (const ScenarioIssue& issue : scenarioIssues)
        {
            if (issue.commandIndex == commandIndex)
            {
                issueMessage = issue.message;
                break;
            }
        }
        const bool disabled = ParseBoolValue(GetCommandParameter(command, L"disabled"), false);
        const bool expanded = command.type == ScriptCommand::Type::Text && expandedTextCommandIndex_ == commandIndex;
        const int fullHeight = rowHeight + (expanded ? expandedHeight : 0);
        RECT rowRect = { panelRect.left + 10, cursorY, panelRect.right - 10, cursorY + rowHeight - 4 };
        cursorY += fullHeight;
        if (rowRect.bottom < startY || rowRect.top > panelRect.bottom - 8)
        {
            continue;
        }
        eventRowRects_.push_back(rowRect);
        eventRowIndices_.push_back(commandIndex);

        HBRUSH rowBrush = CreateSolidBrush(commandIndex == selectedCommandIndex_ ? RGB(34, 44, 58) : (disabled ? RGB(18, 22, 28) : RGB(22, 28, 38)));
        FillRect(hdc, &rowRect, rowBrush);
        DeleteObject(rowBrush);
        if (assetDragActive_ && assetDragMoved_ && commandIndex == assetDropTargetIndex_)
        {
            HBRUSH hoverBrush = CreateSolidBrush(RGB(30, 68, 54));
            FrameRect(hdc, &rowRect, hoverBrush);
            DeleteObject(hoverBrush);
        }
        if (eventReorderDragActive_ && eventReorderMoved_ && commandIndex == eventEffectDropTargetIndex_)
        {
            HBRUSH hoverBrush = CreateSolidBrush(RGB(56, 96, 150));
            FrameRect(hdc, &rowRect, hoverBrush);
            DeleteObject(hoverBrush);
        }

        RECT accentRect = { rowRect.left, rowRect.top, rowRect.left + 118, rowRect.bottom };
        COLORREF accentColor = GetCommandAccentColor(command);
        if (disabled)
        {
            accentColor = RGB(GetRValue(accentColor) / 2, GetGValue(accentColor) / 2, GetBValue(accentColor) / 2);
        }
        HBRUSH accentBrush = CreateSolidBrush(accentColor);
        FillRect(hdc, &accentRect, accentBrush);
        DeleteObject(accentBrush);

        RECT typeRect = { accentRect.left + 8, accentRect.top + 2, accentRect.right - 8, accentRect.bottom - 2 };
        SetTextColor(hdc, RGB(255, 255, 255));
        DrawWrappedText(hdc, typeRect, GetCommandTypeLabel(command) + (disabled ? L" [OFF]" : L""), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT summaryRect = { accentRect.right + 10, rowRect.top + 2, rowRect.right - (issueMessage.empty() ? 30 : 54), rowRect.bottom - 2 };
        SetTextColor(hdc, disabled ? RGB(136, 144, 152) : RGB(206, 214, 222));
        DrawWrappedText(hdc, summaryRect, GetCommandSummary(command), DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);

        if (!issueMessage.empty())
        {
            RECT issueRect = { rowRect.right - 48, rowRect.top + 5, rowRect.right - 28, rowRect.bottom - 5 };
            HBRUSH issueBrush = CreateSolidBrush(RGB(194, 126, 42));
            FillRect(hdc, &issueRect, issueBrush);
            DeleteObject(issueBrush);
            SetTextColor(hdc, RGB(32, 24, 16));
            DrawWrappedText(hdc, issueRect, L"!", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        if (command.type == ScriptCommand::Type::Text)
        {
            RECT expandRect = { rowRect.right - 24, rowRect.top + 5, rowRect.right - 6, rowRect.bottom - 5 };
            eventExpandRects_.push_back(expandRect);
            eventExpandIndices_.push_back(commandIndex);
            SetTextColor(hdc, RGB(244, 248, 252));
            DrawWrappedText(hdc, expandRect, expanded ? L"\u25b2" : L"\u25bc", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            if (expanded)
            {
                RECT editorFrame = { rowRect.left + 40, rowRect.bottom + 6, rowRect.right - 10, rowRect.bottom + expandedHeight };
                HBRUSH editorBrush = CreateSolidBrush(RGB(245, 245, 245));
                FillRect(hdc, &editorFrame, editorBrush);
                DeleteObject(editorBrush);
                FrameRect(hdc, &editorFrame, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

                RECT editorLabelRect = { editorFrame.left + 12, editorFrame.top + 8, editorFrame.right - 12, editorFrame.top + 28 };
                SetTextColor(hdc, RGB(56, 62, 70));
                DrawWrappedText(hdc, editorLabelRect, L"\u672c\u6587\u5165\u529b", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                eventTextEditRect_ = { editorFrame.left + 12, editorFrame.top + 30, editorFrame.right - 12, editorFrame.bottom - 12 };
            }
        }
    }

    if (paletteDragActive_ && paletteDropValid_)
    {
        int indicatorY = panelRect.top + 52;
        int indicatorLeft = panelRect.left + 10;
        int indicatorRight = panelRect.right - 10;
        if (!eventRowRects_.empty())
        {
            indicatorY = eventRowRects_.back().bottom + 4;
            for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
            {
                if (dragInsertIndex_ <= eventRowIndices_[i])
                {
                    indicatorY = eventRowRects_[i].top - 2;
                    break;
                }
            }
        }

        HPEN dropPen = CreatePen(PS_SOLID, 3, RGB(112, 184, 255));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, dropPen));
        MoveToEx(hdc, indicatorLeft, indicatorY, nullptr);
        LineTo(hdc, indicatorRight, indicatorY);
        SelectObject(hdc, oldPen);
        DeleteObject(dropPen);
    }
    else if (eventReorderDragActive_ && eventReorderMoved_)
    {
        if (eventEffectDropTargetIndex_ < scenario_.commands.size())
        {
            RECT scrollContentRect = { panelRect.left, startY, panelRect.right, panelRect.bottom - 8 };
            DrawVerticalScrollbar(hdc, scrollContentRect, eventListScrollOffset_, maxOffset, RGB(24, 30, 38), RGB(106, 120, 136));
            SelectObject(hdc, oldFont);
            DeleteObject(headerFont);
            DeleteObject(bodyFont);
            return;
        }
        int indicatorY = panelRect.top + 52;
        int indicatorLeft = panelRect.left + 10;
        int indicatorRight = panelRect.right - 10;
        if (!eventRowRects_.empty())
        {
            indicatorY = eventRowRects_.back().bottom + 4;
            for (size_t i = 0; i < eventRowRects_.size() && i < eventRowIndices_.size(); ++i)
            {
                if (eventDragInsertIndex_ <= eventRowIndices_[i])
                {
                    indicatorY = eventRowRects_[i].top - 2;
                    break;
                }
            }
        }

        HPEN dropPen = CreatePen(PS_SOLID, 3, RGB(112, 184, 255));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, dropPen));
        MoveToEx(hdc, indicatorLeft, indicatorY, nullptr);
        LineTo(hdc, indicatorRight, indicatorY);
        SelectObject(hdc, oldPen);
        DeleteObject(dropPen);
    }

    RECT scrollContentRect = { panelRect.left, startY, panelRect.right, panelRect.bottom - 8 };
    DrawVerticalScrollbar(hdc, scrollContentRect, eventListScrollOffset_, maxOffset, RGB(24, 30, 38), RGB(106, 120, 136));

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(bodyFont);
}

void NovelRuntime::DrawInspector(HDC hdc, const RECT& panelRect)
{
    inspectorEditTargets_.clear();
    inspectorActionTargets_.clear();
    inspectorCommitRect_ = {};
    inspectorCancelRect_ = {};
    SetBkMode(hdc, TRANSPARENT);
    HFONT headerFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT bodyFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, headerFont));

    HBRUSH panelBrush = CreateSolidBrush(RGB(18, 24, 32));
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);

    SetTextColor(hdc, RGB(240, 242, 246));
    RECT titleRect = { panelRect.left + 20, panelRect.top + 18, panelRect.right - 20, panelRect.top + 52 };
    DrawWrappedText(hdc, titleRect, L"\u30a4\u30f3\u30b9\u30da\u30af\u30bf", DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, bodyFont);
    int cursorY = panelRect.top + 72 - inspectorScrollOffset_;
    const int lineHeight = 26;
    auto drawLine = [&](const std::wstring& line, COLORREF color)
    {
        RECT lineRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + lineHeight };
        SetTextColor(hdc, color);
        DrawWrappedText(hdc, lineRect, line, DT_LEFT | DT_WORDBREAK);
        cursorY += lineHeight;
    };
    auto drawEditable = [&](size_t commandIndex, const std::wstring& label, const std::wstring& key, const std::wstring& value)
    {
        const bool numericField =
            key == L"time" || key == L"x" || key == L"y" || key == L"scale" || key == L"opacity" ||
            key == L"volume" || key == L"fadein" || key == L"fadeout" || key == L"power" || key == L"value";
        RECT textRect = { panelRect.left + 20, cursorY, panelRect.right - (numericField ? 150 : 90), cursorY + lineHeight };
        RECT buttonRect = { panelRect.right - (numericField ? 140 : 80), cursorY, panelRect.right - (numericField ? 80 : 20), cursorY + lineHeight - 4 };
        SetTextColor(hdc, RGB(205, 214, 222));
        DrawWrappedText(hdc, textRect, label + L": " + value, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
        HBRUSH editBrush = CreateSolidBrush(RGB(58, 88, 118));
        FillRect(hdc, &buttonRect, editBrush);
        DeleteObject(editBrush);
        FrameRect(hdc, &buttonRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(245, 248, 250));
        DrawWrappedText(hdc, buttonRect, L"\u7de8\u96c6", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        inspectorEditTargets_.push_back(InspectorEditTarget{ commandIndex, key, label, buttonRect });
        if (numericField)
        {
            RECT minusRect = { panelRect.right - 72, cursorY, panelRect.right - 46, cursorY + lineHeight - 4 };
            RECT plusRect = { panelRect.right - 42, cursorY, panelRect.right - 16, cursorY + lineHeight - 4 };
            HBRUSH spinBrush = CreateSolidBrush(RGB(224, 228, 232));
            FillRect(hdc, &minusRect, spinBrush);
            FillRect(hdc, &plusRect, spinBrush);
            DeleteObject(spinBrush);
            FrameRect(hdc, &minusRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            FrameRect(hdc, &plusRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            SetTextColor(hdc, RGB(40, 56, 72));
            DrawWrappedText(hdc, minusRect, L"-", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            DrawWrappedText(hdc, plusRect, L"+", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"nudge:-:" + key, commandIndex, 0, minusRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"nudge:+:" + key, commandIndex, 0, plusRect });
        }
        cursorY += lineHeight;
    };
    auto drawActionButton = [&](const RECT& rect, const std::wstring& label, COLORREF fill)
    {
        HBRUSH buttonBrush = CreateSolidBrush(fill);
        FillRect(hdc, &rect, buttonBrush);
        DeleteObject(buttonBrush);
        FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(245, 248, 250));
        DrawWrappedText(hdc, rect, label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };

    if (!scenario_.commands.empty() && selectedCommandIndex_ < scenario_.commands.size())
    {
        const ScriptCommand& command = scenario_.commands[selectedCommandIndex_];
        drawLine(L"\u7a2e\u985e: " + GetCommandTypeLabel(command), RGB(255, 225, 160));
        drawLine(L"\u884c\u756a\u53f7: " + std::to_wstring(command.sourceLine), RGB(205, 214, 222));
        drawLine(L"\u6982\u8981: " + GetCommandSummary(command), RGB(230, 236, 240));
        const std::wstring issueMessage = GetFirstIssueForCommand(selectedCommandIndex_);
        if (!issueMessage.empty())
        {
            drawLine(L"警告: " + issueMessage, RGB(255, 188, 92));
        }
        cursorY += 8;
        drawLine(L"\u30d1\u30e9\u30e1\u30fc\u30bf", RGB(255, 225, 160));
        if (command.type == ScriptCommand::Type::Text)
        {
            drawLine(L"\u672c\u6587\u306f\u4e2d\u592e\u306e\u30a4\u30d9\u30f3\u30c8\u4e00\u89a7\u304b\u3089\u958b\u3044\u3066\u7de8\u96c6\u3057\u307e\u3059\u3002", RGB(180, 188, 196));
            drawLine(L"\u73fe\u5728\u306e\u672c\u6587: " + GetCommandParameter(command, L"value"), RGB(205, 214, 222));
        }
        else if (command.type == ScriptCommand::Type::MessageWindow)
        {
            drawEditable(selectedCommandIndex_, L"表示", L"visible", GetCommandParameter(command, L"visible"));
        }
        else if (command.type == ScriptCommand::Type::TextSpeed)
        {
            drawEditable(selectedCommandIndex_, L"速度(ms)", L"value", GetCommandParameter(command, L"value"));
        }
        else if (command.type == ScriptCommand::Type::MessageFont)
        {
            drawEditable(selectedCommandIndex_, L"フォント名", L"face", GetCommandParameter(command, L"face"));
            RECT cycleTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT cycleButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            const std::wstring fontInfo = availableFonts_.empty() ? L"assets/fonts にフォントがありません" : L"候補数: " + std::to_wstring(availableFonts_.size());
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, cycleTextRect, fontInfo, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(cycleButtonRect, L"選択", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"cycle_message_font", selectedCommandIndex_, 0, cycleButtonRect });
            cursorY += lineHeight;

            std::wstring previewFace = GetCommandParameter(command, L"face");
            if (previewFace.empty())
            {
                previewFace = editorSettings_.defaultFont.empty() ? messageFontFace_ : editorSettings_.defaultFont;
            }
            if (previewFace.empty())
            {
                previewFace = L"Yu Gothic UI";
            }

            RECT sampleFrame = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 52 };
            HBRUSH sampleBrush = CreateSolidBrush(RGB(28, 34, 42));
            FillRect(hdc, &sampleFrame, sampleBrush);
            DeleteObject(sampleBrush);
            HBRUSH sampleBorder = CreateSolidBrush(RGB(74, 82, 92));
            FrameRect(hdc, &sampleFrame, sampleBorder);
            DeleteObject(sampleBorder);

            HFONT sampleFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, previewFace.c_str());
            HFONT oldSampleFont = static_cast<HFONT>(SelectObject(hdc, sampleFont));
            RECT sampleTextRect = { sampleFrame.left + 12, sampleFrame.top + 10, sampleFrame.right - 12, sampleFrame.bottom - 10 };
            SetTextColor(hdc, RGB(244, 246, 250));
            DrawWrappedText(hdc, sampleTextRect, L"見本 Aa あア 亜 Sample 0123", DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(hdc, oldSampleFont);
            DeleteObject(sampleFont);
            cursorY += 60;
        }
        else if (command.type == ScriptCommand::Type::MessageFontReset)
        {
            drawLine(L"標準フォントへ戻します。", RGB(205, 214, 222));
        }
        else if (command.type == ScriptCommand::Type::MessageStyle)
        {
            drawEditable(selectedCommandIndex_, L"背景色", L"color", GetCommandParameter(command, L"color"));
            drawEditable(selectedCommandIndex_, L"枠線色", L"border", GetCommandParameter(command, L"border"));
            drawEditable(selectedCommandIndex_, L"透明度(%)", L"opacity", GetCommandParameter(command, L"opacity"));
            drawEditable(selectedCommandIndex_, L"余白", L"padding", GetCommandParameter(command, L"padding"));
            drawEditable(selectedCommandIndex_, L"画像", L"image", GetCommandParameter(command, L"image"));
            RECT browseRect = { panelRect.left + 20, cursorY, panelRect.left + 92, cursorY + 28 };
            RECT clearRect = { panelRect.left + 100, cursorY, panelRect.left + 172, cursorY + 28 };
            drawActionButton(browseRect, L"参照", RGB(58, 88, 118));
            drawActionButton(clearRect, L"解除", RGB(112, 62, 70));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"browse_message_image", selectedCommandIndex_, 0, browseRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"clear_message_image", selectedCommandIndex_, 0, clearRect });
            cursorY += 36;
            drawLine(L"プレビューを開いたまま編集すると即時反映されます。", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::TextColor)
        {
            drawEditable(selectedCommandIndex_, L"本文色", L"color", GetCommandParameter(command, L"color"));
        }
        else if (command.type == ScriptCommand::Type::NameColor)
        {
            drawEditable(selectedCommandIndex_, L"名前色", L"color", GetCommandParameter(command, L"color"));
        }
        else if (command.type == ScriptCommand::Type::NameWindow)
        {
            drawEditable(selectedCommandIndex_, L"表示", L"visible", GetCommandParameter(command, L"visible"));
            drawEditable(selectedCommandIndex_, L"横位置", L"x", GetCommandParameter(command, L"x"));
            drawEditable(selectedCommandIndex_, L"縦位置", L"y", GetCommandParameter(command, L"y"));
            drawEditable(selectedCommandIndex_, L"幅", L"width", GetCommandParameter(command, L"width"));
            drawEditable(selectedCommandIndex_, L"高さ", L"height", GetCommandParameter(command, L"height"));
            drawEditable(selectedCommandIndex_, L"余白", L"padding", GetCommandParameter(command, L"padding"));
            drawEditable(selectedCommandIndex_, L"背景色", L"color", GetCommandParameter(command, L"color"));
            drawEditable(selectedCommandIndex_, L"枠線色", L"border", GetCommandParameter(command, L"border"));
            drawEditable(selectedCommandIndex_, L"透明度(%)", L"opacity", GetCommandParameter(command, L"opacity"));
            drawEditable(selectedCommandIndex_, L"画像", L"image", GetCommandParameter(command, L"image"));
            RECT browseRect = { panelRect.left + 20, cursorY, panelRect.left + 92, cursorY + 28 };
            RECT clearRect = { panelRect.left + 100, cursorY, panelRect.left + 172, cursorY + 28 };
            drawActionButton(browseRect, L"参照", RGB(58, 88, 118));
            drawActionButton(clearRect, L"解除", RGB(112, 62, 70));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"browse_name_image", selectedCommandIndex_, 0, browseRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"clear_name_image", selectedCommandIndex_, 0, clearRect });
            cursorY += 36;
            drawLine(L"話者名をメッセージウィンドウ左上基準で表示します。", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::VerticalText)
        {
            drawEditable(selectedCommandIndex_, L"有効", L"enabled", GetCommandParameter(command, L"enabled"));
            drawLine(L"true で縦書き / false で横書き", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::PageBreak)
        {
            drawLine(L"本文を区切って次のページへ送ります。", RGB(205, 214, 222));
        }
        else if (command.type == ScriptCommand::Type::Wait)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
        }
        else if (command.type == ScriptCommand::Type::Shake)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"強さ", L"power", GetCommandParameter(command, L"power"));
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Fade)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"色", L"color", GetCommandParameter(command, L"color"));
            drawEditable(selectedCommandIndex_, L"不透明度", L"opacity", GetCommandParameter(command, L"opacity"));
            RECT targetTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT targetButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, targetTextRect, L"対象: " + GetEffectTargetLabel(GetCommandParameter(command, L"target")), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(targetButtonRect, L"切替", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"fade_cycle_target", selectedCommandIndex_, 0, targetButtonRect });
            cursorY += lineHeight;
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Transition)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"種類", L"style", GetCommandParameter(command, L"style"));
            drawEditable(selectedCommandIndex_, L"色", L"color", GetCommandParameter(command, L"color"));
            RECT targetTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT targetButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, targetTextRect, L"対象: " + GetEffectTargetLabel(GetCommandParameter(command, L"target")), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(targetButtonRect, L"切替", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"fade_cycle_target", selectedCommandIndex_, 0, targetButtonRect });
            cursorY += lineHeight;
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Zoom)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"拡大率(%)", L"scale", GetCommandParameter(command, L"scale"));
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Pan)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"横位置", L"x", GetCommandParameter(command, L"x"));
            drawEditable(selectedCommandIndex_, L"縦位置", L"y", GetCommandParameter(command, L"y"));
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Flash)
        {
            drawEditable(selectedCommandIndex_, L"時間(ms)", L"time", GetCommandParameter(command, L"time"));
            drawEditable(selectedCommandIndex_, L"色", L"color", GetCommandParameter(command, L"color"));
            drawEditable(selectedCommandIndex_, L"不透明度", L"opacity", GetCommandParameter(command, L"opacity"));
            drawEditable(selectedCommandIndex_, L"並列", L"parallel", GetCommandParameter(command, L"parallel"));
        }
        else if (command.type == ScriptCommand::Type::Tint)
        {
            drawEditable(selectedCommandIndex_, L"色", L"color", GetCommandParameter(command, L"color"));
            drawEditable(selectedCommandIndex_, L"不透明度", L"opacity", GetCommandParameter(command, L"opacity"));
        }
        else if (command.type == ScriptCommand::Type::Choice)
        {
            drawEditable(selectedCommandIndex_, L"選択肢文", L"prompt", GetCommandParameter(command, L"prompt"));
            cursorY += 8;
            drawLine(L"\u9078\u629e\u80a2GUI", RGB(255, 225, 160));
            for (size_t linkIndex = 0; linkIndex < command.links.size(); ++linkIndex)
            {
                const auto& link = command.links[linkIndex];
                const std::wstring conditionName = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_name_", linkIndex));
                const std::wstring conditionOp = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_op_", linkIndex));
                const std::wstring conditionValue = GetCommandParameter(command, GetChoiceParamKey(L"__choice_cond_value_", linkIndex));
                const bool showDisabled = ParseBoolValue(GetCommandParameter(command, GetChoiceParamKey(L"__choice_show_disabled_", linkIndex)), false);
                drawLine(std::to_wstring(linkIndex + 1) + L". \u679d", linkIndex == selectedChoiceLinkIndex_ ? RGB(255, 225, 160) : RGB(205, 214, 222));
                drawEditable(selectedCommandIndex_, L"\u6587\u8a00", L"__choice_text_" + std::to_wstring(linkIndex), link.first);
                drawEditable(selectedCommandIndex_, L"\u9077\u79fb\u5148", L"__choice_target_" + std::to_wstring(linkIndex), link.second);
                RECT condVarTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
                RECT condVarButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
                SetTextColor(hdc, RGB(205, 214, 222));
                DrawWrappedText(hdc, condVarTextRect, L"条件変数: " + (conditionName.empty() ? L"(未設定)" : conditionName), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                drawActionButton(condVarButtonRect, L"切替", RGB(58, 88, 118));
                inspectorActionTargets_.push_back(InspectorActionTarget{ L"choice_cycle_var", selectedCommandIndex_, linkIndex, condVarButtonRect });
                cursorY += lineHeight;
                drawEditable(selectedCommandIndex_, L"条件変数名", GetChoiceParamKey(L"__choice_cond_name_", linkIndex), conditionName);
                RECT opTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
                RECT opButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
                SetTextColor(hdc, RGB(205, 214, 222));
                DrawWrappedText(hdc, opTextRect, L"条件演算子: " + GetIfOperatorLabel(conditionOp.empty() ? L"eq" : conditionOp), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                drawActionButton(opButtonRect, L"切替", RGB(58, 88, 118));
                inspectorActionTargets_.push_back(InspectorActionTarget{ L"choice_cycle_op", selectedCommandIndex_, linkIndex, opButtonRect });
                cursorY += lineHeight;
                drawEditable(selectedCommandIndex_, L"条件値", GetChoiceParamKey(L"__choice_cond_value_", linkIndex), conditionValue);
                RECT disabledTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
                RECT disabledButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
                SetTextColor(hdc, RGB(205, 214, 222));
                DrawWrappedText(hdc, disabledTextRect, L"条件未達時表示: " + std::wstring(showDisabled ? L"ON" : L"OFF"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                drawActionButton(disabledButtonRect, showDisabled ? L"ON" : L"OFF", showDisabled ? RGB(76, 108, 76) : RGB(90, 62, 62));
                inspectorActionTargets_.push_back(InspectorActionTarget{ L"choice_toggle_disabled", selectedCommandIndex_, linkIndex, disabledButtonRect });
                cursorY += lineHeight;
                RECT removeRect = { panelRect.left + 20, cursorY, panelRect.left + 88, cursorY + 24 };
                drawActionButton(removeRect, L"\u524a\u9664", command.links.size() > 1 ? RGB(132, 58, 66) : RGB(62, 62, 62));
                if (command.links.size() > 1)
                {
                    inspectorActionTargets_.push_back(InspectorActionTarget{ L"choice_remove", selectedCommandIndex_, linkIndex, removeRect });
                }
                cursorY += 30;
            }
            RECT addRect = { panelRect.left + 20, cursorY, panelRect.left + 112, cursorY + 28 };
            drawActionButton(addRect, L"+ \u679d\u3092\u8ffd\u52a0", RGB(76, 96, 76));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"choice_add", selectedCommandIndex_, 0, addRect });
            cursorY += 36;
            drawLine(L"\u30ce\u30fc\u30c9\u56f3\u306e\u30e9\u30d9\u30eb\u3092\u30af\u30ea\u30c3\u30af\u3057\u3066\u3082\u9077\u79fb\u5148\u3092\u5909\u66f4\u3067\u304d\u307e\u3059\u3002", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::IfJump)
        {
            RECT varTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT varButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, varTextRect, L"変数名: " + GetCommandParameter(command, L"name"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(varButtonRect, L"切替", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"if_cycle_var", selectedCommandIndex_, 0, varButtonRect });
            cursorY += lineHeight;
            drawEditable(selectedCommandIndex_, L"\u5909\u6570\u540d", L"name", GetCommandParameter(command, L"name"));
            RECT opTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT opButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, opTextRect, L"\u6f14\u7b97\u5b50: " + GetIfOperatorLabel(GetCommandParameter(command, L"op")), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(opButtonRect, L"\u5207\u66ff", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"if_cycle_op", selectedCommandIndex_, 0, opButtonRect });
            cursorY += lineHeight;
            drawEditable(selectedCommandIndex_, L"\u6bd4\u8f03\u5024", L"value", GetCommandParameter(command, L"value"));
            drawEditable(selectedCommandIndex_, L"\u9077\u79fb\u5148", L"target", GetCommandParameter(command, L"target"));
            cursorY += 8;
            const std::wstring variableName = GetCommandParameter(command, L"name");
            const auto currentValueIt = variables_.find(variableName);
            const std::wstring currentValue = currentValueIt == variables_.end() ? L"(未設定)" : currentValueIt->second;
            drawLine(L"現在値: " + currentValue, RGB(205, 214, 222));
            drawLine(L"現在の評価: " + std::wstring(EvaluateCondition(command) ? L"true" : L"false"), RGB(180, 188, 196));
            drawLine(L"\u5207\u66ff: \u7b49\u3057\u3044 / \u4e0d\u4e00\u81f4 / \u5927\u306a\u308a / \u4ee5\u4e0a / \u5c0f\u306a\u308a / \u4ee5\u4e0b", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::Background)
        {
            drawLine(L"\u753b\u50cf\u8a2d\u5b9a", RGB(255, 225, 160));
            drawEditable(selectedCommandIndex_, L"\u540d\u524d", L"name", GetCommandParameter(command, L"name"));
            drawEditable(selectedCommandIndex_, L"\u30d1\u30b9", L"storage", GetCommandParameter(command, L"storage"));
            RECT browseTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT browseButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, browseTextRect, L"\u753b\u50cf\u53c2\u7167: \u30a8\u30af\u30b9\u30d7\u30ed\u30fc\u30e9\u30fc\u304b\u3089\u9078\u629e", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(browseButtonRect, L"\u53c2\u7167", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"browse_image", selectedCommandIndex_, 0, browseButtonRect });
            cursorY += lineHeight;

            RECT visibleTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT visibleButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            const bool visible = ParseBoolValue(GetCommandParameter(command, L"visible"), true);
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, visibleTextRect, L"\u8868\u793a: " + std::wstring(visible ? L"ON" : L"OFF"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(visibleButtonRect, visible ? L"ON" : L"OFF", visible ? RGB(76, 108, 76) : RGB(90, 62, 62));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"toggle_visible", selectedCommandIndex_, 0, visibleButtonRect });
            cursorY += lineHeight;

            drawEditable(selectedCommandIndex_, L"\u5ea7\u6a19X", L"x", GetCommandParameter(command, L"x"));
            drawEditable(selectedCommandIndex_, L"\u5ea7\u6a19Y", L"y", GetCommandParameter(command, L"y"));
            drawEditable(selectedCommandIndex_, L"\u30b9\u30b1\u30fc\u30eb", L"scale", GetCommandParameter(command, L"scale"));
            drawEditable(selectedCommandIndex_, L"\u900f\u660e\u5ea6", L"opacity", GetCommandParameter(command, L"opacity"));
            drawEditable(selectedCommandIndex_, L"\u80cc\u666f\u8272", L"color", GetCommandParameter(command, L"color"));
            cursorY += 8;
            drawLine(L"\u30d7\u30ec\u30d3\u30e5\u30fc\u3092\u958b\u3044\u3066\u3044\u308c\u3070\u3001\u5909\u66f4\u5f8c\u306b\u3059\u3050\u518d\u751f\u3092\u66f4\u65b0\u3057\u307e\u3059\u3002", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::Character)
        {
            drawLine(L"\u30ad\u30e3\u30e9\u30af\u30bf\u30fc\u767b\u5834", RGB(255, 225, 160));
            drawEditable(selectedCommandIndex_, L"\u30ad\u30e3\u30e9\u30af\u30bf\u30fc", L"name", GetCommandParameter(command, L"name"));
            drawEditable(selectedCommandIndex_, L"\u30ad\u30e3\u30e9\u30af\u30bf\u30fc\u30bf\u30b0", L"pos", GetCommandParameter(command, L"pos"));
            if (!characterDefinitions_.empty())
            {
                RECT defsLabelRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 22 };
                SetTextColor(hdc, RGB(205, 214, 222));
                DrawWrappedText(hdc, defsLabelRect, L"登録キャラクター", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                cursorY += 26;
                int chipX = panelRect.left + 20;
                for (size_t i = 0; i < characterDefinitions_.size(); ++i)
                {
                    RECT chipRect = { chipX, cursorY, chipX + 96, cursorY + 24 };
                    const bool active = GetCommandParameter(command, L"name") == characterDefinitions_[i].id;
                    drawActionButton(chipRect, GetCharacterDefinitionLabel(characterDefinitions_[i]), active ? RGB(72, 128, 188) : RGB(92, 102, 116));
                    inspectorActionTargets_.push_back(InspectorActionTarget{ L"set_character_name", selectedCommandIndex_, i, chipRect });
                    chipX += 102;
                    if (chipX + 96 > panelRect.right - 20)
                    {
                        chipX = panelRect.left + 20;
                        cursorY += 30;
                    }
                }
                cursorY += 34;
            }

            const CharacterDefinition* selectedDefinition = FindCharacterDefinition(GetCommandParameter(command, L"name"));
            if (selectedDefinition && !selectedDefinition->expressions.empty())
            {
                RECT exprLabelRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 22 };
                SetTextColor(hdc, RGB(205, 214, 222));
                DrawWrappedText(hdc, exprLabelRect, L"表情差分", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                cursorY += 26;
                RECT baseRect = { panelRect.left + 20, cursorY, panelRect.left + 88, cursorY + 24 };
                const bool baseActive = GetCommandParameter(command, L"face").empty();
                drawActionButton(baseRect, L"基本", baseActive ? RGB(72, 128, 188) : RGB(92, 102, 116));
                inspectorActionTargets_.push_back(InspectorActionTarget{ L"set_character_base", selectedCommandIndex_, 0, baseRect });
                int exprX = baseRect.right + 8;
                for (size_t expressionIndex = 0; expressionIndex < selectedDefinition->expressions.size(); ++expressionIndex)
                {
                    RECT chipRect = { exprX, cursorY, exprX + 84, cursorY + 24 };
                    const bool active = GetCommandParameter(command, L"face") == selectedDefinition->expressions[expressionIndex].name;
                    drawActionButton(chipRect, selectedDefinition->expressions[expressionIndex].name.empty() ? L"差分" : selectedDefinition->expressions[expressionIndex].name, active ? RGB(160, 112, 72) : RGB(92, 102, 116));
                    inspectorActionTargets_.push_back(InspectorActionTarget{ L"set_character_expression", selectedCommandIndex_, expressionIndex, chipRect });
                    exprX += 90;
                    if (exprX + 84 > panelRect.right - 20)
                    {
                        exprX = panelRect.left + 20;
                        cursorY += 30;
                    }
                }
                cursorY += 34;
            }

            RECT imageLabelRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 22 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, imageLabelRect, L"\u753b\u50cf", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            cursorY += 26;

            RECT previewRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 220 };
            HBRUSH previewBrush = CreateSolidBrush(RGB(230, 228, 222));
            FillRect(hdc, &previewRect, previewBrush);
            DeleteObject(previewBrush);
            FrameRect(hdc, &previewRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            const std::wstring storage = GetCommandParameter(command, L"storage");
            std::unique_ptr<Gdiplus::Image> previewImage;
            if (!storage.empty())
            {
                previewImage = TryLoadImage(CombinePath(scenarioBaseDir_, storage));
                if (!previewImage)
                {
                    previewImage = TryLoadImage(storage);
                }
            }
            if (previewImage)
            {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(previewImage.get(), Gdiplus::Rect(previewRect.left + 4, previewRect.top + 4, (previewRect.right - previewRect.left) - 8, (previewRect.bottom - previewRect.top) - 8));
            }
            else
            {
                RECT noImageRect = { previewRect.left + 12, previewRect.top + 12, previewRect.right - 12, previewRect.bottom - 12 };
                SetTextColor(hdc, RGB(96, 96, 96));
                DrawWrappedText(hdc, noImageRect, L"\u753b\u50cf\u672a\u8a2d\u5b9a", DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            }
            cursorY += 228;

            RECT fileButtonRect = { panelRect.left + 20, cursorY, panelRect.left + 132, cursorY + 28 };
            RECT clearButtonRect = { panelRect.right - 88, cursorY, panelRect.right - 20, cursorY + 28 };
            drawActionButton(fileButtonRect, L"\u30d5\u30a1\u30a4\u30eb\u9078\u629e", RGB(92, 102, 116));
            drawActionButton(clearButtonRect, L"\u89e3\u9664", RGB(74, 98, 128));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"browse_image", selectedCommandIndex_, 0, fileButtonRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"clear_image", selectedCommandIndex_, 0, clearButtonRect });
            cursorY += 38;

            RECT toolLabelRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 22 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, toolLabelRect, L"\u7acb\u3061\u4f4d\u7f6e\u8abf\u6574", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            cursorY += 26;

            RECT toolButtonRect = { panelRect.left + 20, cursorY, panelRect.left + 188, cursorY + 30 };
            drawActionButton(toolButtonRect, characterAdjustMode_ && adjustCharacterCommandIndex_ == selectedCommandIndex_ ? L"\u8abf\u6574\u4e2d" : L"\u9818\u57df\u8abf\u6574\u30c4\u30fc\u30eb\u3092\u958b\u304f", RGB(92, 102, 116));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"character_adjust", selectedCommandIndex_, 0, toolButtonRect });
            cursorY += 40;

            drawEditable(selectedCommandIndex_, L"\u7e26\u4f4d\u7f6e", L"y", GetCommandParameter(command, L"y"));
            drawEditable(selectedCommandIndex_, L"\u6a2a\u4f4d\u7f6e", L"x", GetCommandParameter(command, L"x"));
            drawEditable(selectedCommandIndex_, L"\u30b9\u30b1\u30fc\u30eb", L"scale", GetCommandParameter(command, L"scale"));
            drawEditable(selectedCommandIndex_, L"\u900f\u660e\u5ea6", L"opacity", GetCommandParameter(command, L"opacity"));

            RECT visibleTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT visibleButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            const bool visible = ParseBoolValue(GetCommandParameter(command, L"visible"), true);
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, visibleTextRect, L"\u8868\u793a: " + std::wstring(visible ? L"ON" : L"OFF"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(visibleButtonRect, visible ? L"ON" : L"OFF", visible ? RGB(76, 108, 76) : RGB(90, 62, 62));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"toggle_visible", selectedCommandIndex_, 0, visibleButtonRect });
            cursorY += lineHeight;
            drawLine(L"\u30d7\u30ec\u30d3\u30e5\u30fc\u7a93\u3067\u30c9\u30e9\u30c3\u30b0\u3059\u308b\u3068\u6a2a/\u7e26\u4f4d\u7f6e\u3092\u305d\u306e\u307e\u307e\u8abf\u6574\u3067\u304d\u307e\u3059\u3002", RGB(180, 188, 196));
        }
        else if (command.type == ScriptCommand::Type::Bgm || command.type == ScriptCommand::Type::Se || command.type == ScriptCommand::Type::Voice)
        {
            drawLine(L"\u97f3\u58f0\u8a2d\u5b9a", RGB(255, 225, 160));
            drawEditable(selectedCommandIndex_, L"\u30ab\u30c6\u30b4\u30ea", L"category", GetCommandParameter(command, L"category"));
            drawEditable(selectedCommandIndex_, L"\u30d1\u30b9", L"storage", GetCommandParameter(command, L"storage"));
            RECT browseTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT browseButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, browseTextRect, L"\u97f3\u58f0\u53c2\u7167: \u30a8\u30af\u30b9\u30d7\u30ed\u30fc\u30e9\u30fc\u304b\u3089\u9078\u629e", DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(browseButtonRect, L"\u53c2\u7167", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"browse_audio", selectedCommandIndex_, 0, browseButtonRect });
            cursorY += lineHeight;

            drawEditable(selectedCommandIndex_, L"\u97f3\u91cf", L"volume", GetCommandParameter(command, L"volume"));
            RECT loopTextRect = { panelRect.left + 20, cursorY, panelRect.right - 126, cursorY + lineHeight };
            RECT loopButtonRect = { panelRect.right - 116, cursorY, panelRect.right - 20, cursorY + lineHeight - 4 };
            const bool loop = ParseBoolValue(GetCommandParameter(command, L"loop"), command.type == ScriptCommand::Type::Bgm);
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, loopTextRect, L"\u30eb\u30fc\u30d7: " + std::wstring(loop ? L"ON" : L"OFF"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(loopButtonRect, loop ? L"ON" : L"OFF", loop ? RGB(76, 108, 76) : RGB(90, 62, 62));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"toggle_loop", selectedCommandIndex_, 0, loopButtonRect });
            cursorY += lineHeight;

            drawEditable(selectedCommandIndex_, L"\u30d5\u30a7\u30fc\u30c9\u30a4\u30f3", L"fadein", GetCommandParameter(command, L"fadein"));
            drawEditable(selectedCommandIndex_, L"\u30d5\u30a7\u30fc\u30c9\u30a2\u30a6\u30c8", L"fadeout", GetCommandParameter(command, L"fadeout"));
            RECT playRect = { panelRect.left + 20, cursorY, panelRect.left + 92, cursorY + 28 };
            RECT stopRect = { panelRect.left + 100, cursorY, panelRect.left + 172, cursorY + 28 };
            drawActionButton(playRect, L"\u518d\u751f", RGB(62, 110, 78));
            drawActionButton(stopRect, L"\u505c\u6b62", RGB(112, 62, 70));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"audio_play", selectedCommandIndex_, 0, playRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"audio_stop", selectedCommandIndex_, 0, stopRect });
            cursorY += 36;
            cursorY += 8;
            drawLine(L"\u97f3\u91cf 0-100 / \u30d5\u30a7\u30fc\u30c9\u306f\u30df\u30ea\u79d2\u5358\u4f4d\u3067\u5165\u529b\u3057\u307e\u3059\u3002", RGB(180, 188, 196));
        }
        else if (command.parameters.empty())
        {
            drawLine(L"(\u306a\u3057)", RGB(180, 188, 196));
        }
        else
        {
            for (const auto& parameter : command.parameters)
            {
                drawEditable(selectedCommandIndex_, parameter.first, parameter.first, parameter.second);
            }
        }
        if ((command.type == ScriptCommand::Type::Jump || command.type == ScriptCommand::Type::IfJump) && command.type != ScriptCommand::Type::Choice)
        {
            cursorY += 8;
            drawLine(L"\u30d5\u30ed\u30fc\u30b0\u30e9\u30d5\u4e0a\u306e\u30e9\u30d9\u30eb\u3092\u30af\u30ea\u30c3\u30af\u3059\u308b\u3068\u63a5\u7d9a\u5148\u3092\u5909\u66f4\u3067\u304d\u307e\u3059\u3002", RGB(180, 188, 196));
        }
    }

    if (inspectorEditing_)
    {
        cursorY += 12;
        drawLine(L"\u7de8\u96c6\u4e2d", RGB(255, 225, 160));

        RECT inputRect = { panelRect.left + 20, cursorY, panelRect.right - 20, cursorY + 58 };
        HBRUSH inputBrush = CreateSolidBrush(RGB(22, 32, 44));
        FillRect(hdc, &inputRect, inputBrush);
        DeleteObject(inputBrush);
        FrameRect(hdc, &inputRect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetTextColor(hdc, RGB(236, 242, 246));
        RECT inputTextRect = { inputRect.left + 10, inputRect.top + 8, inputRect.right - 10, inputRect.bottom - 8 };
        DrawWrappedText(hdc, inputTextRect, editingLabel_ + L": " + editingBuffer_, DT_LEFT | DT_WORDBREAK);
        cursorY += 66;

        inspectorCommitRect_ = { panelRect.left + 20, cursorY, panelRect.left + 100, cursorY + 28 };
        inspectorCancelRect_ = { panelRect.left + 108, cursorY, panelRect.left + 188, cursorY + 28 };
        HBRUSH commitBrush = CreateSolidBrush(RGB(64, 112, 72));
        FillRect(hdc, &inspectorCommitRect_, commitBrush);
        DeleteObject(commitBrush);
        HBRUSH cancelBrush = CreateSolidBrush(RGB(122, 64, 70));
        FillRect(hdc, &inspectorCancelRect_, cancelBrush);
        DeleteObject(cancelBrush);
        FrameRect(hdc, &inspectorCommitRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        FrameRect(hdc, &inspectorCancelRect_, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        DrawWrappedText(hdc, inspectorCommitRect_, L"\u4fdd\u5b58", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DrawWrappedText(hdc, inspectorCancelRect_, L"\u30ad\u30e3\u30f3\u30bb\u30eb", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursorY += 36;
        drawLine(L"Enter \u3067\u4fdd\u5b58 / Esc \u3067\u30ad\u30e3\u30f3\u30bb\u30eb", RGB(180, 188, 196));
    }

    cursorY += 12;
    drawLine(L"\u5909\u6570", RGB(255, 225, 160));
    if (variables_.empty())
    {
        drawLine(L"(\u7a7a)", RGB(180, 188, 196));
    }
    else
    {
        for (const auto& variable : variables_)
        {
            RECT valueRect = { panelRect.left + 20, cursorY, panelRect.right - 136, cursorY + lineHeight };
            RECT editRect = { panelRect.right - 126, cursorY, panelRect.right - 74, cursorY + lineHeight - 4 };
            RECT minusRect = { panelRect.right - 68, cursorY, panelRect.right - 42, cursorY + lineHeight - 4 };
            RECT plusRect = { panelRect.right - 38, cursorY, panelRect.right - 12, cursorY + lineHeight - 4 };
            SetTextColor(hdc, RGB(205, 214, 222));
            DrawWrappedText(hdc, valueRect, variable.first + L" = " + variable.second, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
            drawActionButton(editRect, L"編集", RGB(58, 88, 118));
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"var_edit:" + variable.first, 0, 0, editRect });
            HBRUSH spinBrush = CreateSolidBrush(RGB(224, 228, 232));
            FillRect(hdc, &minusRect, spinBrush);
            FillRect(hdc, &plusRect, spinBrush);
            DeleteObject(spinBrush);
            FrameRect(hdc, &minusRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            FrameRect(hdc, &plusRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            SetTextColor(hdc, RGB(40, 56, 72));
            DrawWrappedText(hdc, minusRect, L"-", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            DrawWrappedText(hdc, plusRect, L"+", DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"var_nudge:-:" + variable.first, 0, 0, minusRect });
            inspectorActionTargets_.push_back(InspectorActionTarget{ L"var_nudge:+:" + variable.first, 0, 0, plusRect });
            cursorY += lineHeight;
        }
    }

    cursorY += 12;
    drawLine(L"変数履歴", RGB(255, 225, 160));
    if (variableHistory_.empty())
    {
        drawLine(L"(まだありません)", RGB(180, 188, 196));
    }
    else
    {
        const size_t start = variableHistory_.size() > 8 ? variableHistory_.size() - 8 : 0;
        for (size_t i = start; i < variableHistory_.size(); ++i)
        {
            drawLine(variableHistory_[i], RGB(180, 188, 196));
        }
    }

    const int visibleHeight = static_cast<int>(panelRect.bottom - (panelRect.top + 72));
    const int contentBottom = cursorY + inspectorScrollOffset_;
    const int contentHeight = static_cast<int>((contentBottom - (panelRect.top + 72)) - visibleHeight + 12);
    inspectorScrollMax_ = (std::max)(0, contentHeight);
    inspectorScrollOffset_ = (std::max)(0, (std::min)(inspectorScrollOffset_, inspectorScrollMax_));
    RECT scrollContentRect = { panelRect.left, panelRect.top + 72, panelRect.right, panelRect.bottom - 10 };
    DrawVerticalScrollbar(hdc, scrollContentRect, inspectorScrollOffset_, inspectorScrollMax_, RGB(24, 30, 38), RGB(106, 120, 136));

    SelectObject(hdc, oldFont);
    DeleteObject(headerFont);
    DeleteObject(bodyFont);
}

bool NovelRuntime::TrySelectCommandFromPoint(POINT point, const RECT& clientRect)
{
    UNREFERENCED_PARAMETER(clientRect);
    for (size_t i = 0; i < commandRowRects_.size(); ++i)
    {
        if (PtInRect(&commandRowRects_[i], point))
        {
            selectedCommandIndex_ = commandRowIndices_[i];
            return true;
        }
    }

    for (size_t i = 0; i < graphNodeRects_.size(); ++i)
    {
        if (PtInRect(&graphNodeRects_[i], point))
        {
            return HandleGraphNodeSelection(graphNodeIndices_[i]);
        }
    }

    for (size_t i = 0; i < eventRowRects_.size(); ++i)
    {
        if (PtInRect(&eventRowRects_[i], point))
        {
            selectedCommandIndex_ = eventRowIndices_[i];
            return true;
        }
    }

    return false;
}
