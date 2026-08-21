#pragma once

#include "replay/ReplayStorage.hpp"
#include "replay/ReplayTypes.hpp"

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace lg::replay {

class ReplayIoService {
public:
    using JobId = std::uint64_t;

    enum class JobKind {
        Save,
        Load,
        Decode,
        Encode,
        List,
        Delete,
    };

    struct Config {
        // This count includes the job that the worker is running.
        std::size_t maxPendingJobs = 2;
    };

    struct Result {
        JobId id = 0;
        JobKind kind = JobKind::Load;
        bool ok = false;
        std::filesystem::path path;
        std::vector<ReplayFileInfo> files;
        std::optional<ReplayDemo> demo;
        std::vector<std::uint8_t> bytes;
        std::string error;
    };

    ReplayIoService();
    explicit ReplayIoService(Config config);
    ~ReplayIoService();

    ReplayIoService(const ReplayIoService&) = delete;
    ReplayIoService& operator=(const ReplayIoService&) = delete;

    // Lvalue payloads are moved only after admission succeeds. Queue-full or
    // stopped-service rejection therefore leaves the caller's data intact.
    [[nodiscard]] bool enqueueSave(const std::filesystem::path& path,
                                   ReplayDemo& demo,
                                   JobId& id,
                                   std::string* error = nullptr);
    [[nodiscard]] bool enqueueSave(const std::filesystem::path& path,
                                   ReplayDemo&& demo,
                                   JobId& id,
                                   std::string* error = nullptr) {
        return enqueueSave(path, demo, id, error);
    }
    [[nodiscard]] bool enqueueLoad(const std::filesystem::path& path,
                                   JobId& id,
                                   std::string* error = nullptr);
    [[nodiscard]] bool enqueueDecode(std::vector<std::uint8_t>& bytes,
                                     JobId& id,
                                     std::string* error = nullptr);
    [[nodiscard]] bool enqueueDecode(std::vector<std::uint8_t>&& bytes,
                                     JobId& id,
                                     std::string* error = nullptr) {
        return enqueueDecode(bytes, id, error);
    }
    [[nodiscard]] bool enqueueEncode(ReplayDemo& demo,
                                     std::size_t maximumBytes,
                                     JobId& id,
                                     std::string* error = nullptr);
    [[nodiscard]] bool enqueueEncode(ReplayDemo&& demo,
                                     std::size_t maximumBytes,
                                     JobId& id,
                                     std::string* error = nullptr) {
        return enqueueEncode(demo, maximumBytes, id, error);
    }
    [[nodiscard]] bool enqueueList(const std::filesystem::path& directory,
                                   JobId& id,
                                   std::string* error = nullptr);
    [[nodiscard]] bool enqueueDelete(const std::filesystem::path& path,
                                     JobId& id,
                                     std::string* error = nullptr);

    // These aliases keep the call site clear when a service is shared by more
    // than one replay command surface.
    [[nodiscard]] bool saveDemo(const std::filesystem::path& path,
                                ReplayDemo demo,
                                JobId& id,
                                std::string* error = nullptr) {
        return enqueueSave(path, std::move(demo), id, error);
    }
    [[nodiscard]] bool loadDemo(const std::filesystem::path& path,
                                JobId& id,
                                std::string* error = nullptr) {
        return enqueueLoad(path, id, error);
    }

    [[nodiscard]] std::optional<Result> poll();
    [[nodiscard]] std::size_t pendingJobs() const;
    [[nodiscard]] bool hasPendingJobs() const { return pendingJobs() != 0; }

    // Stop accepts queued work, lets it finish, and then joins the worker.
    void shutdown();

private:
    struct SaveJob {
        std::filesystem::path path;
        ReplayDemo demo;
    };
    struct LoadJob {
        std::filesystem::path path;
    };
    struct DecodeJob {
        std::vector<std::uint8_t> bytes;
    };
    struct EncodeJob {
        ReplayDemo demo;
        std::size_t maximumBytes = 0;
    };
    struct ListJob {
        std::filesystem::path directory;
    };
    struct DeleteJob {
        std::filesystem::path path;
    };
    using JobPayload = std::variant<SaveJob, LoadJob, DecodeJob, EncodeJob, ListJob, DeleteJob>;

    struct Job {
        JobId id = 0;
        JobKind kind = JobKind::Load;
        JobPayload payload;
    };

    [[nodiscard]] bool enqueue(JobKind kind,
                               JobPayload payload,
                               JobId& id,
                               std::string* error);
    void workerLoop();
    [[nodiscard]] Result runJob(Job job) const;

    Config config_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    std::size_t runningJobs_ = 0;
    JobId nextId_ = 1;
    bool stopping_ = false;
    bool stopped_ = false;
    std::thread worker_;
};

} // namespace lg::replay
