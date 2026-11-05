#include "campustwin/environment.hpp"
#include "campustwin/students.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace campustwin;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void rejected(const std::function<void()>& f) {
    try { f(); } catch (const ValidationError&) { return; }
    throw std::runtime_error("expected ValidationError");
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("temporary path required");
        CampusEnvironment env;
        CampusScenario scene;
        scene.locations = {{"T","Teaching",LocationKind::Teaching,10}, {"F","Food",LocationKind::Canteen,10},
                           {"L","Library",LocationKind::Library,10}, {"D","Dorm",LocationKind::Dormitory,10}};
        scene.roads = {{"T","F",60}, {"F","L",60}, {"L","D",60}};
        scene.facilities = {{"C","F",1,1}};
        scene.seat_pools = {{"F",1}, {"L",1}};
        env.replace_scenario(scene);
        const auto saved = std::filesystem::path(argv[1]) / "activity_scene_roundtrip";
        env.save_directory(saved);
        CampusEnvironment copy; copy.load_directory(saved);
        check(copy.scenario().seat_pools.size() == 2 && copy.scenario().seat_pools[1].location_id == "L",
              "seat configuration must survive save/load");
        auto invalid = scene; invalid.seat_pools.push_back({"F",2});
        rejected([&] { env.replace_scenario(invalid); });
        check(env.scenario().seat_pools.size() == 2, "failed scene load must preserve old scene");
        invalid = scene; invalid.seat_pools[0].location_id = "UNKNOWN";
        rejected([&] { env.replace_scenario(invalid); });
        StudentSpec spec{"S","T","C","D",0,60,2};
        spec.activities = {{"meal",ActivityKind::Lunch,"",0,2,false}, {"read",ActivityKind::Study,"L",5,2,true}};
        StudentEngine students; students.reset(scene,{spec});
        check(students.tick(0,env).empty(), "depart at zero");
        auto arrival = students.tick(1,env).at(0);
        students.on_service_started({arrival.request_id,"S","C",0,1,0,2,"standard"});
        students.on_service_completed({arrival.request_id,"S","C",0,2});
        rejected([&] { students.snapshots(1); });
        students.tick(2,env);
        auto request = students.seat_requests(2).at(0);
        check(students.snapshots(2).at(0).state == StudentState::WaitingSeat, "cannot eat without grant");
        auto wrong = request; wrong.location_id = "L";
        rejected([&] { students.on_seat_granted({wrong,2,4}); });
        check(students.snapshots(2).at(0).state == StudentState::WaitingSeat, "bad grant must not change state");
        students.on_seat_granted({request,2,4});
        rejected([&] { students.on_seat_granted({request,2,4}); });
        check(students.snapshots(2).at(0).occupied_seat == "F", "meal seat tracked");
        auto bad_spec = spec; bad_spec.activities[1].id = "meal";
        rejected([&] { students.reset(scene,{bad_spec}); });
        students.tick(3,env);
        check(students.seat_requests(3).empty(), "no duplicate seat request while eating");
        students.tick(4,env);
        check(students.snapshots(4).at(0).state == StudentState::Moving &&
              !students.snapshots(4).at(0).occupied_seat, "release meal seat before travel");
        students.tick(5,env); request = students.seat_requests(5).at(0);
        students.on_seat_granted({request,5,7});
        check(students.snapshots(5).at(0).state == StudentState::InActivity, "study begins after grant");
        students.tick(6,env); students.tick(7,env);
        const auto final = students.snapshots(7).at(0);
        check(final.state == StudentState::Finished && !final.occupied_seat && final.location_id == "L",
              "schedule ends at its last activity, without an implicit return");
        const auto records = students.activity_records();
        check(records.size() == 2 && records[0].completed_at == 4 && records[1].completed_at == 7,
              "exact activity history");
        std::cout << "PASS schedule event contracts, reset atomicity, activity boundaries and A seat persistence\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
