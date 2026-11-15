// 05 - synchronization: condition_variable, counting/binary semaphore,
// latch, barrier and shared_mutex -- the C++20 headers built for the kernel.
#include <mtp_board.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <shared_mutex>
#include <thread>
#include <latch>
#include <barrier>

namespace
{
  using namespace std::chrono_literals;
} // namespace

mt::static_suite suite_sync{
  "05 - condition variable and C++20 sync", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("condition_variable: notify_one wakes the waiter", [] (auto& t) {
      std::mutex mutex;
      std::condition_variable cv;
      bool ready = false;
      std::atomic<bool> woke{ false };

      std::thread th ([&]
                      {
                        std::unique_lock<std::mutex> lock (mutex);
                        cv.wait (lock, [&] { return ready; });
                        woke.store (true, std::memory_order_release);
                      });

      {
        std::lock_guard<std::mutex> lock (mutex);
        ready = true;
      }
      cv.notify_one ();
      th.join ();

      t.expect (woke.load ()) << "the waiter observed the predicate";
    });

    ts.test ("condition_variable::wait_for times out", [] (auto& t) {
      std::mutex mutex;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock (mutex);

      t.expect (cv.wait_for (lock, 2ms) == std::cv_status::timeout)
          << "nothing notified it";
      t.expect (lock.owns_lock ()) << "the lock is held again";
    });

    ts.test ("notify_all_at_thread_exit: the notifier runs at exit",
             [] (auto& t) {
               std::mutex mutex;
               std::condition_variable cv;
               std::atomic<bool> ready{ false };
               std::atomic<bool> woke{ false };

               std::thread th ([&]
                               {
                                 std::unique_lock<std::mutex> lock (mutex);
                                 ready.store (true, std::memory_order_release);
                                 std::notify_all_at_thread_exit (cv,
                                                                 std::move (lock));
                               });

               std::unique_lock<std::mutex> lock (mutex);
               cv.wait (lock, [&] { return ready.load (); });
               woke.store (true);
               th.join ();

               t.expect (woke.load ()) << "the waiter ran after the exit";
             });

    ts.test ("binary_semaphore: a handshake between two threads",
             [] (auto& t) {
               std::binary_semaphore ping (0);
               std::binary_semaphore pong (0);

               std::thread th ([&]
                               {
                                 ping.acquire ();
                                 pong.release ();
                               });

               ping.release ();
               t.expect (pong.try_acquire_for (500ms))
                   << "the thread answered";
               th.join ();
             });

    ts.test ("counting_semaphore: three acquires from one release",
             [] (auto& t) {
               std::counting_semaphore<4> sem (0);
               sem.release (3);

               unsigned taken = 0;
               while (sem.try_acquire ())
                 {
                   ++taken;
                 }
               t.expect (mt::eq (taken, 3u)) << "three tokens were available";
               t.expect (!sem.try_acquire ()) << "and then none";
             });

    ts.test ("latch: three arrivals release every waiter", [] (auto& t) {
      std::latch latch (3);
      std::atomic<int> passed{ 0 };
      std::atomic<bool> ready{ false };

      auto worker = [&]
      {
        ready.store (true);
        latch.count_down ();
        latch.wait ();
        passed.fetch_add (1, std::memory_order_relaxed);
      };

      std::thread a (worker);
      std::thread b (worker);
      latch.arrive_and_wait ();

      a.join ();
      b.join ();

      t.expect (ready.load ()) << "both workers reached the latch";
      t.expect (mt::eq (passed.load (), 2))
          << "both workers passed the latch";
      t.expect (latch.try_wait ()) << "the latch stays open";
    });

    ts.test ("barrier: one completion per phase, all threads pass",
             [] (auto& t) {
               std::atomic<int> completions{ 0 };
               std::atomic<int> passed{ 0 };
               std::barrier sync (3, [&] {
                 completions.fetch_add (1, std::memory_order_relaxed);
               });

               auto worker = [&]
               {
                 sync.arrive_and_wait ();
                 passed.fetch_add (1, std::memory_order_relaxed);
               };

               std::thread a (worker);
               std::thread b (worker);
               sync.arrive_and_wait ();

               a.join ();
               b.join ();

               t.expect (mt::eq (completions.load (), 1))
                   << "the completion ran once";
               t.expect (mt::eq (passed.load (), 2))
                   << "both workers passed the barrier";
             });

    ts.test ("shared_mutex: readers together, a writer alone",
             [] (auto& t) {
               std::shared_mutex shared;
               {
                 std::shared_lock<std::shared_mutex> first (shared);
                 std::shared_lock<std::shared_mutex> second (shared);
                 t.expect (first.owns_lock () && second.owns_lock ())
                     << "two readers hold it at once";
               }
               {
                 std::unique_lock<std::shared_mutex> writer (shared);
                 t.expect (writer.owns_lock ()) << "the writer got it";
               }
             });
  }
};
