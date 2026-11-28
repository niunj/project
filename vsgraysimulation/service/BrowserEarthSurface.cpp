#include "BrowserEarthSurface.h"
#include <vsgraysim/earth/VoxelEarthPatch.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QtEndian>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace vsgraysim::service {
namespace {
using Json = nlohmann::json;
using Paths = std::set<std::string>;
struct Atlas { QByteArray key; std::string file; };
constexpr int divisions = 8;
constexpr int maxChunkSize = 32 * 1024 * 1024;

const earth::VoxelSceneObject* ground(const earth::VoxelSceneSnapshot& scene) {
    for (const auto& [id,object] : scene.objects)
        if (object.role == "terrain" && object.earth) return &object;
    return nullptr;
}

earth::EarthSurfaceSnapshotPtr surfaceOf(const earth::VoxelSceneObject& object,
                                        const earth::VoxelSceneSnapshotPtr& scene) {
    return earth::EarthSurfaceSnapshotPtr(scene,&object.surface());
}

void write(const std::filesystem::path& file, const QByteArray& data) {
    auto pending=file;
    pending+=".pending";
    std::ofstream output(pending,std::ios::binary);
    output.exceptions(std::ios::failbit|std::ios::badbit);
    output.write(data.constData(),data.size());
    output.close();
    std::filesystem::rename(pending,file);
}

std::string store(const std::filesystem::path& dir, const QByteArray& bytes, std::string extension, Paths& keep) {
    if (bytes.isEmpty() || bytes.size() > maxChunkSize) throw std::runtime_error("browser terrain chunk exceeds budget");
    const auto name = QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()+extension;
    const auto path = dir / name;
    if (!std::filesystem::is_regular_file(path)) write(path,bytes);
    keep.insert(name);
    return name;
}

std::string saveImage(const earth::EarthSurfaceSnapshot& terrain,const std::filesystem::path& dir,Paths& keep,Atlas& cache) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(terrain.imagery_width)+"x"+QByteArray::number(terrain.imagery_height));
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(terrain.imagery_rgba.data()),qsizetype(terrain.imagery_rgba.size())));
    const auto key=hash.result();
    if (cache.key==key && std::filesystem::is_regular_file(dir/cache.file)) {
        keep.insert(cache.file);
        return cache.file;
    }
    QImage image;
    const auto pixels = size_t(terrain.imagery_width)*terrain.imagery_height*4;
    if (pixels && pixels == terrain.imagery_rgba.size()) {
        const auto source = QImage(terrain.imagery_rgba.data(),int(terrain.imagery_width),int(terrain.imagery_height),
                                   QImage::Format_RGBA8888);
        image = source;
    } else {
        image = QImage(1,1,QImage::Format_RGB32);
        image.fill(qRgb(142,148,132));
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer,"PNG", 75))
        throw std::runtime_error("cannot encode browser terrain atlas");
    cache={key,store(dir,png,".png",keep)};
    return cache.file;
}

void append(QByteArray& bytes,uint32_t value) {
    const char p[] = {char(value),char(value>>8),char(value>>16),char(value>>24)};
    bytes.append(p,4);
}

