#include "StreamingAudioPlayer.h"
#include "miniaudio.h"

#include <algorithm>

struct StreamingAudioPlayer::Impl
{
    ma_engine engine = {};
    ma_sound sound = {};
    bool engineReady = false;
    bool soundReady = false;
};

StreamingAudioPlayer::StreamingAudioPlayer() : impl_(std::make_unique<Impl>())
{
}

StreamingAudioPlayer::~StreamingAudioPlayer()
{
    Shutdown();
}

bool StreamingAudioPlayer::Initialize()
{
    if (impl_->engineReady)
    {
        return true;
    }
    impl_->engineReady = ma_engine_init(nullptr, &impl_->engine) == MA_SUCCESS;
    return impl_->engineReady;
}

bool StreamingAudioPlayer::Play(const std::wstring& path, bool loop, int volumePercent)
{
    Stop();
    if (!Initialize() || path.empty())
    {
        return false;
    }
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file_w(&impl_->engine, path.c_str(), flags, nullptr, nullptr, &impl_->sound) != MA_SUCCESS)
    {
        return false;
    }
    impl_->soundReady = true;
    ma_sound_set_looping(&impl_->sound, loop ? MA_TRUE : MA_FALSE);
    SetVolume(volumePercent);
    if (ma_sound_start(&impl_->sound) != MA_SUCCESS)
    {
        Stop();
        return false;
    }
    return true;
}

void StreamingAudioPlayer::SetVolume(int volumePercent)
{
    if (impl_->soundReady)
    {
        const float volume = static_cast<float>((std::max)(0, (std::min)(100, volumePercent))) / 100.0f;
        ma_sound_set_volume(&impl_->sound, volume);
    }
}

void StreamingAudioPlayer::Stop()
{
    if (impl_->soundReady)
    {
        ma_sound_stop(&impl_->sound);
        ma_sound_uninit(&impl_->sound);
        impl_->sound = {};
        impl_->soundReady = false;
    }
}

void StreamingAudioPlayer::Shutdown()
{
    Stop();
    if (impl_ && impl_->engineReady)
    {
        ma_engine_uninit(&impl_->engine);
        impl_->engine = {};
        impl_->engineReady = false;
    }
}
