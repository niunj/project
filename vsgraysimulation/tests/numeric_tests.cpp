#include <rocky_env/RadarSampling.h>
#include <rocky_env/RadarStages.h>
#include <rocky_env/SensorScheduler.h>
#include <rocky_env/StateRingBuffer.h>
#include <product_dispatch/ProductDispatch.h>
#include <simcore/Time.h>
#include <simcore/GeoFrame.h>
#include "vsgraysim/broker/RadarFrameCodec.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void sampling() {
    using namespace rockyenv::sensor;
    const auto clock = RadarSampling::make(30e6);
    check(std::abs(clock.rangeSpacing() - 4.996540966666667) < 1e-12, "radar range spacing");
    for (uint32_t fftSize : {512u, 1024u, 2048u, 4096u}) {
        RadarEchoMeta meta;
        meta.n_fast = fftSize;
        meta.cpi_pulses = 2;
        ProductFrame in, out;
        in.width = fftSize; in.height = 2;
        in.iq.resize(size_t(fftSize) * 2);
        // A delayed transmitted LFM. Matched filtering must peak at the
        // SAME delay for every zero-padding length, with identical gain.
        constexpr uint32_t delay = 100;
        const uint32_t chirpSize = uint32_t(std::round(meta.pulse_width_s * clock.rate_hz));
        for (uint32_t p = 0; p < 2; ++p)
            for (uint32_t n = 0; n < chirpSize; ++n) {
                const double t = n * clock.period();
                in.iq[size_t(p) * fftSize + delay + n] = std::polar(1.0f,
                    float(std::acos(-1.0) * meta.bandwidth_hz / meta.pulse_width_s * t * t));
            }
        RangeCompStage(meta).process(in, out);
        uint32_t peak = 0;
        for (uint32_t n = 1; n < fftSize; ++n)
            if (std::abs(out.iq[n]) > std::abs(out.iq[peak])) peak = n;
        check(peak == delay, "matched filter changes delay with FFT padding");
        check(std::abs(std::abs(out.iq[peak]) - chirpSize) < 1e-3, "matched filter gain");
    }
    bool rejected = false;
    try { RadarSampling::make(30e6, 10e6); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "undersampling must be rejected");
    RadarEchoMeta meta; meta.n_fast = 512; meta.cpi_pulses = 2; meta.sample_rate_hz = 60e6;
    const std::vector<std::complex<float>> echo(1024);
    const auto products = runRadarChain(echo, meta, 0, 1.0, 7, {2, 4});
    check(products.size() == 2 && products.front().sample_rate_hz == 60e6,
          "CPU radar products lost the sampling clock");
    rejected = false;
    try { runRadarChain(echo, meta, 0, 1.0, 7, {3}); }
    catch (const std::logic_error&) { rejected = true; }
    check(rejected, "RD pass-through was advertised as focused SAR");
    for (auto badStage : {5u, 100u}) {
        rejected = false;
        try { runRadarChain(echo, meta, 0, 1.0, 7, {badStage}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid radar stage accepted");
    }
    ProductFrame malformed, output;
    malformed.width = 512; malformed.height = 2; malformed.iq.resize(1023);
    rejected = false;
    try { RangeCompStage(meta).process(malformed, output); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "truncated FFT input accepted");
    check(!writeMagnitudePPM(malformed, "/tmp/vsgraysim-invalid-radar.ppm"), "truncated IQ image writer accepted");
    malformed.iq.clear(); malformed.pixels.resize(1023);
    check(!writeMagnitudePPM(malformed, "/tmp/vsgraysim-invalid-radar.ppm"), "truncated scalar image writer accepted");
    ProductFrame aliased; aliased.width = aliased.height = 2; aliased.iq = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};
    ProductStageImpl().process(aliased, aliased);
    check(aliased.iq.empty() && aliased.pixels.back() == 1.0f, "in-place radar magnitude lost input");
}

