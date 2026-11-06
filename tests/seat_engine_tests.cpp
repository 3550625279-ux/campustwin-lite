#include "campustwin/seats.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>

using namespace campustwin;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& call) {
    try { call(); } catch (const ValidationError&) { return; }
    throw std::runtime_error("expected ValidationError");
}
int main() {
    try {
        SeatEngine seats;
        rejected([&] { seats.release_due(0); });
        seats.reset({{"LIB", 1}, {"DINING", 2}});
        rejected([&] { seats.reset({{"LIB", 1}, {"LIB", 2}}); });
        seats.release_due(0);
        rejected([&] { seats.snapshots(0); });
        rejected([&] { seats.enqueue({{"r1","B","LIB",0,2}, {"r2","B","DINING",0,2}},0); });
        seats.enqueue({{"r2","B","LIB",0,2}, {"r1","A","LIB",0,3},
                       {"r3","C","DINING",0,1}},0);
        auto grants = seats.start_ready(0);
        check(grants.size() == 2, "separate pools must fill independently");
        check(grants[1].request.student_id == "A" && grants[1].until == 3, "same-tick ID order");
        rejected([&] { seats.start_ready(0); });
        for (int t = 1; t <= 2; ++t) {
            seats.release_due(t);
            if (t == 1) rejected([&] { seats.enqueue({{"r4","A","DINING",t,1}},t); });
            seats.enqueue({},t); check(seats.start_ready(t).empty(), "occupied pool must not overbook");
        }
        seats.release_due(3);
        seats.enqueue({{"r5","D","LIB",3,1}},3);
        grants = seats.start_ready(3);
        check(grants.size() == 1 && grants[0].request.student_id == "B" && grants[0].until == 5,
              "release at t reusable at t; old waiter before new arrivals");
        auto snapshot = seats.snapshots(3).at(1);
        check(snapshot.occupied == 1 && snapshot.queued == 1 && snapshot.released == 1 &&
              snapshot.total_wait_minutes == 3 && snapshot.busy_seat_minutes == 3, "exact cutoff accounting");
        for (int t = 4; t <= 6; ++t) {
            seats.release_due(t); seats.enqueue({},t); grants = seats.start_ready(t);
            if (t == 5) check(grants.size() == 1 && grants[0].request.student_id == "D", "FIFO after second release");
        }
        snapshot = seats.snapshots(6).at(1);
        check(snapshot.occupied == 0 && snapshot.queued == 0 && snapshot.released == 3 &&
              snapshot.total_wait_minutes == 5 && snapshot.busy_seat_minutes == 6, "final exact totals");
        seats.release_due(7);
        rejected([&] { seats.enqueue({{"r1","Z","LIB",7,1}},7); });
        seats.enqueue({{"r6","A","LIB",7,1}},7);
        check(seats.start_ready(7).size() == 1, "same student may return with a new request");
        std::cout << "PASS seat FIFO, phases, atomic input, reuse and conservation\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
