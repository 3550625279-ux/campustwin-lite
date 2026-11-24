#include "run_cache.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace campustwin {
namespace {

// Bump the version when simulation rules or recording fields change.
constexpr std::string_view version = "CampusTwin.RecordedRun.1";
constexpr std::size_t max_cache_size = 512 * 1024 * 1024;

// Field order is the local cache format. Files store values, never object memory
// such as pointers or container internals.
#define CACHE_FIELDS(Type, ...) \
    template<class Archive, class Value> \
    void fields(Archive& ar, Value& v, Type*) { ar(__VA_ARGS__); }

CACHE_FIELDS(ActivitySpec, v.id, v.kind, v.location_id, v.start_minute, v.duration_minutes, v.requires_seat)
CACHE_FIELDS(Location, v.id, v.name, v.kind, v.capacity)
CACHE_FIELDS(Road, v.from, v.to, v.distance_m)
CACHE_FIELDS(WindowServiceConfig, v.window_id, v.meal_type, v.service_minutes)
CACHE_FIELDS(FacilityConfig, v.id, v.location_id, v.window_count, v.service_minutes, v.window_services)
CACHE_FIELDS(SeatPoolConfig, v.location_id, v.capacity)
CACHE_FIELDS(CampusScenario, v.locations, v.roads, v.facilities, v.seat_pools)
CACHE_FIELDS(StudentSpec, v.id, v.initial_location, v.lunch_facility, v.after_meal_location,
             v.departure_minute, v.walk_m_per_min, v.meal_minutes, v.lunch_policy, v.meal_type, v.activities)
CACHE_FIELDS(StudentArrival, v.request_id, v.student_id, v.facility_id, v.at, v.meal_type)
CACHE_FIELDS(ServiceCompleted, v.request_id, v.student_id, v.facility_id, v.window_id, v.at)
CACHE_FIELDS(ServiceStarted, v.request_id, v.student_id, v.facility_id, v.window_id, v.at, v.wait_minutes,
             v.expected_end_at, v.meal_type)
CACHE_FIELDS(MoveInfo, v.from, v.to, v.depart_at, v.arrive_at, v.distance_m)
CACHE_FIELDS(StudentSnapshot, v.at, v.student_id, v.state, v.location_id, v.move, v.lunch_facility, v.occupied_seat)
CACHE_FIELDS(SeatRequest, v.request_id, v.student_id, v.location_id, v.at, v.duration_minutes)
CACHE_FIELDS(SeatGranted, v.request, v.at, v.until)
CACHE_FIELDS(SeatSnapshot, v.at, v.location_id, v.capacity, v.queued, v.occupied, v.arrived, v.granted,
             v.released, v.max_queue, v.total_wait_minutes, v.busy_seat_minutes)
CACHE_FIELDS(ActivityRecord, v.student_id, v.activity, v.destination, v.departed_at, v.arrived_at, v.started_at,
             v.completed_at, v.distance_m, v.path, v.seat_wait_minutes)
CACHE_FIELDS(LunchDecision, v.at, v.student_id, v.policy, v.facility_id, v.destination, v.observed_at,
             v.queue_length, v.distance_m, v.path, v.travel_minutes, v.estimated_wait_minutes,
             v.service_minutes, v.estimated_total_minutes, v.selected, v.reason)
CACHE_FIELDS(FacilitySnapshot, v.at, v.facility_id, v.queue_length, v.in_service, v.arrived, v.started, v.completed,
             v.max_queue_length, v.total_wait_minutes, v.busy_window_minutes)
CACHE_FIELDS(FacilitySummary, v.facility_id, v.window_count, v.observation_minutes, v.arrived, v.started,
             v.completed, v.max_queue_length, v.total_wait_minutes, v.busy_window_minutes)
CACHE_FIELDS(TickFrame, v.at, v.completed, v.arrivals, v.started, v.finished_students, v.facilities,
             v.decisions, v.seat_requests, v.seat_grants, v.seats)
#undef CACHE_FIELDS

struct Encoder {
    std::string bytes;

    template<class... T> void operator()(const T&... value) { (write(value), ...); }

    void write(const std::string& value) {
        write(static_cast<std::uint64_t>(value.size()));
        bytes.append(value);
    }
    template<class T> void write(const std::vector<T>& list) {
        write(static_cast<std::uint64_t>(list.size()));
        for (const auto& value : list) write(value);
    }
    template<class T> void write(const std::optional<T>& value) {
        write(value.has_value());
        if (value) write(*value);
    }
    template<class T> void write(const T& value) {
        if constexpr (std::is_same_v<T, bool>) write(static_cast<std::uint8_t>(value));
        else if constexpr (std::is_enum_v<T>) write(static_cast<std::underlying_type_t<T>>(value));
        else if constexpr (std::is_arithmetic_v<T>) bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
        else fields(*this, value, static_cast<T*>(nullptr));
    }
};

struct Decoder {
    std::string_view bytes;

