/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "job_system.h"
#include "logging/logger.h"

namespace Framework::Utils {
    JobSystem* JobSystem::GetInstance() {
        static JobSystem instance;
        return &instance;
    }

    JobSystem::~JobSystem() {
        Shutdown();
    }

    bool JobSystem::Init(uint32_t numThreads) {
        if (_initialized) {
            return false;
        }

        _pendingShutdown = false;
        _threads.reserve(numThreads);

        for (size_t i = 0; i < numThreads; i++) {
            _threads.emplace_back([this, i]() {
                WorkerThread(i);
            });
        }

        _initialized = true;
        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job system initialized with {} threads", numThreads);
        return true;
    }

    bool JobSystem::Shutdown(std::chrono::milliseconds timeout) {
        if (!_initialized) {
            return false;
        }

        _pendingShutdown = true;
        _jobAvailable.notify_all();

        const auto deadline = Clock::now() + timeout;
        bool allThreadsJoined = true;

        for (auto& thread : _threads) {
            if (thread.joinable()) {
                if (Clock::now() < deadline) {
                    thread.join();
                } else {
                    allThreadsJoined = false;
                    Logging::GetLogger(FRAMEWORK_INNER_JOBS)->error("Thread join timeout");
                }
            }
        }

        if (allThreadsJoined) {
            _threads.clear();
            _initialized = false;
            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job system shut down successfully");
        }

        return allThreadsJoined;
    }

    void JobSystem::EnqueueJob(const JobProc& job, JobPriority priority, bool repeatOnFail) {
        if (_pendingShutdown) {
            return;
        }

        const std::lock_guard<std::mutex> lock(_mutex);
        const auto priorityIndex = static_cast<size_t>(priority);
        
        _jobs[priorityIndex].push(Job{
            job,
            priority,
            JobStatus::Pending,
            repeatOnFail,
            Clock::now()
        });

        _stats.totalJobs++;
        _stats.jobsByPriority[priorityIndex]++;
        _jobAvailable.notify_one();
        
        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job with priority {} was enqueued", priorityIndex);
    }

    bool JobSystem::IsQueueEmpty(JobPriority priority) const {
        const std::lock_guard<std::mutex> lock(_mutex);
        return _jobs[static_cast<size_t>(priority)].empty();
    }

    bool JobSystem::IsPendingShutdown() const {
        return _pendingShutdown;
    }

    JobSystem::JobStats JobSystem::GetStats() const {
        JobStats stats;
        stats.totalJobs = _stats.totalJobs.load(std::memory_order_relaxed);
        stats.failedJobs = _stats.failedJobs.load(std::memory_order_relaxed);
        stats.retriedJobs = _stats.retriedJobs.load(std::memory_order_relaxed);
        
        for (size_t i = 0; i < static_cast<size_t>(JobPriority::NumJobPriorities); ++i) {
            stats.jobsByPriority[i] = _stats.jobsByPriority[i].load(std::memory_order_relaxed);
        }
        return stats;
    }

    void JobSystem::WorkerThread(size_t threadId) {
        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job worker thread {} started", threadId);

        while (!_pendingShutdown) {
            Job jobInfo{};
            bool hasJob = false;

            {
                std::unique_lock<std::mutex> lock(_mutex);
                
                _jobAvailable.wait(lock, [this]() {
                    return _pendingShutdown || HasAvailableJobs();
                });

                if (_pendingShutdown) {
                    break;
                }

                hasJob = TryGetNextJob(jobInfo);
            }

            if (hasJob) {
                ProcessJob(jobInfo);
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job worker thread {} terminated", threadId);
    }

    bool JobSystem::HasAvailableJobs() const {
        return std::any_of(_jobs.begin(), _jobs.end(), [](const auto& queue) { return !queue.empty(); });
    }

    bool JobSystem::TryGetNextJob(Job& outJob) {
        const auto now = Clock::now();
        bool last_empty = false;

        for (size_t id = 0; id < _jobs.size(); ++id) {
            auto& queue = _jobs[id];
            
            if (queue.empty()) {
                last_empty = (id == _jobs.size() - 1);
                continue;
            }

            if (!last_empty && ((_counter++ % GetJobPriorityFromIndex(id)) != 0)) {
                continue;
            }

            outJob = queue.front();
            queue.pop();

            // Log long wait times
            auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - outJob.enqueueTime).count();
            if (waitTime > 1000) {
                Logging::GetLogger(FRAMEWORK_INNER_JOBS)->warn("Job with priority {} waited {}ms in queue", id, waitTime);
            }

            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job with priority {} is now being processed", id);
            return true;
        }

        return false;
    }

    void JobSystem::ProcessJob(Job& job) {
        job.status = JobStatus::Running;
        
        const auto startTime = Clock::now();
        bool success = job.proc();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startTime).count();

        if (!success) {
            _stats.failedJobs++;
            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->warn("Job failed after {}ms", duration);

            if (job.repeatOnFail) {
                _stats.retriedJobs++;
                Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job is enqueued for another attempt");
                
                job.status = JobStatus::Pending;
                const std::lock_guard<std::mutex> lock(_mutex);
                _jobs[static_cast<size_t>(job.priority)].push(job);
            }
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job completed successfully in {}ms", duration);
        }
    }

    constexpr uint8_t JobSystem::GetJobPriorityFromIndex(size_t index) {
        constexpr uint8_t priorities[] = {2, 3, 5, 7, 11};
        return priorities[index];
    }

} // namespace Framework::Utils