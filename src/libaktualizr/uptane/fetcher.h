#ifndef UPTANE_FETCHER_H_
#define UPTANE_FETCHER_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include "config/config.h"
#include "http/httpinterface.h"
#include "storage/invstorage.h"

namespace Uptane {

constexpr int64_t kMaxRootSize = 64 * 1024;
constexpr int64_t kMaxDirectorTargetsSize = 64 * 1024;
constexpr int64_t kMaxTimestampSize = 64 * 1024;
constexpr int64_t kMaxSnapshotSize = 64 * 1024;
constexpr int64_t kMaxImagesTargetsSize = 1024 * 1024;

using FetcherProgressCb = std::function<void(const Uptane::Target&, const std::string&, unsigned int)>;

///
/// Provides a thread-safe way to pause and terminate task execution.
/// A task must call canContinue() method to check the current state.
///
class FlowControlToken {
 public:
  ///
  /// Called by the controlling thread to request the task to pause or resume.
  /// Has no effect if the task was aborted.
  /// @return `true` if the state was changed, `false` otherwise.
  ///
  bool setPause(bool set_paused) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (set_paused && state_ == State::Running) {
        state_ = State::Paused;
      } else if (!set_paused && state_ == State::Paused) {
        state_ = State::Running;
      } else {
        return false;
      }
    }
    cv_.notify_all();
    return true;
  }

  ///
  /// Called by the controlling thread to request the task to abort.
  /// @return `false` if the task was already aborted, `true` otherwise.
  ///
  bool setAbort() {
    {
      std::lock_guard<std::mutex> g(mutex_);
      if (state_ == State::Aborted) {
        return false;
      }
      state_ = State::Aborted;
    }
    cv_.notify_all();
    return true;
  }

  ///
  /// Called by the controlled thread to query the currently requested state.
  /// Sleeps if the state is `Paused` and `blocking == true`.
  /// @return `true` for `Running` state, `false` for `Aborted`,
  /// and also `false` for the `Paused` state, if the call is non-blocking.
  ///
  bool canContinue(bool blocking = true) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (blocking) {
      cv_.wait(lk, [this] { return state_ != State::Paused; });
    }
    return state_ == State::Running;
  }

 private:
  enum class State {
    Running,  // transitions: ->Paused, ->Aborted
    Paused,   // transitions: ->Running, ->Aborted
    Aborted   // transitions: none
  } state_{State::Running};
  std::mutex mutex_;
  std::condition_variable cv_;
};

class Fetcher {
 public:
  Fetcher(const Config& config_in, std::shared_ptr<INvStorage> storage_in, std::shared_ptr<HttpInterface> http_in,
          FetcherProgressCb progress_cb_in = nullptr)
      : http(std::move(http_in)),
        storage(std::move(storage_in)),
        config(config_in),
        progress_cb(std::move(progress_cb_in)) {}
  bool fetchVerifyTarget(const Target& target);
  bool fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role, Version version);
  bool fetchLatestRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role) {
    return fetchRole(result, maxsize, repo, role, Version());
  }
  void restoreHasherState(MultiPartHasher& hasher, StorageTargetRHandle* data);

  bool setPause(bool pause) { return token.setPause(pause); }

 private:
  std::shared_ptr<HttpInterface> http;
  std::shared_ptr<INvStorage> storage;
  const Config& config;
  FetcherProgressCb progress_cb;
  FlowControlToken token;
};

struct DownloadMetaStruct {
  DownloadMetaStruct(Target target_in, FetcherProgressCb progress_cb_in, FlowControlToken* token_in)
      : hash_type{target_in.hashes()[0].type()},
        target{std::move(target_in)},
        token{token_in},
        progress_cb{std::move(progress_cb_in)} {}
  uint64_t downloaded_length{0};
  unsigned int last_progress{0};
  std::unique_ptr<StorageTargetWHandle> fhandle;
  const Hash::Type hash_type;
  MultiPartHasher& hasher() {
    switch (hash_type) {
      case Hash::Type::kSha256:
        return sha256_hasher;
      case Hash::Type::kSha512:
        return sha512_hasher;
      default:
        throw std::runtime_error("Unknown hash algorithm");
    }
  }
  Target target;
  FlowControlToken* token;
  FetcherProgressCb progress_cb;

 private:
  MultiPartSHA256Hasher sha256_hasher;
  MultiPartSHA512Hasher sha512_hasher;
};

}  // namespace Uptane

#endif