    template<class... T> void operator()(T&... value) { (read(value), ...); }

    std::string_view take(std::size_t count) {
        if (count > bytes.size()) throw ValidationError("incomplete run cache");
        const auto part = bytes.substr(0, count);
        bytes.remove_prefix(count);
        return part;
    }

    std::size_t count() {
        std::uint64_t size = 0;
        read(size);
        if (size > bytes.size()) throw ValidationError("invalid run cache size");
        return static_cast<std::size_t>(size);
    }
    void read(std::string& value) { value = take(count()); }
    template<class T> void read(std::vector<T>& values) {
        const auto size = count();
        if (size > max_cache_size / sizeof(T)) throw ValidationError("run cache allocation limit");
        values.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            values.emplace_back();
            read(values.back());
        }
    }
    template<class T> void read(std::optional<T>& value) {
        bool present = false;
        read(present);
        if (present) read(value.emplace());
    }
    template<class T> void read(T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t flag = 0;
            read(flag);
            if (flag > 1) throw ValidationError("invalid run cache flag");
            value = flag == 1;
        } else if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> raw{};
            read(raw);
            value = static_cast<T>(raw);
        } else if constexpr (std::is_arithmetic_v<T>) {
            std::memcpy(&value, take(sizeof(T)).data(), sizeof(T));
        } else {
            fields(*this, value, static_cast<T*>(nullptr));
        }
    }
};

// A checksum detects accidental corruption. Exact input bytes are compared
// separately, so even a filename hash collision cannot load the wrong result.
std::uint64_t checksum(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool valid_history(const RecordedRun& run) {
    if (run.frames.size() != static_cast<std::size_t>(run.settings.duration_minutes) + 1 ||
        run.student_history.size() != run.population.size()) return false;
    for (std::size_t i = 0; i < run.frames.size(); ++i) {
        if (run.frames[i].at != static_cast<SimTime>(i) ||
            run.frames[i].finished_students > run.population.size()) return false;
    }
    std::vector<StudentId> ids;
    for (const auto& student : run.population) ids.push_back(student.id);
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& history = run.student_history[i];
        if (history.empty() || history.front().at != 0) return false;
        SimTime previous = -1;
        for (const auto& snapshot : history) {
            if (snapshot.student_id != ids[i] || snapshot.at <= previous ||
                snapshot.at > run.settings.duration_minutes) return false;
            previous = snapshot.at;
        }
    }
    return true;
}
}  // namespace

RunCacheKey make_run_cache_key(const std::filesystem::path& directory, const CampusScenario& scenario,
                              const std::vector<StudentSpec>& population, SimTime duration) {
    Encoder inputs;
    inputs(std::string(version), duration, scenario, population);
    std::ostringstream name;
    name << std::hex << checksum(inputs.bytes) << ".run";
    return {directory / name.str(), std::move(inputs.bytes)};
}

std::optional<RecordedRun> load_run_cache(const RunCacheKey& key, const CampusScenario& scenario,
                                        const std::vector<StudentSpec>& population, SimTime duration) {
    std::ifstream stream(key.file, std::ios::binary | std::ios::ate);
    if (!stream) return std::nullopt;
    const auto size = stream.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(max_cache_size)) return std::nullopt;
    std::string bytes(static_cast<std::size_t>(size), '\0');
    stream.seekg(0);
    if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
    try {
        Decoder decoder{bytes};
        if (decoder.take(key.inputs.size()) != key.inputs) return std::nullopt;
        std::uint64_t expected = 0;
        decoder(expected);
        if (checksum(decoder.bytes) != expected) return std::nullopt;
        RecordedRun run;
        run.settings.duration_minutes = duration;
        run.scenario = scenario;
        run.population = population;
        decoder(run.frames, run.student_history, run.activities, run.summaries);
        if (!decoder.bytes.empty() || !valid_history(run)) return std::nullopt;
        return run;
    } catch (const ValidationError&) {
        return std::nullopt;
    }
}

void save_run_cache(const RunCacheKey& key, const RecordedRun& run) {
    Encoder data;
    data(run.frames, run.student_history, run.activities, run.summaries);
    if (key.inputs.size() + sizeof(std::uint64_t) + data.bytes.size() > max_cache_size)
        throw ValidationError("run exceeds local cache size limit");
    const auto hash = checksum(data.bytes);
    std::filesystem::create_directories(key.file.parent_path());
    auto temporary = key.file;
    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(key.inputs.data(), static_cast<std::streamsize>(key.inputs.size()));
        stream.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        stream.write(data.bytes.data(), static_cast<std::streamsize>(data.bytes.size()));
        stream.close();
        if (!stream) throw ValidationError("failed to write local run cache");
        std::filesystem::rename(temporary, key.file);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

}  // namespace campustwin