void dispatch() {
    using namespace rockyenv::sensor;
    SensorDesc desc; desc.frame_kind = "MWIR";
    product_dispatch::ProductDispatcher d;
    d.configure(desc); d.setLinked(true);
    int deliveries = 0;
    d.onIr = [&](const vsgraysim::IrFrame& f) {
        check(f.radiance.size() == 4, "sink received incomplete pixels");
        ++deliveries; return true;
    };
    ProductFrame f; f.width = f.height = 2;
    f.gpu_resident = true;
    check(!d.dispatch(f), "a residency flag is not a payload");
    f.gpu_resident = false;
    f.pixels = {1, 2, 3};
    check(!d.dispatch(f), "truncated image accepted");
    f.pixels.push_back(4);
    d.onIr = [](const vsgraysim::IrFrame&) { return false; };
    check(!d.dispatch(f), "sink rejection counted as delivery");
    check(d.stats().published == 0 && d.stats().sink_rejected == 1, "delivery statistics");
    d.onIr = [&](const vsgraysim::IrFrame&) { ++deliveries; return true; };
    check(d.dispatch(f), "valid image rejected");
    d.setPowered(false);
    check(!d.dispatch(f) && deliveries == 1, "power gate");
    check(d.stats().published == 1 && d.stats().dropped_invalid == 2, "invalid frame statistics");
}

void clocks() {
    using namespace simcore;
    check(PeriodicTimeline(60).at(60ull * 86400) == 86400ll * nanosecondsPerSecond,
          "24-hour clock drift");
    check(PeriodicTimeline(30000, 1001).at(30000) == 1001ll * nanosecondsPerSecond,
          "rational broadcast clock drift");
    SimulationClock clock;
    for (int i = 0; i < 60; ++i) clock.advance();
    check(clock.now() == nanosecondsPerSecond, "frame clock accumulates rounding");
    clock.setRate(PeriodicTimeline(120));
    for (int i = 0; i < 120; ++i) clock.advance();
    check(clock.now() == 2 * nanosecondsPerSecond, "rate change rewound time");

    using namespace rockyenv::sensor;
    SensorDesc desc; desc.frame_kind = "MWIR"; desc.rate_hz = 25; desc.integration_ms = 8;
    auto run = [&](int displayRate) {
        SensorScheduler scheduler; check(scheduler.configure(desc), "image scheduler configuration");
        std::vector<SchedulerEvent> events;
        for (int i = 0; i <= displayRate * 2; ++i) {
            auto due = scheduler.update(double(i) / displayRate);
            events.insert(events.end(), due.begin(), due.end());
        }
        return events;
    };
    const auto a = run(30), b = run(144);
    check(a.size() == 50 && a.size() == b.size(), "exposure count depends on display rate");
    for (size_t i = 0; i < a.size(); ++i) {
        check(a[i].t == b[i].t && a[i].frame_id == b[i].frame_id, "exposure clock depends on polling");
        check(std::abs(a[i].t - a[i].t_start - .008) < 1e-12, "exposure window length");
    }
    desc.rate_hz = 1000; desc.integration_ms = .5;
    SensorScheduler scheduler; check(scheduler.configure(desc), "fast scheduler configuration");
    size_t count = 0;
    do { count += scheduler.update(10).size(); } while (scheduler.backlogged());
    check(count == 10000, "scheduler discarded backlog");
    check(scheduler.update(10).empty(), "same timestamp duplicates exposure");
    desc.rate_hz = std::numeric_limits<double>::quiet_NaN();
    // NaN must not silently select the integration-time fallback.
    check(!scheduler.configure(desc), "NaN sensor rate accepted");
    simcore::SensorRuntime runtime;
    simcore::SensorRuntime::Cadence image;
    image.sensor_id=2; image.timeline=simcore::PeriodicTimeline(25); image.exposure=8000000;
    runtime.add(image);
    auto coherent=image;
    coherent.sensor_id=1; coherent.timeline=simcore::PeriodicTimeline(1000).dividedBy(128);
    coherent.exposure=0; coherent.contiguous_windows=true; coherent.samples=128;
    runtime.add(coherent);
    std::vector<simcore::SensorRuntime::Measurement> measurements;
    do {
        auto batch=runtime.advanceTo(simcore::nanosecondsPerSecond,3);
        check(batch.size()<=3,"runtime ignored backend work budget");
        measurements.insert(measurements.end(),batch.begin(),batch.end());
    } while(runtime.backlogged());
    check(measurements.size()==32 && runtime.frames(2)==25 && runtime.frames(1)==7,
          "multi-instance runtime lost an exposure or CPI");
    simcore::TimePoint previous=0;
    for(const auto& event:measurements) {
        check(event.end>=previous && event.start>=0 && event.end>event.start,"runtime measurement window/order");
        check(event.end-event.start==(event.sensor_id==1 ? 128000000 : 8000000),"runtime changed exposure/CPI duration");
        previous=event.end;
    }
    check(runtime.advanceTo(simcore::nanosecondsPerSecond).empty(),"runtime redelivered completed measurements");
    bool rewound=false;
    try { runtime.advanceTo(0); } catch(const std::invalid_argument&) { rewound=true; }
    check(rewound,"runtime accepted a clock rewind");
}

