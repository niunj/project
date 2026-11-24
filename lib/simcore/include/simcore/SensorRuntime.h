#pragma once

#include <simcore/Time.h>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace simcore {

/// Window-independent measurement orchestration. Backends consume each event
/// separately after the host has made its complete truth interval available.
/// This class neither creates a graphics device nor advances physics.
class SensorRuntime {
public:
    struct Cadence {
        uint32_t sensor_id = 0;
        PeriodicTimeline timeline;
        uint64_t first_index = 1;
        TimePoint exposure = 0;
        bool contiguous_windows = false; // e.g. non-overlapping radar CPIs
        uint32_t samples = 1;
    };
    struct Measurement {
        uint32_t sensor_id = 0;
        uint64_t frame_id = 0;
        uint64_t index = 0;
        TimePoint start = 0, end = 0;
        uint32_t samples = 1;
    };

    void add(Cadence cadence) {
        if (started_) throw std::logic_error("configure sensor runtime before advancing it");
        if (!cadence.samples || cadence.exposure<0 ||
            (cadence.contiguous_windows && !cadence.first_index) ||
            cadence.timeline.at(cadence.first_index)<cadence.exposure)
            throw std::invalid_argument("measurement window requires unavailable pre-epoch truth");
        const auto id=cadence.sensor_id;
        if (!streams_.emplace(id,Stream{cadence,cadence.first_index,0}).second)
            throw std::invalid_argument("duplicate sensor runtime id");
    }
    std::vector<Measurement> advanceTo(TimePoint target, std::size_t batch_limit=1024) {
        if (target<0 || (started_ && target<target_) || !batch_limit)
            throw std::invalid_argument("invalid sensor runtime advance");
        started_=true; target_=target;
        std::vector<Measurement> out;
        while (out.size()<batch_limit) {
            auto best=streams_.end();
            TimePoint deadline=std::numeric_limits<TimePoint>::max();
            for (auto it=streams_.begin();it!=streams_.end();++it) {
                const auto time=it->second.cadence.timeline.at(it->second.next);
                if (time<=target && (best==streams_.end() || time<deadline)) { best=it; deadline=time; }
            }
            if (best==streams_.end()) break;
            auto& stream=best->second;
            const auto& cadence=stream.cadence;
            const auto begin=cadence.contiguous_windows ? cadence.timeline.at(stream.next-1) : deadline-cadence.exposure;
            // Check overflow before committing this event, so it remains retryable.
            if (stream.next==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("measurement index overflow");
            cadence.timeline.at(stream.next+1);
            out.push_back({best->first,stream.frames,stream.next,begin,deadline,cadence.samples});
            ++stream.frames; ++stream.next;
        }
        const auto next=nextDeadline();
        backlogged_=next && *next<=target;
        return out;
    }
    std::optional<TimePoint> nextDeadline() const {
        std::optional<TimePoint> result;
        for (const auto& [id,stream]:streams_) {
            const auto deadline=stream.cadence.timeline.at(stream.next);
            if (!result || deadline<*result) result=deadline;
        }
        return result;
    }
    uint64_t frames(uint32_t id) const {
        const auto it=streams_.find(id); return it==streams_.end() ? 0 : it->second.frames;
    }
    bool backlogged() const { return backlogged_; }
private:
    struct Stream { Cadence cadence; uint64_t next,frames; };
    std::map<uint32_t,Stream> streams_; // stable tie-break by sensor id
    TimePoint target_=0;
    bool started_=false, backlogged_=false;
};
}
