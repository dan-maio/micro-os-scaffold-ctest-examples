// 03 - packaged_task, async and at-thread-exit ready: the parts of <future>
// that build on std::thread.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>

namespace
{
  using namespace std::chrono_literals;
} // namespace

mt::static_suite suite_future{
  "03 - packaged_task and async", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("packaged_task: run on another thread, get () on this one",
             [] (auto& t) {
               std::packaged_task<int (int)> task (
                   [] (int value) { return value * 2; });
               std::future<int> future = task.get_future ();

               std::thread th (std::move (task), 21);
               t.expect (mt::eq (future.get (), 42)) << "the task returned 42";
               th.join ();
             });

    ts.test ("async (launch::async): runs on another thread",
             [] (auto& t) {
               std::atomic<std::thread::id> worker{};
               std::future<int> future = std::async (
                   std::launch::async,
                   [&worker]
                   {
                     worker.store (std::this_thread::get_id (),
                                   std::memory_order_release);
                     return 7;
                   });

               t.expect (mt::eq (future.get (), 7)) << "the value arrived";
               t.expect (worker.load () != std::this_thread::get_id ())
                   << "the callable ran on another thread";
             });

    ts.test ("async (launch::deferred): runs at get ()",
             [] (auto& t) {
               std::atomic<bool> ran{ false };
               std::future<int> future = std::async (
                   std::launch::deferred,
                   [&ran]
                   {
                     ran.store (true, std::memory_order_release);
                     return 5;
                   });

               t.expect (!ran.load ()) << "nothing ran before get ()";
               t.expect (mt::eq (future.get (), 5)) << "the value arrived";
               t.expect (ran.load ()) << "get () ran the callable";
             });

    ts.test ("set_value_at_thread_exit: ready once the thread is gone",
             [] (auto& t) {
               std::promise<int> promise;
               std::future<int> future = promise.get_future ();

               std::thread th (
                   [&promise] { promise.set_value_at_thread_exit (11); });

               t.expect (mt::eq (future.get (), 11))
                   << "get () waited for the thread to exit";
               th.join ();
             });

    ts.test ("future move assignment transfers the state", [] (auto& t) {
      std::promise<int> promise;
      std::future<int> future = promise.get_future ();
      std::future<int> other;

      other = std::move (future);
      t.expect (!future.valid ()) << "the source is empty";
      t.expect (other.valid ()) << "the target owns the state";

      promise.set_value (3);
      t.expect (mt::eq (other.get (), 3)) << "and reads the value";
    });

    ts.test ("two packaged tasks in flight at once", [] (auto& t) {
      std::packaged_task<int ()> a ([] { return 10; });
      std::packaged_task<int ()> b ([] { return 20; });
      std::future<int> fa = a.get_future ();
      std::future<int> fb = b.get_future ();

      std::thread ta (std::move (a));
      std::thread tb (std::move (b));

      fa.wait ();
      fb.wait ();
      t.expect (mt::eq (fa.get () + fb.get (), 30)) << "both tasks completed";

      ta.join ();
      tb.join ();
    });
  }
};
