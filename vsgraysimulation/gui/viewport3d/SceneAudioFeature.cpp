#include "SceneAudioFeature.h"
#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "VoxelSceneFeature.h"
#include "VsgQtWidget.h"
#include "../audio/SceneAudioOutput.h"
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/SceneAcoustics.h>
#pragma push_macro("slots")
#undef slots
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#pragma pop_macro("slots")

#include <simcore/GeoFrame.h>
#include <vsg/app/Camera.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/maths/transform.h>
#include <QVariant>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vsgraysim::viewport3d {
namespace {
struct AcousticInput {
    earth::VoxelSceneSnapshotPtr scene;
    nlohmann::json data;
    std::string epoch, probe;
    vsg::dvec3 listener, velocity, right;
    bool playing=false;
};

struct AcousticOutput {
    audio::MixFrame mix;
    std::string reading;
};

class AcousticWorker {
public:
    AcousticWorker():worker_([this]{run();}){}
    ~AcousticWorker(){
        {std::lock_guard lock(mutex_);stopping_=true;}
        wake_.notify_one();worker_.join();
    }
    void submit(AcousticInput input){
        {std::lock_guard lock(mutex_);request_=std::move(input);}
        wake_.notify_one();
    }
    std::optional<AcousticOutput> take(){
        std::lock_guard lock(mutex_);
        return std::exchange(result_,{});
    }
private:
    void run(){
        earth::SceneAcoustics solver;
        earth::AcousticMeter meter;
        std::string meterKey,epoch;
        double previous=-1.;
        for(;;){
            AcousticInput input;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock,[this]{return stopping_||request_.has_value();});
                if(stopping_)return;
                input=std::move(*request_);request_.reset();
            }
            try{
                solver.ingest(*input.scene,input.data,input.epoch);
                const auto reading=solver.listen(*input.scene,input.listener,input.velocity);
                const auto time=input.scene->timeSeconds();
                if(meterKey!=input.probe||epoch!=input.epoch||time<previous){meter={};meterKey=input.probe;epoch=input.epoch;}
                previous=time;
                meter.observe(time,reading);
                auto result=reading.encode();
                result.update(meter.encode());
                result["sample_s"]=time;
                result["probe"]=input.probe;
                auto legacy=input.data;
                for(const auto* field:{"loops","events"})if(legacy.contains(field)){
                    auto& list=legacy.at(field);
                    for(auto it=list.begin();it!=list.end();)
                        if(it->contains("spl_bands_db"))it=list.erase(it);
                        else ++it;
                }
                const auto& p=input.listener;
                const model_sdk::Vec3 right{input.right.x,input.right.y,input.right.z};
                auto mix=audio::spatialFrame(legacy,input.epoch,input.playing,{p.x,p.y,p.z},right);
                std::lock_guard lock(mutex_);
                result_=AcousticOutput{audio::acousticFrame(reading,std::move(mix),right),result.dump()};
            }catch(const std::exception&){continue;}
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<AcousticInput> request_;
    std::optional<AcousticOutput> result_;
    bool stopping_=false;
    std::thread worker_;
};
}

struct SceneAudioFeature::Impl {
    std::optional<earth::VoxelEquipmentConfig> preview;
    earth::VoxelSceneSnapshotPtr previous;
    nlohmann::json localPreview,localBroker;
    nlohmann::json acoustics;
    std::unique_ptr<AcousticWorker> worker;
    std::unique_ptr<audio::SceneAudioOutput> output;
    vsg::dvec3 previousPosition{};
    double previousTime=-1.;
    std::string previousProbe;
    std::chrono::steady_clock::time_point lastRequest;
};

SceneAudioFeature::SceneAudioFeature():impl_(std::make_unique<Impl>()){}
SceneAudioFeature::~SceneAudioFeature()=default;

void SceneAudioFeature::configure(const nlohmann::json& configuration) {
    if(configuration.contains("preview_equipment"))
        impl_->preview=earth::VoxelEquipmentConfig::parse(configuration.at("preview_equipment"));
    else if(configuration.contains("acoustics")){
        impl_->preview=earth::VoxelEquipmentConfig{};
        impl_->preview->visualOnly=true;
    }
    else
        impl_->preview.reset();
    impl_->previous.reset();
    impl_->localPreview=nullptr;
    impl_->acoustics=configuration.value("acoustics",nlohmann::json());
    impl_->worker.reset();impl_->previousTime=-1.;
}

void SceneAudioFeature::publishLocalBrokerState(nlohmann::json data) {
    impl_->localBroker=std::move(data);
}

void SceneAudioFeature::onDetach(ViewportContext& ctx) {
    impl_->output.reset();
    impl_->worker.reset();
    impl_->previous.reset();
    impl_->localBroker=nullptr;
    impl_->localPreview=nullptr;
    if(ctx.widget())ctx.widget()->setProperty("sceneAudioOutputActive",false);
}

