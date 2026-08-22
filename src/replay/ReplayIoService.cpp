#include "replay/ReplayIoService.hpp"

#include "replay/ReplayCodec.hpp"
#include "replay/ReplayFile.hpp"

#include <exception>

namespace lg::replay {

namespace {

void setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

} // namespace

ReplayIoService::ReplayIoService()
    : ReplayIoService(Config{}) {}

ReplayIoService::ReplayIoService(Config config)
    : config_(config) {
    if (config_.maxPendingJobs == 0) {
        config_.maxPendingJobs = 1;
    }
    if (config_.startWorker) {
        start();
    }
}

ReplayIoService::~ReplayIoService() {
    shutdown();
}

void ReplayIoService::start() {
    std::lock_guard lock(mutex_);
    if (workerStarted_ || stopping_ || stopped_) {
        return;
    }
    worker_ = std::thread(&ReplayIoService::workerLoop, this);
    workerStarted_ = true;
}

bool ReplayIoService::enqueueSave(const std::filesystem::path& path,
                                  ReplayDemo& demo,
                                  JobId& id,
                                  std::string* error) {
    std::lock_guard lock(mutex_);
    if (stopping_ || stopped_) {
        setError(error, "replay I/O service is stopped");
        return false;
    }
    if (jobs_.size() + runningJobs_ >= config_.maxPendingJobs) {
        setError(error, "replay I/O queue is full");
        return false;
    }
    id = nextId_++;
    jobs_.push_back(Job{id, JobKind::Save, SaveJob{path, std::move(demo)}});
    condition_.notify_one();
    return true;
}

bool ReplayIoService::enqueueLoad(const std::filesystem::path& path,
                                  JobId& id,
                                  std::string* error) {
    return enqueue(JobKind::Load, LoadJob{path}, id, error);
}

bool ReplayIoService::enqueueDecode(
                                    std::vector<std::uint8_t>& bytes,
                                    JobId& id,
                                    std::string* error,
                                    std::size_t maximumResidentBytes,
                                    std::size_t maximumTicks) {
    if (bytes.size() > kMaxReplayBytes) {
        setError(error, "replay bytes exceed the size limit");
        return false;
    }
    if (maximumResidentBytes == 0U ||
        maximumResidentBytes > kMaxReplayDecodedResidentBytes ||
        maximumTicks == 0U || maximumTicks > kMaxReplayTicks) {
        setError(error, "replay decode limits are invalid");
        return false;
    }
    std::lock_guard lock(mutex_);
    if (stopping_ || stopped_) {
        setError(error, "replay I/O service is stopped");
        return false;
    }
    if (jobs_.size() + runningJobs_ >= config_.maxPendingJobs) {
        setError(error, "replay I/O queue is full");
        return false;
    }
    id = nextId_++;
    jobs_.push_back(Job{
        id,
        JobKind::Decode,
        DecodeJob{std::move(bytes), maximumResidentBytes, maximumTicks}
    });
    condition_.notify_one();
    return true;
}

bool ReplayIoService::enqueueEncode(ReplayDemo& demo,
                                    std::size_t maximumBytes,
                                    JobId& id,
                                    std::string* error) {
    if (maximumBytes == 0U) {
        setError(error, "replay encode size limit is zero");
        return false;
    }
    std::lock_guard lock(mutex_);
    if (stopping_ || stopped_) {
        setError(error, "replay I/O service is stopped");
        return false;
    }
    if (jobs_.size() + runningJobs_ >= config_.maxPendingJobs) {
        setError(error, "replay I/O queue is full");
        return false;
    }
    id = nextId_++;
    jobs_.push_back(Job{id, JobKind::Encode,
                        EncodeJob{std::move(demo), maximumBytes}});
    condition_.notify_one();
    return true;
}

bool ReplayIoService::enqueueList(const std::filesystem::path& directory,
                                  JobId& id,
                                  std::string* error) {
    return enqueue(JobKind::List, ListJob{directory}, id, error);
}

bool ReplayIoService::enqueueDelete(const std::filesystem::path& path,
                                    JobId& id,
                                    std::string* error) {
    return enqueue(JobKind::Delete, DeleteJob{path}, id, error);
}

bool ReplayIoService::enqueue(JobKind kind,
                              JobPayload payload,
                              JobId& id,
                              std::string* error) {
    std::lock_guard lock(mutex_);
    if (stopping_ || stopped_) {
        setError(error, "replay I/O service is stopped");
        return false;
    }
    if (jobs_.size() + runningJobs_ >= config_.maxPendingJobs) {
        setError(error, "replay I/O queue is full");
        return false;
    }
    id = nextId_++;
    jobs_.push_back(Job{id, kind, std::move(payload)});
    condition_.notify_one();
    return true;
}

std::optional<ReplayIoService::Result> ReplayIoService::poll() {
    std::lock_guard lock(mutex_);
    if (results_.empty()) {
        return std::nullopt;
    }
    Result result = std::move(results_.front());
    results_.pop_front();
    return result;
}

std::size_t ReplayIoService::pendingJobs() const {
    std::lock_guard lock(mutex_);
    return jobs_.size() + runningJobs_;
}

void ReplayIoService::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        if (!workerStarted_ && !jobs_.empty()) {
            worker_ = std::thread(&ReplayIoService::workerLoop, this);
            workerStarted_ = true;
        }
        stopping_ = true;
    }
    condition_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard lock(mutex_);
    stopped_ = true;
}

