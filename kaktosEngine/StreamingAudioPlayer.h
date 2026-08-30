#pragma once

#include <memory>
#include <string>

class StreamingAudioPlayer final
{
public:
    StreamingAudioPlayer();
    ~StreamingAudioPlayer();

    StreamingAudioPlayer(const StreamingAudioPlayer&) = delete;
    StreamingAudioPlayer& operator=(const StreamingAudioPlayer&) = delete;

    bool Initialize();
    bool Play(const std::wstring& path, bool loop, int volumePercent);
    void SetVolume(int volumePercent);
    void Stop();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