void SceneAudioFeature::onFrame(ViewportContext& ctx,const FrameTick&) {
    auto* widget=ctx.widget();
    const auto* feature=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    const auto state=feature?feature->snapshot():nullptr;
    const bool requested=widget&&widget->isVisible()&&widget->property("sceneAudioRequested").toBool();
    if(!requested||!state){
        if(impl_->output)impl_->output.reset();
        if(impl_->worker)impl_->worker.reset();
        impl_->previousTime=-1.;
        impl_->previous.reset();
        if(widget){
            if(widget->property("sceneAudioOutputActive").toBool())widget->setProperty("sceneAudioOutputActive",false);
            if(widget->property("sceneAcousticsReading").isValid())widget->setProperty("sceneAcousticsReading",QVariant());
        }
        return;
    }
    const auto* data=&state->scenePresentation;
    if(feature->runtime()&&impl_->preview){
        if(impl_->previous!=state){
            const auto equipment=earth::voxelEquipmentStateTelemetry(*impl_->preview,*state);
            auto previous=impl_->previous;
            auto history=std::move(impl_->localPreview);
            if(previous&&state->tick>previous->tick+1){
                for(const auto& snapshot:feature->runtime()->history()){
                    if(snapshot->tick<=previous->tick||snapshot->tick>=state->tick)continue;
                    if(!snapshot->contacts.empty()){
                        auto intermediate=earth::scenePresentation(*impl_->preview,*snapshot,equipment,previous.get());
                        earth::retainSceneSoundEvents(intermediate,history,*snapshot);
                        history=std::move(intermediate);
                    }
                    previous=snapshot;
                }
            }
            impl_->localPreview=earth::scenePresentation(*impl_->preview,*state,equipment,previous.get());
            earth::retainSceneSoundEvents(impl_->localPreview,history,*state);
            earth::appendAcousticSources(impl_->acoustics,*state,impl_->localPreview);
            impl_->previous=state;
        }
        data=&impl_->localPreview;
    }else if(data->is_null()){
        data=&impl_->localBroker;
    }
    const nlohmann::json environmentOnly={{"tick",state->tick},{"revision",state->revision}};
    if(state->ocean&&data->is_null())data=&environmentOnly;
    if(!data->is_object()||!data->contains("tick")||data->at("tick")!=state->tick||
        !data->contains("revision")||data->at("revision")!=state->revision)return;
    const bool playing=feature->runtime()?!feature->runtime()->paused():widget->property("sceneAudioPlaying").toBool();
    const auto epoch=widget->property("sceneAudioEpoch").toString().toStdString();
    if(!playing&&impl_->output)impl_->output->submit({epoch,state->tick,false,{}});
    const auto lookAt=ctx.camera()?ctx.camera()->viewMatrix.cast<vsg::LookAt>():nullptr;
    if(!lookAt)return;
    const auto fromWorld=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(state->anchor));
    auto listener=fromWorld*lookAt->eye;
    const auto direction=vsg::cross(lookAt->center-lookAt->eye,lookAt->up);
    vsg::dvec4 right{};
    if(vsg::length(direction)>1e-10)right=fromWorld*vsg::dvec4(vsg::normalize(direction),0.);
    auto probe=widget->property("sceneAcousticProbe").toString().toStdString();
    bool fixed=false;
    if(const auto probes=data->find("acoustic_probes");probes!=data->end()&&probes->is_array()){
        for(const auto& row:*probes)if(row.at("id")==probe){
            const auto p=row.at("position_m").get<std::array<double,3>>();
            listener={p[0],p[1],p[2]};fixed=true;break;
        }
    }
    if(!fixed)probe="camera";
    if(!impl_->worker)impl_->worker=std::make_unique<AcousticWorker>();
    const auto wall=std::chrono::steady_clock::now();
    if(wall-impl_->lastRequest>=std::chrono::milliseconds(45)){
        vsg::dvec3 velocity{};
        const auto time=state->timeSeconds(),dt=time-impl_->previousTime;
        if(!fixed&&impl_->previousProbe==probe&&impl_->previousTime>=0.&&dt>0.&&dt<.5){
            velocity=(listener-impl_->previousPosition)/dt;
            if(vsg::length(velocity)>100.)velocity={};
        }
        impl_->previousTime=time;impl_->previousPosition=listener;impl_->previousProbe=probe;
        impl_->worker->submit({state,*data,epoch,probe,listener,velocity,{right.x,right.y,right.z},playing});
        impl_->lastRequest=wall;
    }
    if(auto result=impl_->worker->take();result&&result->mix.epoch==epoch){
        widget->setProperty("sceneAcousticsReading",QString::fromStdString(result->reading));
        if(!impl_->output&&playing&&result->mix.playing&&!result->mix.voices.empty())impl_->output=std::make_unique<audio::SceneAudioOutput>();
        if(impl_->output&&playing&&result->mix.playing)impl_->output->submit(std::move(result->mix));
    }
    if(impl_->output){
        const bool active=impl_->output->active();
        if(widget->property("sceneAudioOutputActive").toBool()!=active)widget->setProperty("sceneAudioOutputActive",active);
    }
}

VSGVP_REGISTER_FEATURE(SceneAudioFeature);
}
