/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <function2.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>
#include <future>

namespace Framework::Utils {
    class JobSystem final {
    public:
        using JobProc = fu2::function<bool() const>;
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        enum class JobPriority : size_t {
            RealTime,
            High,
            Normal,
            Low,
            Idle,
            NumJobPriorities
        };

        struct JobStats {
            uint64_t totalJobs{0};
            uint64_t failedJobs{0};
            uint64_t retriedJobs{0};
            std::array<uint64_t, static_cast<size_t>(JobPriority::NumJobPriorities)> jobsByPriority{};
        };

        ~JobSystem();
        
        static JobSystem* GetInstance();
        bool Init(uint32_t numThreads = std::thread::hardware_concurrency());
        bool Shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5));

        template<typename F>
        [[nodiscard]] auto EnqueueJobWithFuture(F&& job, JobPriority priority = JobPriority::Normal) {
            using ReturnType = std::invoke_result_t<F>;
            auto promise = std::make_shared<std::promise<ReturnType>>();
            auto future = promise->get_future();

            auto wrappedJob = [job = std::forward<F>(job), promise]() mutable {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        job();
                        promise->set_value();
                    } else {
                        promise->set_value(job());
                    }
                    return true;
                } catch (...) {
                    promise->set_exception(std::current_exception());
                    return false;
                }
            };

            EnqueueJob(wrappedJob, priority);
            return future;
        }

        void EnqueueJob(const JobProc& job, JobPriority priority = JobPriority::Normal, bool repeatOnFail = false);
        bool IsQueueEmpty(JobPriority priority) const;
        bool IsPendingShutdown() const;
        JobStats GetStats() const;

    private:
        JobSystem() = default;
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        enum class JobStatus : int32_t {
            Invalid = -1,
            Pending = 0,
            Running = 1
        };

        struct Job {
            JobProc proc;
            JobPriority priority;
            JobStatus status;
            bool repeatOnFail;
            TimePoint enqueueTime;
        };

        void WorkerThread(size_t threadId);
        bool HasAvailableJobs() const;
        bool TryGetNextJob(Job& outJob);
        void ProcessJob(Job& job);
        static constexpr uint8_t GetJobPriorityFromIndex(size_t index);

        using JobQueue = std::queue<Job>;
        std::array<JobQueue, static_cast<size_t>(JobPriority::NumJobPriorities)> _jobs;
        std::vector<std::thread> _threads;
        mutable std::mutex _mutex;
        std::condition_variable _jobAvailable;
        std::atomic_bool _pendingShutdown{false};
        std::atomic_bool _initialized{false};
        std::atomic_uint64_t _counter{0};
        struct AtomicStats {
            std::atomic_uint64_t totalJobs{0};
            std::atomic_uint64_t failedJobs{0};
            std::atomic_uint64_t retriedJobs{0};
            std::array<std::atomic_uint64_t, static_cast<size_t>(JobPriority::NumJobPriorities)> jobsByPriority{};
        };
        AtomicStats _stats;
    };
} // namespace Framework::Utils