void geodesy() {
    using namespace simcore::geo;
    for (double lat : {-90.0, -89.999, -45.0, 0.0, 25.078, 89.999, 90.0})
        for (double lon : {-180.0, -90.0, 0.0, 121.772, 180.0})
            for (double height : {-500.0, 0.0, 1000.0, 1000000.0}) {
                const auto ecef = geodeticToECEF(lat, lon, height);
                const auto geo = ecefToGeodetic(ecef.x, ecef.y, ecef.z);
                const auto roundtrip = geodeticToECEF(geo.x, geo.y, geo.z);
                check(vsg::length(roundtrip - ecef) < 1e-3, "WGS84 roundtrip exceeds 1mm");
                check(std::abs(geo.z - height) < 1e-3, "ellipsoid height unstable at poles");
            }
    vsg::dvec3 hit;
    check(rayIntersectEllipsoid({GeoAnchor::kEarthA + 1000, 0, 0}, {-1, 0, 0}, hit),
          "normalized earth ray incorrectly rejected");
    check(std::abs(hit.x - GeoAnchor::kEarthA) < 1e-6, "earth ray intersection distance");
    check(!rayIntersectEllipsoid({GeoAnchor::kEarthA + 1000, 0, 0}, {1, 0, 0}, hit), "backward earth intersection");
    check(!rayIntersectEllipsoid({GeoAnchor::kEarthA + 1000, 0, 0}, {0, 0, 0}, hit), "zero direction accepted");
    check(rayIntersectEllipsoid({0, 0, 0}, {1, 0, 0}, hit), "interior ray intersection");
    GeoAnchor anchor; anchor.lat_deg = 0; anchor.lon_deg = 0;
    anchor.heading_deg = 90;
    const auto rotated = makeEnuToWorldMatrix(anchor);
    const auto north = rotated * vsg::dvec4(0, 0, -1, 0);
    check(vsg::length(vsg::dvec3(north.x, north.y, north.z) - geoEast(0, 0)) < 1e-12,
          "grid heading does not rotate clockwise from north");
    const vsg::dvec3 point(1200, 45, -900);
    const auto restored = ecefToLocal(anchor, localToECEF(anchor, point));
    check(vsg::length(restored - point) < 1e-6,
          "local frame loses precision");
}

void eventClockAndTruth() {
    using namespace rockyenv::sensor;
    for (double rate : {25.0, 60.0, 144.0, 1000.0}) {
        SensorDesc desc; desc.frame_kind = "MWIR"; desc.rate_hz = rate; desc.integration_ms = .5;
        SensorScheduler scheduler;
        check(scheduler.configure(desc), "event scheduler configuration");
        simcore::EventClock clock;
        uint64_t count = 0, physicsHits = 0;
        simcore::PeriodicTimeline physics(60);
        while (clock.now() <= 2 * simcore::nanosecondsPerSecond) {
            const auto events = scheduler.update(simcore::toSeconds(clock.now()));
            check(events.size() <= 1 && !scheduler.backlogged(), "GPU evaluation reused for multiple deadlines");
            for (const auto& e : events) {
                check(simcore::fromSeconds(e.t) == clock.now(), "measurement rendered at wrong simulation time");
                ++count;
            }
            if (clock.now() == physics.at(physicsHits + 1)) ++physicsHits;
            clock.advance(scheduler.nextDeadline());
        }
        check(count == uint64_t(rate * 2) && physicsHits == 120,
              "measurement deadlines shifted physics or dropped measurements");
    }
    StateRingBuffer truth(3, 0.0);
    StateSample out; out.t = 99;
    check(!truth.sample(0, out) && out.t == 99 && std::isnan(truth.newestT()), "empty truth query");
    check(truth.push({0, {0, 0, 0}, {0, 0, 0}}), "initial truth rejected");
    check(truth.push({1, {1, 0, 0}, {2, 0, 0}}), "accelerating truth rejected");
    check(truth.sample(.5, out) && std::abs(out.pos.x - .25) < 1e-12 &&
          std::abs(out.vel.x - 1) < 1e-12, "trajectory position/velocity inconsistent under acceleration");
    check(!truth.sample(2, out) && out.t == .5, "stale truth returned as fresh");
    check(truth.push({1, {2, 0, 0}, {2, 0, 0}}) && truth.size() == 2, "duplicate truth not replaced");
    check(truth.sample(1, out) && out.pos.x == 2, "truth replacement not visible");
    check(!truth.push({.9, {}, {}}) && !truth.push({std::numeric_limits<double>::quiet_NaN(), {}, {}}),
          "invalid/out-of-order truth accepted");
    truth.push({2, {4, 0, 0}, {2, 0, 0}}); truth.push({3, {6, 0, 0}, {2, 0, 0}});
    check(truth.size() == 3 && truth.oldestT() == 1 && !truth.sample(0, out), "ring wraparound corrupted history");
    StateRingBuffer predicted(2, .25);
    predicted.push({0, {}, {2, 0, 0}});
    check(predicted.sample(.2, out) && out.t == .2 && std::abs(out.pos.x - .4) < 1e-12,
          "explicit bounded truth prediction");
    check(!predicted.sample(.3, out), "truth prediction exceeded explicit horizon");
}

