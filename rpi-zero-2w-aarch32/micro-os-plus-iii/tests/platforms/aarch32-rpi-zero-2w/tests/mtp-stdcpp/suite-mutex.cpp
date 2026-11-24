// 04 - mutexes and call_once: std::mutex, timed_mutex, recursive_mutex,
// unique_lock and std::call_once on the gthread layer.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace
{
  using namespace std::chrono_literals;

  std::mutex g_mutex;
  long g_counter = 0;

  std::once_flag g_once;
  std::atomic<int> g_once_calls{ 0 };

  bool
  wait_flag (const std::atomic<bool>& flag, unsigned ms = 2000)
  {
    for (unsigned i = 0; i < ms; ++i)
      {
        if (flag.load (std::memory_order_acquire))
          {
            return true;
          }
        std::this_thread::sleep_for (1ms);
      }
    return flag.load (std::memory_order_acquire);
  }
} // namespace

mt::static_suite suite_mutex{
  "04 - mutexes and call_once", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("std::mutex + lock_guard: no lost increment across threads",
             [] (auto& t) {
               constexpr int increments = 3000;
               g_counter = 0;

               auto worker = []
               {
                 for (int i = 0; i < increments; ++i)
                   {
                     std::lock_guard<std::mutex> lock (g_mutex);
                     const long value = g_counter;
                     asm volatile ("" ::: "memory");
                     g_counter = value + 1;
                   }
               };

               std::thread a (worker);
               std::thread b (worker);
               a.join ();
               b.join ();

               t.expect (mt::eq (g_counter, 2L * increments))
                   << "every increment landed exactly once";
             });

    ts.test ("timed_mutex: try_lock_for times out while another thread owns it",
             [] (auto& t) {
               std::timed_mutex timed;
               std::atomic<bool> held{ false };
               std::atomic<bool> release{ false };

               std::thread th ([&]
                               {
                                 timed.lock ();
                                 held.store (true, std::memory_order_release);
                                 while (!release.load (std::memory_order_acquire))
                                   {
                                     std::this_thread::sleep_for (1ms);
                                   }
                                 timed.unlock ();
                               });

               t.expect (wait_flag (held)) << "the other thread owns it";
               t.expect (!timed.try_lock_for (2ms))
                   << "try_lock_for (2 ms) failed";
               t.expect (!timed.try_lock_until (std::chrono::steady_clock::now ()
                                                + 2ms))
                   << "try_lock_until (now + 2 ms) failed";

               release.store (true, std::memory_order_release);
               th.join ();

               t.expect (timed.try_lock ()) << "free once the owner released";
               timed.unlock ();
             });

    ts.test ("recursive_mutex: depth counted, released only at zero",
             [] (auto& t) {
               std::recursive_mutex recursive;
               recursive.lock ();
               recursive.lock ();
               t.expect (recursive.try_lock ()) << "a third lock succeeds";
               recursive.unlock ();
               recursive.unlock ();
               recursive.unlock ();
               t.expect (recursive.try_lock ()) << "free at depth zero";
               recursive.unlock ();
             });

    ts.test ("call_once: the callable runs exactly once", [] (auto& t) {
      g_once_calls = 0;
      auto worker = []
      {
        std::call_once (g_once,
                        [] { g_once_calls.fetch_add (1, std::memory_order_relaxed); });
      };

      std::thread a (worker);
      std::thread b (worker);
      std::thread c (worker);
      a.join ();
      b.join ();
      c.join ();

      t.expect (mt::eq (g_once_calls.load (), 1))
          << "three callers, one call";
    });

    ts.test ("unique_lock: defer_lock and try_to_lock", [] (auto& t) {
      std::mutex mutex;
      std::unique_lock<std::mutex> lock (mutex, std::defer_lock);

      t.expect (!lock.owns_lock ()) << "deferred: not owned";
      t.expect (lock.try_lock ()) << "try_lock () succeeds";
      t.expect (lock.owns_lock ()) << "now owned";
      lock.unlock ();
      t.expect (!lock.owns_lock ()) << "unlocked";

      std::unique_lock<std::mutex> taken (mutex);
      std::unique_lock<std::mutex> second (mutex, std::try_to_lock);
      t.expect (!second.owns_lock ()) << "try_to_lock fails while held";
    });
  }
};
