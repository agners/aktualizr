#ifndef AKTUALIZR_APIQUEUE_H
#define AKTUALIZR_APIQUEUE_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>

namespace Api {

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
  bool setPause(bool set_paused);

  ///
  /// Called by the controlling thread to request the task to abort.
  /// @return `false` if the task was already aborted, `true` otherwise.
  ///
  bool setAbort();

  ///
  /// Called by the controlled thread to query the currently requested state.
  /// Sleeps if the state is `Paused` and `blocking == true`.
  /// @return `true` for `Running` state, `false` for `Aborted`,
  /// and also `false` for the `Paused` state, if the call is non-blocking.
  ///
  bool canContinue(bool blocking = true) const;

 private:
  enum class State {
    Running,  // transitions: ->Paused, ->Aborted
    Paused,   // transitions: ->Running, ->Aborted
    Aborted   // transitions: none
  } state_{State::Running};
  mutable std::mutex m_;
  mutable std::condition_variable cv_;
};


class CommandQueue {
 public:
  ~CommandQueue();
  void run();
  void enqueue(const std::function<void()>& t);
  void abort();
  bool pause(bool do_pause);  // returns true iff pause→resume or resume→pause

 private:
  std::atomic_bool shutdown_{false};
  std::atomic_bool paused_{false};
  std::thread thread_;
  std::queue<std::function<void()>> queue_;
  std::mutex m_;
  std::condition_variable cv_;
};

}
#endif //AKTUALIZR_APIQUEUE_H
