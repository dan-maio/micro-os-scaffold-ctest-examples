// 01 - std::thread and TLS: join, detach, jthread, ids, yield, sleep, thread_local,
// on the StateOS stdc++ gthread layer.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
  using namespace std::chrono_literals;

  std::atomic<int> g_counter{ 0 };
  thread_local int g_tls = 0;
  std::atomic<int> g_tls_ok{ 0 };

  // Bounded wait that does not need the thread to be joined first: sleep in
  // one-tick steps, so the scheduler on the other cores keeps running.
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

mt::static_suite suite_thread{
  "01 - std::thread and TLS", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("join: a thread runs, its writes are visible after join ()",
             [] (auto& t) {
               constexpr int increments = 4000;
               g_counter = 0;
               std::thread th ([]
                               {
                                 for (int i = 0; i < increments; ++i)
                                   {
                                     g_counter.fetch_add (
                                         1, std::memory_order_relaxed);
                                   }
                               });
               t.expect (th.joinable ()) << "joinable before join ()";
               th.join ();
               t.expect (mt::eq (g_counter.load (), increments))
                   << "every increment visible after join ()";
               t.expect (!th.joinable ()) << "not joinable after join ()";
             });

    ts.test ("detach: the thread runs on without join ()", [] (auto& t) {
      std::atomic<bool> ran{ false };
      std::thread th ([&ran] { ran.store (true, std::memory_order_release); });
      t.expect (th.joinable ()) << "joinable before detach ()";
      th.detach ();
      t.expect (!th.joinable ()) << "not joinable after detach ()";
      t.expect (wait_flag (ran)) << "the detached thread ran";
    });

    ts.test ("get_id (): another thread has a distinct, stable id",
             [] (auto& t) {
               const std::thread::id main_id = std::this_thread::get_id ();
               std::thread::id other_id;
               std::atomic<bool> stable{ false };

               std::thread th ([&]
                               {
                                 other_id = std::this_thread::get_id ();
                                 std::this_thread::yield ();
                                 stable.store (std::this_thread::get_id ()
                                                   == other_id,
                                               std::memory_order_release);
                               });
               th.join ();

               t.expect (!(main_id == other_id))
                   << "the worker's id differs from the runner's";
               t.expect (stable.load ()) << "the worker's id is stable";
               t.expect (main_id == std::this_thread::get_id ())
                   << "the runner's id is stable";
             });

    ts.test ("hardware_concurrency () reports the board's cores",
             [] (auto& t) {
               t.expect (mt::eq (static_cast<unsigned> (
                                     std::thread::hardware_concurrency ()),
                                 static_cast<unsigned> (OS_NCPU)))
                   << "one thread per core";
             });

    ts.test ("sleep_for () lasts at least the requested time", [] (auto& t) {
      const auto before = std::chrono::steady_clock::now ();
      std::this_thread::sleep_for (20ms);
      const auto after = std::chrono::steady_clock::now ();
      const auto elapsed
          = std::chrono::duration_cast<std::chrono::milliseconds> (after
                                                                  - before)
                .count ();
      t.expect (elapsed >= 15) << "slept at least 15 ms (the tick is 1 ms)";
    });

    ts.test ("thread_local: one instance per thread (emulated TLS)",
             [] (auto& t) {
               g_tls = 1; // the runner's own copy
               g_tls_ok = 0;

               auto worker = [] (int value) {
                 g_tls = value;
                 std::this_thread::sleep_for (5ms);
                 if (g_tls == value)
                   {
                     g_tls_ok.fetch_add (1, std::memory_order_relaxed);
                   }
               };

               std::thread a (worker, 10);
               std::thread b (worker, 20);
               a.join ();
               b.join ();

               t.expect (mt::eq (g_tls_ok.load (), 2))
                   << "each thread saw its own value";
               t.expect (mt::eq (g_tls, 1))
                   << "the runner's value was untouched";
               g_tls = 0;
             });

    ts.test ("jthread: joins at scope exit, the stop token reaches the worker",
             [] (auto& t) {
               std::atomic<bool> started{ false };
               std::atomic<bool> stopped{ false };
               {
                 std::jthread worker ([&] (std::stop_token token) {
                   started.store (true, std::memory_order_release);
                   while (!token.stop_requested ())
                     {
                       std::this_thread::sleep_for (1ms);
                     }
                   stopped.store (true, std::memory_order_release);
                 });

                 t.expect (wait_flag (started)) << "the jthread started";
                 worker.request_stop ();
               } // the destructor joins here
               t.expect (stopped.load ())
                   << "the worker saw the stop request, and the join completed";
             });
  }
};