Json pack(const earth::VoxelSceneSnapshot& scene,const earth::VoxelSceneObject& object,
          const std::filesystem::path& dir,const std::string& generation,Paths& keep,Atlas& atlas,
          const earth::VoxelSceneObject* previousObject,const Json& previous) {
    const auto& terrain = object.surface();
    const auto& vertices = terrain.vertices();
    const auto& indices = terrain.indices();
    const auto uv = terrain.imagery_uv.size() == vertices.size();
    const auto width = terrain.max_x-terrain.min_x, depth = terrain.max_z-terrain.min_z;
    if (width <= 0 || depth <= 0) throw std::runtime_error("invalid browser terrain bounds");
    std::array<std::vector<uint32_t>,divisions*divisions> groups;
    const auto bin = [](double value,double lo,double extent) {
        return std::clamp(int((value-lo)/extent*divisions),0,divisions-1);
    };
    std::string image;
    if(previousObject && !previous.at("changed").empty() && earth::sameVoxelEarthAtlas(previousObject->surface(),terrain)) {
        const auto& existing=previous.at("changed").front().at("image").get_ref<const std::string&>();
        if(std::filesystem::is_regular_file(dir/existing)){image=existing;keep.insert(image);}
    }
    if(image.empty())image=saveImage(terrain,dir,keep,atlas);
    std::array<bool,divisions*divisions> dirty;
    dirty.fill(true);
    std::array<const Json*,divisions*divisions> retained{};
    const Json bounds={terrain.min_x,terrain.max_x,terrain.min_z,terrain.max_z};
    if(previousObject && previous.at("scene_bounds_m")==bounds) {
        const auto& oldChunks=previous.at("changed");
        if(!oldChunks.empty() && oldChunks.front().at("image")==image) {
            std::array<bool,divisions*divisions> changedChunks{};
            if(visitVoxelEarthChangedColumns(*previousObject,object,[&](const vsg::dbox& changed) {
                const auto x0=bin(changed.min.x,terrain.min_x,width),x1=bin(changed.max.x,terrain.min_x,width);
                const auto z0=bin(changed.min.z,terrain.min_z,depth),z1=bin(changed.max.z,terrain.min_z,depth);
                for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)changedChunks[z*divisions+x]=true;
            })) {
                dirty=changedChunks;
                for(const auto& chunk:oldChunks)
                    retained.at(std::stoul(chunk.at("id").get_ref<const std::string&>()))=&chunk;
            }
        }
    }
    if(std::any_of(dirty.begin(),dirty.end(),[](bool value){return value;}))for (size_t i=0;i+2<indices.size();i+=3) {
        const auto center=(vertices.at(indices[i])+vertices.at(indices[i+1])+vertices.at(indices[i+2]))/3.;
        const auto id=bin(center.z,terrain.min_z,depth)*divisions+bin(center.x,terrain.min_x,width);
        if(dirty[id]) groups[id].insert(groups[id].end(),indices.begin()+i,indices.begin()+i+3);
    }
    auto chunks = Json::array();
    size_t rebuilt=0,reused=0;
    constexpr auto absent=std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> remap(vertices.size(),absent), touched;
    for (size_t id=0;id<groups.size();++id) {
        if(!dirty[id]) {
            if(const auto* chunk=retained[id]) {
                keep.insert(chunk->at("mesh").get_ref<const std::string&>());
                chunks.push_back(*chunk);++reused;
            }
            continue;
        }
        ++rebuilt;
        auto& triangles=groups[id];
        if (triangles.empty()) continue;
        // Fixed chunk origins keep unchanged terrain byte-identical after a local edit.
        const vsg::dvec3 center(terrain.min_x+(id%divisions+.5)*width/divisions,0,
                                terrain.min_z+(id/divisions+.5)*depth/divisions);
        QByteArray records;
        records.reserve(std::min<qsizetype>(maxChunkSize,qsizetype(triangles.size())*8));
        touched.clear();
        double radius=0;
        for (auto& i:triangles) {
            if (remap[i]==absent) {
                remap[i]=uint32_t(touched.size());
                touched.push_back(i);
                const auto& position=vertices[i];
                const auto p=position-center;
                radius=std::max(radius,vsg::length(p));
                const auto tex=uv?terrain.imagery_uv[i]:vsg::vec2(0.5f,0.5f);
                for (const float f:{float(p.x),float(p.y),float(p.z),tex.x,tex.y,1.f}) {
                    uint32_t bits;
                    std::memcpy(&bits,&f,sizeof(bits));
                    append(records,bits);
                }
            }
            i=remap[i];
        }
        QByteArray bytes("VXM1",4);
        bytes.reserve(12+records.size()+qsizetype(triangles.size())*4);
        append(bytes,uint32_t(touched.size()));append(bytes,uint32_t(triangles.size()));
        bytes.append(records);
        for (auto index:triangles) append(bytes,index);
        for (auto original:touched) remap[original]=absent;
        chunks.push_back({{"id",std::to_string(id)},{"mesh",store(dir,bytes,".vxm",keep)},{"image",image},
                          {"center_m",{center.x,center.y,center.z}},{"radius_m",radius+1.}});
    }
    const auto& a=scene.anchor;
    return {{"generation_id",generation},{"tick",std::to_string(scene.tick)},
            {"surface_revision",std::to_string(scene.contentRevision)},
            {"native_surface_revision",std::to_string(terrain.revision)},{"terrain_id",object.id},
            {"anchor",{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}}},
            {"scene_bounds_m",bounds},{"changed",std::move(chunks)},
            {"rebuilt_chunks",rebuilt},{"reused_chunks",reused}};
}
}

BrowserEarthSurface::BrowserEarthSurface(std::filesystem::path directory,std::string generation)
    : directory_(std::move(directory)),generation_(std::move(generation)),
      thread_([this] {run();}) {}

BrowserEarthSurface::~BrowserEarthSurface() {
    {
        std::lock_guard lock(mutex_);
        stopping_=true;
    }
    changed_.notify_one();
    thread_.join();
}

