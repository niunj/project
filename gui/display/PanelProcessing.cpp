#include "PanelProcessing.h"
#include "TrackManager.h"
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/layer3b_signal/SensorSignalProcessor.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>

namespace vsgraysim::gui::display {
namespace {
void centerSpectrum(std::vector<std::complex<float>>& spectrum,uint32_t width,uint32_t height) {
    std::rotate(spectrum.begin(),spectrum.begin()+size_t(height/2)*width,spectrum.end());
}
float amplitudeDb(float amplitude) {
    return std::isfinite(amplitude)?20.f*std::log10(std::max(amplitude,1e-12f)):-240.f;
}

void radarRangeProfiles(const RadarFrame& frame,uint32_t pulseIndex,PanelResult& out) {
    if(frame.stage!=0&&frame.stage!=2)return;
    auto bins=out.width;
    if(out.rangeStep>0&&out.rangeGateMax>=out.rangeMin)
        bins=uint32_t(std::clamp(std::floor((out.rangeGateMax-out.rangeMin)/out.rangeStep)+1.,1.,double(bins)));
    out.radialDb.assign(bins,-240.f);
    out.curveDb.resize(bins);
    const auto pulse=std::min(pulseIndex,out.height-1);
    if(frame.stage==0){
        const auto begin=frame.rd_map.begin()+size_t(pulse)*out.width;
        out.curveIq.assign(begin,begin+out.width);
    }
    for(uint32_t bin=0;bin<bins;++bin){
        double sum=0.;float peak=0.f;
        for(uint32_t row=0;row<out.height;++row){
            const auto amplitude=std::abs(frame.rd_map[size_t(row)*out.width+bin]);
            if(!std::isfinite(amplitude))continue;
            sum+=double(amplitude)*amplitude;peak=std::max(peak,amplitude);
        }
        const auto rms=float(std::sqrt(sum/out.height));
        out.radialDb[bin]=amplitudeDb(frame.stage==0?rms:peak);
        if(frame.stage==0&&frame.receiver&&rms<2.5*frame.receiver->noise_iq_rms)
            out.radialDb[bin]=-240.f;
        if(frame.stage==0){
            out.curveDb[bin]=amplitudeDb(std::abs(out.curveIq[bin]));
        }else out.curveDb[bin]=out.radialDb[bin];
    }
}
QImage grayscale(const std::vector<float>& values,uint32_t w,uint32_t h,bool stretch,bool thermal){
    QImage image(int(w),int(h),QImage::Format_RGB32);float lo=0,hi=1;
    if(stretch){lo=std::numeric_limits<float>::infinity();hi=-lo;for(float value:values)if(std::isfinite(value)){lo=std::min(lo,value);hi=std::max(hi,value);}if(!std::isfinite(lo)){lo=0;hi=1;}}
    const float span=hi-lo;for(uint32_t y=0;y<h;++y){auto* row=reinterpret_cast<QRgb*>(image.scanLine(int(y)));for(uint32_t x=0;x<w;++x){const float value=values[size_t(y)*w+x];const float t=std::isfinite(value)?(span>1e-12f?std::clamp((value-lo)/span,0.f,1.f):(value>0?.5f:0.f)):0;
        if(thermal)row[x]=qRgb(int(255*std::clamp(3*t,0.f,1.f)),int(255*std::clamp(3*t-1,0.f,1.f)),int(255*std::clamp(3*t-2,0.f,1.f)));else {const auto level=int(std::lround(t*255));row[x]=qRgb(level,level,level);}}}return image;
}
}
PanelProcessor::PanelProcessor():worker_([this]{run();}){}
PanelProcessor::~PanelProcessor(){{std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}
void PanelProcessor::submit(PanelInput input){std::lock_guard lock(mutex_);if(requestedIdentity_==input.identity&&lastSequence_==input.sequence)return;if(requestedIdentity_!=input.identity)result_.reset();requestedIdentity_=input.identity;lastSequence_=input.sequence;if(pending_)++coalesced_;pending_=std::move(input);wake_.notify_one();}
std::optional<PanelResult> PanelProcessor::take(){std::lock_guard lock(mutex_);auto result=std::move(result_);result_.reset();return result;}
uint64_t PanelProcessor::coalesced() const {std::lock_guard lock(mutex_);return coalesced_;}
void PanelProcessor::run(){
    TrackManager tracks;std::string trackIdentity;double previous=-1;
    for(;;){PanelInput input;{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stop_||pending_.has_value();});if(stop_)return;input=std::move(*pending_);pending_.reset();}
        PanelResult out;out.identity=input.identity;out.sequence=input.sequence;out.workerThread=std::hash<std::thread::id>{}(std::this_thread::get_id());const auto begin=std::chrono::steady_clock::now();
        try{
            auto common=[&](const auto& f,uint32_t w,uint32_t h){out.width=w;out.height=h;out.frame=f.frame_id;out.time=f.sim_time;out.stage=f.stage;};
            if(input.type==SensorType::RADAR){
                auto frame=input.radar;if(!frame||frame->rd_map.empty()){auto owned=std::make_shared<RadarFrame>();if(!SimDataBroker::instance().latestRadar(input.equipment,owned.get()))throw std::runtime_error("radar payload unavailable");frame=owned;}
                const auto& f=*frame;common(f,f.range_bins,f.doppler_bins);if(!out.width||!out.height||uint64_t(out.width)*out.height>(1u<<24)||f.rd_map.size()!=size_t(out.width)*out.height)throw std::runtime_error("invalid RD payload");
                out.receiver=f.receiver;out.sampleRate=f.sample_rate_hz;out.pulseWidth=f.pulse_width;
                out.ground=f.ground;
                out.rangeMin=f.range_min;out.rangeStep=out.width>1?f.rangeAtBin(1)-f.rangeAtBin(0):0;out.prf=f.prf;out.beam=f.beam_az_deg;
                out.rangeGateMax=f.range_max>f.range_min?std::min(double(f.range_max),f.rangeAtBin(out.width-1)):f.rangeAtBin(out.width-1);
                radarRangeProfiles(f,input.pulseIndex,out);
                const bool impulseEcho=f.sample_rate_hz>0&&std::abs(f.pulse_width*f.sample_rate_hz-1.)<1e-5;
                out.localSpectrum=f.stage==0&&input.showRadarImage&&input.showSpectrum&&impulseEcho;
                if(out.localSpectrum) {
                    out.iq=slowTimeSpectrum(f.rd_map,out.width,out.height);
                    centerSpectrum(out.iq,out.width,out.height);
                }else if(f.stage==0&&input.showRadarImage)out.iq=f.rd_map;
                const auto& display=out.localSpectrum?out.iq:f.rd_map;
                std::vector<float> db;
                if(input.showRadarImage||f.stage==2){db.resize(display.size());for(size_t n=0;n<db.size();++n)db[n]=amplitudeDb(std::abs(display[n]));}
                if(trackIdentity!=input.identity||f.sim_time<previous){tracks.clear();trackIdentity=input.identity;}previous=f.sim_time;
                if(f.stage==2){CfarProcessor cfar;cfar.setParams(input.cfar);auto detections=cfar.detect(db,out.width,out.height);std::vector<PpiTarget> measurements;for(const auto& d:detections){PpiTarget p;p.bearing_deg=f.beam_az_deg;p.range_km=f.rangeAtBin(uint32_t(d.col))*.001;p.snr_db=d.snr_db;measurements.push_back(p);}tracks.processDetections(measurements,f.sim_time);out.tracks=tracks.toPpiTargets();
                    if(input.showRadarImage){
                        std::rotate(db.begin(),db.begin()+size_t(out.height/2)*out.width,db.end());
                        out.values=std::move(db);
                    }
                }else {tracks.clear();out.values=std::move(db);}
                if(input.showRadarImage)out.image=grayscale(out.values,out.width,out.height,true,false);
            }else{
                if(input.type==SensorType::IR&&input.ir){
                    const auto& f=*input.ir;common(f,f.width,f.height);out.values=f.radiance;
                    out.fovRadians=f.fov_horizontal;out.exposureSeconds=f.integration_time;
                    out.bandMinMicrons=f.wavelength_min;out.bandMaxMicrons=f.wavelength_max;
                }else if(input.type==SensorType::VIS&&input.vis){
                    const auto& f=*input.vis;common(f,f.width,f.height);out.values=f.intensity;
                    out.fovRadians=f.fov;out.exposureSeconds=f.exposure_time;
                }
                else if(input.type==SensorType::SAR&&input.sar){
                    const auto& f=*input.sar;common(f,f.range_samples,f.azimuth_samples);
                    if(f.hasFocusedIsar())out.isarAxes=f.isar_axes;
                    if(input.focusedIsarOnly&&!out.isarAxes)throw std::runtime_error("等待带距离和横向距离标定的 ISAR 聚焦图像");
                    out.localSpectrum=f.stage==0&&input.showSpectrum&&f.sample_rate_hz>0;
                    out.iq=out.localSpectrum?slowTimeSpectrum(f.echo_data,out.width,out.height):f.echo_data;
                    if(out.localSpectrum)centerSpectrum(out.iq,out.width,out.height);
                    out.values.resize(out.iq.size());for(size_t n=0;n<out.iq.size();++n)out.values[n]=std::abs(out.iq[n]);
                }
                else throw std::runtime_error("image payload unavailable");
                if(!out.width||!out.height||uint64_t(out.width)*out.height>(1u<<24)||out.values.size()!=size_t(out.width)*out.height)throw std::runtime_error("invalid image payload");
                out.minimum=std::numeric_limits<float>::infinity();out.maximum=-out.minimum;for(float value:out.values)if(std::isfinite(value)){out.minimum=std::min(out.minimum,value);out.maximum=std::max(out.maximum,value);}
                auto display=out.values;
                if(out.isarAxes&&input.logarithmic){
                    const auto peakDb=amplitudeDb(out.maximum);
                    const auto span=float(input.isarDynamicRangeDb);
                    for(auto& value:display)value=out.maximum>0.f?std::clamp(1.f+(amplitudeDb(value)-peakDb)/span,0.f,1.f):0.f;
                    out.image=grayscale(display,out.width,out.height,false,input.thermal);
                }else{
                    if(input.type==SensorType::SAR&&input.logarithmic)for(auto& value:display)value=amplitudeDb(value);
                    out.image=grayscale(display,out.width,out.height,input.stretch,input.thermal);
                }
            }
        }catch(const std::exception& e){out.error=e.what();}
        out.processingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        {std::lock_guard lock(mutex_);if(out.identity==requestedIdentity_)result_=std::move(out);}
    }
}
}
