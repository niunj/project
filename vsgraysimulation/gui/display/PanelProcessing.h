#pragma once
#include <vsgraysim/broker/SensorFrames.h>
#include <vsgraysim/core/SensorTypes.h>
#include "CfarProcessor.h"
#include "PpiDisplay.h"
#include <QImage>
#include <condition_variable>
#include <thread>
#include <optional>
#include <mutex>
#include <memory>

namespace vsgraysim::gui::display {
QImage infraredRadianceImage(const std::vector<float>& values, uint32_t width, uint32_t height,
                              double minMicrons, double maxMicrons, bool automatic, bool thermalPalette);
struct PanelInput {
    std::string identity;
    uint64_t sequence=0;
    uint32_t equipment=0;
    SensorType type=SensorType::VIS;
    bool stretch=false,thermal=false,logarithmic=false;
    bool showSpectrum=false;
    bool showRadarImage=false;
    bool focusedIsarOnly=false;
    double isarDynamicRangeDb=45.;
    uint32_t pulseIndex=0;
    CfarProcessor::Params cfar;
    std::shared_ptr<const RadarFrame> radar;
    std::shared_ptr<const IrFrame> ir;
    std::shared_ptr<const VisFrame> vis;
    std::shared_ptr<const SarFrame> sar;
};
struct PanelResult {
    std::string identity,error;
    uint64_t sequence=0,frame=0;
    size_t workerThread=0;
    double time=0,processingMs=0,rangeMin=0,rangeStep=0,rangeGateMax=0,prf=0,beam=0;
    double fovRadians=0, exposureSeconds=0, bandMinMicrons=0, bandMaxMicrons=0;
    uint32_t width=0,height=0;uint8_t stage=0;
    bool localSpectrum=false;
    std::optional<IsarImageAxes> isarAxes;
    std::optional<RadarFrame::Receiver> receiver;
    std::optional<std::vector<RadarFrame::GroundSample>> ground;
    double sampleRate=0.,pulseWidth=0.;
    float minimum=0,maximum=0;
    QImage image;
    std::vector<float> values;
    std::vector<float> radialDb,curveDb;
    std::vector<std::complex<float>> curveIq;
    std::vector<std::complex<float>> iq;
    std::vector<PpiTarget> tracks;
};
class PanelProcessor {
public:
    PanelProcessor();
    ~PanelProcessor();
    void submit(PanelInput);
    std::optional<PanelResult> take();
    uint64_t coalesced() const;
private:
    void run();
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<PanelInput> pending_;
    std::optional<PanelResult> result_;
    std::string requestedIdentity_;
    uint64_t lastSequence_=~uint64_t(0),coalesced_=0;
    bool stop_=false;
    std::thread worker_;
};
}
