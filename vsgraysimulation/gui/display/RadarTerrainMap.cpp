#include "RadarTerrainMap.h"
#include <vsgraysim/earth/EarthDataService.h>

#include <QThreadPool>
#include <atomic>

namespace vsgraysim::gui::display {

struct RadarTerrainMap::Load {
    std::string path;
    earth::EarthSurfaceSnapshotPtr surface;
    QImage image;
    QString error;
    std::atomic<bool> ready=false;
};

RadarTerrainMap::RadarTerrainMap(QWidget* parent):ScrollableImageView(parent) {
    setMinimumSize(220,164);
    setObjectName("radarTerrainMap");
    setProperty("terrainState",tr("等待场景底图"));
    connect(this,&ScrollableImageView::cursorMoved,this,[this](int x,int y){
        const auto surface=surface_;
        if(!surface||x<0||y<0){setToolTip({});return;}
        const auto east=surface->min_x+(x+.5)*(surface->max_x-surface->min_x)/surface->imagery_width;
        const auto south=surface->min_z+(y+.5)*(surface->max_z-surface->min_z)/surface->imagery_height;
        const auto sample=surface->sampleLocal(east,south);
        if(sample.status==earth::SurfaceStatus::NoData){setToolTip(tr("此处缺少高程"));return;}
        const auto point=simcore::geo::localToECEF(surface->anchor,{east,sample.height,south});
        const auto llh=simcore::geo::ecefToGeodetic(point.x,point.y,point.z);
        const auto range=vsg::length(point-station_);
        setToolTip(tr("WGS84 %1°, %2° · 参考椭球高 %3 m · 至站斜距 %4 m")
            .arg(llh.x,0,'f',5).arg(llh.y,0,'f',5).arg(llh.z,0,'f',1).arg(range,0,'f',1));
    });
}

void RadarTerrainMap::setSource(const std::string& path) {
    if(path==source_)return;
    source_=path;
    clearStation();
}

void RadarTerrainMap::clearStation() {
    inFlight_.reset();
    surface_.reset();
    attempt_.reset();
    radius_=0;
    clear();
    setProperty("terrainState",source_.empty()?tr("场景未提供地球底图"):tr("等待雷达站位"));
    setProperty("terrainReady",false);
    setToolTip({});
}

void RadarTerrainMap::updateStation(const RadarFrame& frame) {
    if(source_.empty()||!std::isfinite(frame.latitude)||std::abs(frame.latitude)>90.||
       !std::isfinite(frame.longitude)||!std::isfinite(frame.altitude))return;

    station_=simcore::geo::geodeticToECEF(frame.latitude,frame.longitude,frame.altitude);
    if(inFlight_&&inFlight_->ready.load(std::memory_order_acquire)) {
        if(inFlight_->surface){
            surface_=inFlight_->surface;
            setImage(inFlight_->image);
            setProperty("terrainReady",true);
            const double nativeImagery=surface_->imagery_spacing_m;
            setProperty("terrainState",tr("场景地物参考图 · 北向上 · 影像采样 %1 m · 高程采样 %2 m")
                .arg(nativeImagery,0,'f',0).arg(surface_->dem_spacing_m,0,'f',0));
        }else setProperty("terrainState",tr("地物图层载入失败 · %1").arg(inFlight_->error));
        inFlight_.reset();
    }
    const auto center=simcore::geo::GeoAnchor{frame.latitude,frame.longitude,0.,0.};
    // Short-range experiments still need enough surrounding terrain for orientation.
    const double radius=std::clamp(1000.*std::ceil(double(frame.range_max)/1000.),2000.,20000.);
    const auto movement=attempt_?simcore::geo::ecefToLocal(*attempt_,station_):vsg::dvec3{};
    if(!inFlight_&&(!attempt_||std::hypot(movement.x,movement.z)>radius_*.3||radius!=radius_)) {
        attempt_=center;
        radius_=radius;
        inFlight_=std::make_shared<Load>();
        inFlight_->path=source_;
        if(!surface_)setProperty("terrainState",tr("正在载入场景地物图层"));
        QThreadPool::globalInstance()->start([job=inFlight_,center,radius]{
            try {
                auto surface=earth::EarthDataService::loadMap(job->path,center,{
                    {"grid_size",81},{"imagery_size",321},{"coverage",{
                        {"min_x",-radius},{"max_x",radius},{"min_z",-radius},{"max_z",radius}}}});
                if(!surface->imagery_width||!surface->imagery_height)
                    throw std::runtime_error("scene map has no georeferenced imagery");
                job->image=QImage(surface->imagery_rgba.data(),int(surface->imagery_width),int(surface->imagery_height),QImage::Format_RGBA8888).copy();
                job->surface=std::move(surface);
            }catch(const std::exception& error){job->error=QString::fromUtf8(error.what());}
            job->ready.store(true,std::memory_order_release);
        });
    }
    if(!surface_)return;
    const auto local=simcore::geo::ecefToLocal(surface_->anchor,station_);
    const auto w=surface_->imagery_width,h=surface_->imagery_height;
    const double span=surface_->max_x-surface_->min_x;
    const double x=(local.x-surface_->min_x)*w/span,y=(local.z-surface_->min_z)*h/span;
    const auto beamRad=simcore::geo::deg2rad(frame.beam_az_deg);
    const auto direction=simcore::geo::geoEast(frame.latitude,frame.longitude)*std::sin(beamRad)+
        vsg::cross(simcore::geo::geoUp(frame.latitude,frame.longitude),simcore::geo::geoEast(frame.latitude,frame.longitude))*std::cos(beamRad);
    const auto beamLocal=simcore::geo::ecefToLocal(surface_->anchor,station_+direction)-local;
    const double length=.28*std::min(w,h);
    const QColor color("#70f3dd");
    setOverlays({
        {Overlay::Kind::Text,.05*w,.10*h,0,0,0,0,tr("↑ 北"),color},
        {Overlay::Kind::Line,x,y,0,0,x+length*beamLocal.x,y+length*beamLocal.z,{},color},
        {Overlay::Kind::Cross,x,y,0,0,0,0,tr("站位"),color},
        {Overlay::Kind::Line,.07*w,.92*h,0,0,.07*w+w*1000./span,.92*h,tr("1 km"),color}
    });
}

} // namespace vsgraysim::gui::display
