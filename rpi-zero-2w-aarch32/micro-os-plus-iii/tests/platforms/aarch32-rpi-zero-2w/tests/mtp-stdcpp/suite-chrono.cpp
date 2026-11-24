// 06 - chrono: both clocks over the kernel's system clock, and the sleeps
// this_thread uses.
#include <mtp_board.hpp>

#include <chrono>
#include <thread>

namespace
{
  using namespace std::chrono_literals;
} // namespace

mt::static_suite suite_chrono{
  "06 - chrono", mtp_board::runner (),
  [] (mt::suite& ts) {
    ts.test ("steady_clock: monotonic and advances across a sleep",
             [] (auto& t) {
               const auto first = std::chrono::steady_clock::now ();
               std::this_thread::sleep_for (5ms);
               const auto second = std::chrono::steady_clock::now ();

               t.expect (second > first) << "time moved forward";
               t.expect (std::chrono::steady_clock::is_steady)
                   << "the clock says it is steady";

               const auto delta
                   = std::chrono::duration_cast<std::chrono::milliseconds> (
                         second - first)
                         .count ();
               t.expect (delta >= 4) << "at least 4 ms passed";
             });

    ts.test ("system_clock: the same start-up epoch", [] (auto& t) {
      const auto now = std::chrono::system_clock::now ();
      const auto since_epoch
          = std::chrono::duration_cast<std::chrono::milliseconds> (
                now.time_since_epoch ())
                .count ();
      t.expect (since_epoch >= 0) << "non-negative time since start-up";
    });

    ts.test ("duration conversions are exact where they should be",
             [] (auto& t) {
               t.expect (std::chrono::duration_cast<std::chrono::milliseconds> (
                             1s)
                             .count ()
                         == 1000)
                   << "1 s is 1000 ms";
               t.expect (std::chrono::duration_cast<std::chrono::microseconds> (
                             2ms)
                             .count ()
                         == 2000)
                   << "2 ms is 2000 us";
               t.expect (std::chrono::milliseconds (1500)
                             == std::chrono::seconds (1) + 500ms)
                   << "comparable durations";
             });

    ts.test ("sleep_until waits for the chosen time point", [] (auto& t) {
      const auto start = std::chrono::steady_clock::now ();
      std::this_thread::sleep_until (start + 10ms);
      const auto elapsed
          = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now () - start)
                .count ();
      t.expect (elapsed >= 8) << "at least 8 ms passed";
    });

    ts.test ("sleep_for (0 ms) returns immediately", [] (auto& t) {
      const auto start = std::chrono::steady_clock::now ();
      std::this_thread::sleep_for (0ms);
      const auto elapsed
          = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now () - start)
                .count ();
      t.expect (elapsed <= 5) << "no long wait";
    });
  }
};