void BrowserEarthSurface::submit(earth::VoxelSceneSnapshotPtr snapshot) {
    const auto* terrain=ground(*snapshot);
    if (!terrain) return;
    auto surface=surfaceOf(*terrain,snapshot);
    if (surface==latest_) return;
    latest_=std::move(surface);
    {
        std::lock_guard lock(mutex_);
        pending_=std::move(snapshot);
    }
    changed_.notify_one();
}

std::optional<vsg::dvec3> BrowserEarthSurface::intersect(const std::filesystem::path& directory,const Json& catalog,
        const vsg::dvec3& origin,const vsg::dvec3& direction,double limit) {
    double nearest=limit;
    bool hit=false;
    for (const auto& chunk:catalog.at("changed")) {
        const auto coordinates=chunk.at("center_m").get<std::array<double,3>>();
        const vsg::dvec3 center{coordinates[0],coordinates[1],coordinates[2]};
        const auto relative=center-origin;
        const auto t=std::clamp(vsg::dot(relative,direction),0.,nearest);
        if (vsg::length(relative-direction*t)>chunk.at("radius_m").get<double>()) continue;
        QFile file(QString::fromStdString((directory/chunk.at("mesh").get<std::string>()).string()));
        if (!file.open(QIODevice::ReadOnly) || file.size()>maxChunkSize)
            throw std::runtime_error("browser terrain mesh unavailable for picking");
        const auto bytes=file.readAll();
        if (bytes.size()<12 || !bytes.startsWith("VXM1")) throw std::runtime_error("invalid browser terrain mesh");
        const auto u32=[&](qsizetype at){return qFromLittleEndian<uint32_t>(bytes.constData()+at);};
        const auto vertexCount=u32(4), indexCount=u32(8);
        const qsizetype indexBegin=12+qsizetype(vertexCount)*24;
        if (indexBegin+qsizetype(indexCount)*4!=bytes.size() || indexCount%3)
            throw std::runtime_error("invalid browser terrain mesh extent");
        const auto position=[&](uint32_t index) {
            if (index>=vertexCount) throw std::runtime_error("invalid browser terrain vertex index");
            vsg::dvec3 value;
            for (int axis=0;axis<3;++axis) {
                const auto bits=u32(12+qsizetype(index)*24+axis*4);
                float coordinate;
                std::memcpy(&coordinate,&bits,sizeof(coordinate));
                value[axis]=coordinate;
            }
            return value;
        };
        const auto rayOrigin=origin-center;
        for (qsizetype i=indexBegin;i<bytes.size();i+=12) {
            const auto a=position(u32(i)), b=position(u32(i+4))-a, c=position(u32(i+8))-a;
            const auto h=vsg::cross(direction,c);
            const auto determinant=vsg::dot(b,h);
            if (std::abs(determinant)<1e-8) continue;
            const auto s=rayOrigin-a;
            const auto u=vsg::dot(s,h)/determinant;
            if (u<0 || u>1) continue;
            const auto q=vsg::cross(s,b);
            const auto v=vsg::dot(direction,q)/determinant;
            if (v<0 || u+v>1) continue;
            const auto distance=vsg::dot(c,q)/determinant;
            if (distance>0 && distance<nearest) {nearest=distance;hit=true;}
        }
    }
    if (!hit) return std::nullopt;
    return origin+direction*nearest;
}

void BrowserEarthSurface::run() {
    std::deque<Paths> previous;
    Atlas atlas;
    earth::VoxelSceneSnapshotPtr previousState;
    Json previousCatalog;
    for (;;) {
        earth::VoxelSceneSnapshotPtr state;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock,[&]{return stopping_ || pending_;});
            if (stopping_) return;
            state=std::move(pending_);
        }
        try {
            const auto begin=std::chrono::steady_clock::now();
            const auto chunks=directory_/"browser-earth";
            std::filesystem::create_directories(chunks);
            Paths keep;
            const auto* previousObject=previousState?ground(*previousState):nullptr;
            auto metadata=pack(*state,*ground(*state),chunks,generation_,keep,atlas,previousObject,previousCatalog);
            metadata["publish_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            write(directory_/"browser-earth.json",QByteArray::fromStdString(metadata.dump()));
            previousState=std::move(state);
            previousCatalog=std::move(metadata);
            previous.push_back(std::move(keep));
            if (previous.size()>4) {
                for (const auto& name:previous.front()) {
                    if (std::none_of(std::next(previous.begin()),previous.end(),[&](const auto& set){return set.count(name)>0;})) {
                        std::error_code ignored;
                        std::filesystem::remove(chunks/name,ignored);
                    }
                }
                previous.pop_front();
            }
        } catch (const std::exception& error) {
            spdlog::warn("Browser terrain publication failed: {}",error.what());
        }
    }
}
}
