#include "AsyncTextWriter.h"
#include "Scenario.h"

AsyncTextWriter::AsyncTextWriter() : worker_(&AsyncTextWriter::Run, this)
{
}

AsyncTextWriter::~AsyncTextWriter()
{
    Flush();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void AsyncTextWriter::Submit(std::wstring path, std::wstring content)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingPath_ = std::move(path);
        pendingContent_ = std::move(content);
        hasPending_ = true;
    }
    wake_.notify_one();
}

void AsyncTextWriter::Flush()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return !hasPending_ && !writing_; });
}

void AsyncTextWriter::Run()
{
    for (;;)
    {
        std::wstring path;
        std::wstring content;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || hasPending_; });
            if (stopping_ && !hasPending_)
            {
                return;
            }
            path = std::move(pendingPath_);
            content = std::move(pendingContent_);
            hasPending_ = false;
            writing_ = true;
        }

        TryWriteTextFile(path, content);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            writing_ = false;
            if (!hasPending_)
            {
                idle_.notify_all();
            }
        }
    }
}