void ReplayIoService::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++runningJobs_;
        }

        Result result;
        try {
            result = runJob(std::move(job));
        } catch (const std::exception& exception) {
            result.id = job.id;
            result.kind = job.kind;
            result.path = std::visit([](const auto& payload) -> std::filesystem::path {
                if constexpr (requires { payload.path; }) {
                    return payload.path;
                }
                return {};
            }, job.payload);
            result.error = exception.what();
        } catch (...) {
            result.id = job.id;
            result.kind = job.kind;
            result.error = "replay I/O job failed";
        }

        {
            std::lock_guard lock(mutex_);
            --runningJobs_;
            results_.push_back(std::move(result));
        }
    }
}

ReplayIoService::Result ReplayIoService::runJob(Job job) const {
    Result result;
    result.id = job.id;
    result.kind = job.kind;
    std::visit([&result](auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, SaveJob>) {
            result.path = payload.path;
            result.ok = saveDemoFile(payload.path, payload.demo, &result.error);
        } else if constexpr (std::is_same_v<Payload, LoadJob>) {
            result.path = payload.path;
            ReplayDemo demo;
            result.ok = loadDemoFile(payload.path, demo, &result.error);
            if (result.ok) {
                result.demo = std::move(demo);
            }
        } else if constexpr (std::is_same_v<Payload, DecodeJob>) {
            ReplayDemo demo;
            result.ok = decodeDemo(
                payload.bytes,
                demo,
                &result.error,
                payload.maximumResidentBytes,
                payload.maximumTicks
            );
            if (result.ok) {
                result.demo = std::move(demo);
            }
        } else if constexpr (std::is_same_v<Payload, EncodeJob>) {
            std::vector<std::uint8_t> bytes;
            result.ok = encodeDemo(payload.demo, bytes, &result.error);
            if (result.ok && bytes.size() > payload.maximumBytes) {
                result.ok = false;
                result.error = "encoded replay exceeds the requested size limit";
            }
            if (result.ok) {
                result.bytes = std::move(bytes);
            }
        } else if constexpr (std::is_same_v<Payload, ListJob>) {
            result.path = payload.directory;
            ReplayStorage storage(payload.directory);
            result.files = storage.list(&result.error);
            result.ok = result.error.empty();
        } else if constexpr (std::is_same_v<Payload, DeleteJob>) {
            result.path = payload.path;
            std::error_code error;
            if (!std::filesystem::exists(payload.path, error) || error) {
                result.error = "demo does not exist";
                result.ok = false;
            } else if (!std::filesystem::is_regular_file(payload.path, error) || error ||
                       !std::filesystem::remove(payload.path, error) || error) {
                result.error = "demo could not be deleted";
                result.ok = false;
            } else {
                result.ok = true;
            }
        }
    }, job.payload);
    return result;
}

} // namespace lg::replay
