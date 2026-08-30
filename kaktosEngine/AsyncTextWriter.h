#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class AsyncTextWriter final
{
public:
    AsyncTextWriter();
    ~AsyncTextWriter();

    AsyncTextWriter(const AsyncTextWriter&) = delete;
    AsyncTextWriter& operator=(const AsyncTextWriter&) = delete;

    void Submit(std::wstring path, std::wstring content);
    void Flush();

private:
    void Run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::thread worker_;
    std::wstring pendingPath_;
    std::wstring pendingContent_;
    bool hasPending_ = false;
    bool writing_ = false;
    bool stopping_ = false;
};
