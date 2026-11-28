#include "ServiceProducts.h"
#include <vsgraysim/broker/SimDataBroker.h>
#include <QByteArray>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim::service {
namespace {
using json=nlohmann::json;
json image(const std::vector<float>& values,uint32_t width,uint32_t height,uint8_t stage) {
    if(!width||!height||values.size()!=size_t(width)*height||values.size()>2048u*2048u)return nullptr;
    // Bound the preview transport while retaining the original product's dimensions and stage.
    const uint32_t stride=std::max(1u,(std::max(width,height)+511)/512);
    const uint32_t outW=(width+stride-1)/stride,outH=(height+stride-1)/stride;
    float lo=0,hi=1;
    if(stage!=4){
        lo=std::numeric_limits<float>::infinity();hi=-lo;
        for(const float v:values)if(std::isfinite(v)){lo=std::min(lo,v);hi=std::max(hi,v);}
    }
    const double range=double(hi)-lo;
    QByteArray gray(int(outW*outH),0);
    if(std::isfinite(range)&&range>0)for(uint32_t y=0;y<outH;++y)for(uint32_t x=0;x<outW;++x){
        const auto value=values[size_t(y*stride)*width+x*stride];
        if(std::isfinite(value))gray[int(y*outW+x)]=char(std::lround(255*std::clamp((double(value)-lo)/range,0.,1.)));
    }
    return {{"pixel_format","gray8"},{"width",outW},{"height",outH},{"source_width",width},{"source_height",height},
        {"mapping",stage==4?"display_normalized":"display_minmax"},{"pixels_base64",gray.toBase64().toStdString()}};
}

template<class Frame>
json metadata(const Frame& frame,const char* kind) {
    return {{"kind",kind},{"frame_id",std::to_string(frame.frame_id)},{"platform_id",frame.platform_id},
        {"equipment_id",frame.sensor_id},{"sim_time_s",frame.sim_time},
        {"content_revision",std::to_string(frame.surface_revision)},{"stage",frame.stage}};
}

json preview(uint32_t id,const json& platforms,const json& navigation,
             uint64_t tick,uint64_t revision,double time) {
    auto& broker=SimDataBroker::instance();
    if(const auto frame=broker.sonarSnapshot(id)){
        auto out=metadata(*frame,"depth_sounder");
        out.update({{"water_depth_m",frame->water_depth_m},{"clearance_m",frame->clearance_m},
            {"bottom",unsigned(frame->bottom)},{"two_way_ms",frame->two_way_ms}});
        return out;
    }
    for(const auto& device:navigation)if(device.at("id")==id){
        for(const auto& platform:platforms)if(platform.at("platform_id")==device.at("platform_id")){
            const auto& nav=platform.at("navigation");
            return {{"kind","navigation"},{"frame_id",std::to_string(tick)},{"equipment_id",id},
                {"platform_id",platform.at("platform_id")},{"sim_time_s",time},
                {"content_revision",std::to_string(revision)},{"navigation",nav}};
        }
        return nullptr;
    }
    VisFrame vis;
    if(broker.latestVis(id,&vis)&&!vis.empty()){
        auto out=metadata(vis,"visible");
        out["image"]=image(vis.intensity,vis.width,vis.height,vis.stage);
        return out;
    }
    IrFrame ir;
    if(broker.latestIr(id,&ir)&&!ir.empty()){
        auto out=metadata(ir,"infrared");
        out["image"]=image(ir.radiance,ir.width,ir.height,ir.stage);
        return out;
    }
    return nullptr;
}
}

void refreshPreviewProducts(const std::filesystem::path& directory,const std::set<uint32_t>& desired,
                            const json& platforms,const json& navigation,
                            uint64_t tick,uint64_t revision,double time) {
    for(const auto id:desired) {
        const auto info=EquipmentRegistry::instance().findById(id);
        if(info&&!info->powered)continue;
        const auto product=preview(id,platforms,navigation,tick,revision,time);
        if(product.is_null())continue;
        QSaveFile file(QString::fromStdString((directory/("frame-"+std::to_string(id)+".json")).string()));
        const auto body=QByteArray::fromStdString(product.dump());
        if(file.open(QIODevice::WriteOnly)&&file.write(body)==body.size())file.commit();
    }
}
}