void wireFrames() {
    vsgraysim::RadarFrame source, decoded;
    source.frame_id = 0; source.sensor_id = 7;
    source.range_bins = source.doppler_bins = 2;
    source.range_min = 100; source.range_max = 8000;
    source.sample_rate_hz = 30e6; source.beam_az_deg = 123.5;
    source.rd_map = {{1, 2}, {-3, 4}, {5, -6}, {0, 0}};
    const auto encoded = vsgraysim::wire::encodeRadarFrame(source);
    check(vsgraysim::wire::decodeRadarFrame(encoded.data(), encoded.size(), decoded), "radar wire roundtrip");
    check(decoded.frame_id == 0 && decoded.rd_map == source.rd_map && decoded.beam_az_deg == 123.5 &&
          decoded.sample_rate_hz == 30e6, "radar wire lost measurement axes");
    check(std::abs(decoded.rangeAtBin(1) - 104.99654096666667) < 1e-10, "distance axis includes FFT padding");
    check(vsgraysim::wire::decodeRadarFrame(encoded.data(), encoded.size() - 16, decoded), "legacy radar compatibility");
    check(decoded.sample_rate_hz == 0, "legacy frame invented a sampling clock");
    for (size_t n = 0; n < encoded.size(); ++n) {
        if (n == encoded.size() - 16) continue; // complete legacy payload
        check(!vsgraysim::wire::decodeRadarFrame(encoded.data(), n, decoded), "truncated radar frame accepted");
    }
    auto corrupt = encoded;
    for (size_t i = 120; i < 128; ++i) corrupt[i] = 255; // hostile dimensions must not overflow
    check(!vsgraysim::wire::decodeRadarFrame(corrupt.data(), corrupt.size(), decoded), "malformed dimensions accepted");
    source.sim_time=.8;
    source.receiver=vsgraysim::RadarFrame::Receiver{.64,.035};
    const auto withReceiver=vsgraysim::wire::encodeRadarFrame(source);
    check(vsgraysim::wire::decodeRadarFrame(withReceiver.data(),withReceiver.size(),decoded)&&decoded.receiver&&
          decoded.receiver->first_transmit_s==.64&&decoded.receiver->noise_iq_rms==.035&&decoded.rd_map==source.rd_map,
          "receiver metadata changed the complex samples or was lost in transport");
    check(vsgraysim::wire::decodeRadarFrame(withReceiver.data(),withReceiver.size()-20,decoded)&&!decoded.receiver,
          "earlier radar transport invented receiver metadata");
    source.ground=std::vector<vsgraysim::RadarFrame::GroundSample>{{98.f,128.f,.125f}};
    const auto withGround=vsgraysim::wire::encodeRadarFrame(source);
    check(vsgraysim::wire::decodeRadarFrame(withGround.data(),withGround.size(),decoded)&&decoded.ground&&
          decoded.ground->size()==1&&(*decoded.ground)[0].slant_range_m==128.f&&
          (*decoded.ground)[0].bearing_deg==98.f&&(*decoded.ground)[0].power_relative==.125f&&decoded.receiver,
          "radar ground samples lost calibration in transport");
    check(!vsgraysim::wire::decodeRadarFrame(withGround.data(),withGround.size()-1,decoded),"truncated ground layer accepted");
}
} // namespace

int main() {
    try {
        sampling(); dispatch(); clocks(); geodesy(); eventClockAndTruth(); wireFrames();
        std::cout << "PASS: sampling, matched filtering, delivery, clocks, scheduling, truth, WGS84 and wire frames\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
