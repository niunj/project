#include "vsgraysim/kernel/TimingBus.h"
#include "vsgraysim/broker/SimDataBroker.h"
#include "vsgraysim/broker/FrameRecorder.h"
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool c, const char* m) { if (!c) throw std::runtime_error(m); }
void recordingStages() {
    using namespace vsgraysim;
    struct Scratch {
        std::string path="/tmp/vsgraysim_record_XXXXXX";
        Scratch() { int fd=mkstemp(path.data()); check(fd>=0,"recording fixture"); close(fd); }
        ~Scratch() { std::remove(path.c_str()); }
    } current,legacy;
    FrameRecorder recorder; check(recorder.start(current.path),"recording start");
    SignalFrame frame; frame.resize(1,1); frame.samples[0]={3,4}; frame.product_stage=0; frame.surface_revision=41;
    check(recorder.recordFrame(frame),"P0 recording");
    frame.frame_id=1; frame.product_stage=2; check(recorder.recordFrame(frame),"P2 recording"); recorder.stop();
    FramePlayer player; check(player.open(current.path)==2,"current recording header");
    SignalFrame result; check(player.nextFrame(&result) && result.product_stage==0 && result.surface_revision==41 && result.samples==frame.samples,"P0 replay lost units");
    check(player.seek(1) && player.nextFrame(&result) && result.product_stage==2,"P2 replay lost units");
    check(player.seek(0) && player.nextFrame(&result) && result.product_stage==0,"recording seek lost stage byte alignment");
    std::ifstream input(current.path,std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input),{}};
    const uint32_t version=1; const uint64_t count=1;
    std::memcpy(bytes.data()+4,&version,4); std::memcpy(bytes.data()+8,&count,8);
    bytes.erase(88,9); bytes.resize(96); // legacy header + one frame head + IQ
    { std::ofstream output(legacy.path,std::ios::binary); output.write(bytes.data(),bytes.size()); }
    check(player.open(legacy.path)==1 && player.nextFrame(&result) && result.product_stage==4 && result.samples==frame.samples,
        "v1 recording compatibility lost");
}
class Probe : public vsgraysim::ISimulationSubsystem {
public:
    explicit Probe(bool substeps) : substeps_(substeps) {}
    std::string name() const override { return substeps_ ? "physics" : "sensor"; }
    bool initialize(const nlohmann::json&) override { return true; }
    void shutdown() override {}
    void onPreTick(const vsgraysim::TimeTick&) override { ++pre; }
    void onTick(const vsgraysim::TimeTick& t) override { ++main; elapsed += t.dt; }
    void onPostTick(const vsgraysim::TimeTick&) override { ++post; }
    bool usesSubSteps() const override { return substeps_; }
    void onSubStep(const vsgraysim::TimeTick& t, int) override {
        check(t.sim_time > previous, "substep time is not monotonic");
        previous = t.sim_time; elapsed += t.dt; ++sub;
    }
    void* getMutableProduct(const std::string&) override { return nullptr; }
    const void* getConstProduct(const std::string&) const override { return nullptr; }
    int tickPriority() const override { return 0; }
    int pre = 0, main = 0, post = 0, sub = 0;
    double elapsed = 0, previous = 0;
private:
    bool substeps_;
};
}
int main() {
    try {
        using namespace vsgraysim;
        recordingStages();
        TimingBus bus;
        check(!bus.tick(), "stopped kernel reported an active tick");
        bus.run(); // a stopped kernel must return, not spin forever
        bool rejected = false;
        try { bus.configureVariableStep(.1, 60); }
        catch (const std::logic_error&) { rejected = true; }
        check(rejected && bus.config().fixed_timestep, "unsupported wall-clock mode silently accepted");
        rejected = false;
        try { bus.setTimeScale(2); }
        catch (const std::logic_error&) { rejected = true; }
        check(rejected, "unsupported time scale silently accepted");
        bus.configureFixedStep(1.0 / 60, 4);
        auto physics = std::make_shared<Probe>(true), sensor = std::make_shared<Probe>(false);
        bus.registerSubsystem(physics, -10); bus.registerSubsystem(sensor);
        int spawned = 0, destroyed = 0;
        bus.subscribe(EventType::EntitySpawned, [&](const SimulationEvent&) {
            ++spawned;
            SimulationEvent next; next.type = EventType::EntityDestroyed;
            bus.publish(next); // callbacks must be allowed to publish without deadlock
        });
        bus.subscribe(EventType::EntityDestroyed, [&](const SimulationEvent&) { ++destroyed; });
        bus.start();
        SimulationEvent event; event.type = EventType::EntitySpawned; bus.publish(event);
        check(bus.tick() && bus.tick(), "kernel stopped unexpectedly");
        check(spawned == 1 && destroyed == 1, "reentrant event delivery");
        check(physics->sub == 8 && physics->main == 0, "physics substep count");
        check(sensor->main == 2 && sensor->sub == 0, "frame system lost when substeps enabled");
        check(std::abs(sensor->elapsed - physics->elapsed) < 1e-12, "physics and sensor elapsed time differ");
        check(std::abs(bus.context().tick.sim_time - bus.currentTick().sim_time) < 1e-12, "stale frame context");
        const double t = bus.currentTick().sim_time;
        bus.setPaused(true); bus.tick();
        check(bus.currentTick().sim_time == t, "paused kernel advanced");
        bus.setPaused(false); bus.tick(); bus.stop();
        check(sensor->main == 3, "resume did not advance once");
        bus.setMaxFrames(4); bus.start();
        check(!bus.tick() && !bus.isRunning() && sensor->main == 4, "frame limit failed to stop the kernel");
        auto& broker = SimDataBroker::instance();
        broker.serverOnline(true);
        IrFrame image; image.sensor_id = 9001; image.frame_id = 0;
        image.width = image.height = 2; image.radiance = {1, 2, 3, 4};
        check(broker.publishIr(image), "broker rejected valid first frame");
        IrFrame received;
        check(broker.latestIr(image.sensor_id, &received) && received.radiance == image.radiance,
              "broker used frame zero as an empty-channel sentinel");
        image.radiance.pop_back();
        check(!broker.publishIr(image), "broker accepted a truncated image");
        broker.serverOnline(false);
        std::cout << "PASS: substeps, frame systems, pause/resume and reentrant events\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
