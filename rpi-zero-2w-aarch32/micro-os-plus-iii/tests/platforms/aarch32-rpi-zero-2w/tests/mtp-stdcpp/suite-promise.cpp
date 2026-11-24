// 02 - promise and future: the core of the C++11 future library on top of
// the gthread layer.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>

namespace
{
  using namespace std::chrono_literals;

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

mt::static_suite suite_promise{
  "02 - promise and future", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("promise<int>: the value set by a thread reaches get ()",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();
               t.expect (future.valid ()) << "a future is attached";

               std::thread th ([p = std::move (promise)] () mutable {
                 std::this_thread::sleep_for (5ms);
                 p.set_value (41 + 1);
               });

               t.expect (mt::eq (future.get (), 42))
                   << "get () waited for the value";
               th.join ();
               t.expect (!future.valid ()) << "get () releases the state";
             });

    ts.test ("promise<void>: a value-less handshake", [] (auto& t) {
      std::promise<void> promise;
      std::future<void> future = promise.get_future ();
      std::atomic<bool> started{ false };

      std::thread th ([&]
                      {
                        started.store (true, std::memory_order_release);
                        std::this_thread::sleep_for (5ms);
                        promise.set_value ();
                      });

      t.expect (wait_flag (started)) << "the setter is running";
      future.wait ();
      t.expect (future.valid ()) << "wait () does not consume the state";
      th.join ();
    });

    ts.test ("future_status: timeout before the value, ready after",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();

               t.expect (future.wait_for (1ms) == std::future_status::timeout)
                   << "nothing is ready yet";

               promise.set_value (7);

               t.expect (future.wait_for (1ms) == std::future_status::ready)
                   << "the value is ready";
               t.expect (mt::eq (future.get (), 7)) << "and readable";
             });

    ts.test ("shared_future: two threads read the same value",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();
               std::shared_future<int> shared = future.share ();
               std::atomic<int> sum{ 0 };

               std::thread a ([shared, &sum] {
                 sum.fetch_add (shared.get (), std::memory_order_relaxed);
               });
               std::thread b ([shared, &sum] {
                 sum.fetch_add (shared.get (), std::memory_order_relaxed);
               });

               promise.set_value (21);
               a.join ();
               b.join ();

               t.expect (mt::eq (sum.load (), 42))
                   << "both readers saw 21";
             });

    ts.test ("a promise moved between threads keeps its future",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();

               // Move the promise into a lambda, then move it again between
               // the two threads through a shared slot.
               std::thread producer ([p = std::move (promise)] () mutable {
                 std::this_thread::sleep_for (2ms);
                 p.set_value (99);
               });

               t.expect (mt::eq (future.get (), 99)) << "the moved promise set";
               producer.join ();
             });
  }
};
