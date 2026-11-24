#include "VoxelBuoyancy.h"
#include "VoxelTerrain.h"
#include <vsgraysim/earth/VoxelOcean.h>
#include <btBulletDynamicsCommon.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <exception>
namespace vsgraysim::earth {
namespace {
vsg::dvec3 vec(const btVector3& p){return {p.x(),p.y(),p.z()};}
btVector3 bt(vsg::dvec3 p){return {btScalar(p.x),btScalar(p.y),btScalar(p.z)};}
// Fixed workers only evaluate immutable geometry and water. Bullet is touched
// on the owner thread. Fixed partitions/reduction preserve deterministic save
// and restart; worker exceptions propagate before applying the force.
class HydroWorkers {
    std::mutex invocation,mutex;std::condition_variable ready,finished;
    std::array<std::thread,3> threads;std::array<std::function<void()>,4> jobs;std::array<std::exception_ptr,4> errors{};size_t generation=0;int remaining=0;bool stopping=false;
public:
    HydroWorkers(){for(int i=1;i<4;++i)threads[i-1]=std::thread([this,i]{size_t seen=0;std::unique_lock lock(mutex);for(;;){ready.wait(lock,[&]{return stopping||generation!=seen;});if(stopping)return;seen=generation;lock.unlock();try{jobs[i]();}catch(...){errors[i]=std::current_exception();}lock.lock();if(--remaining==0)finished.notify_one();}});}
    ~HydroWorkers(){{std::lock_guard lock(mutex);stopping=true;}ready.notify_all();for(auto& t:threads)t.join();}
    void run(std::array<std::function<void()>,4> work){std::lock_guard call(invocation);{std::lock_guard lock(mutex);jobs=std::move(work);errors={};remaining=3;++generation;}ready.notify_all();try{jobs[0]();}catch(...){errors[0]=std::current_exception();}{std::unique_lock lock(mutex);finished.wait(lock,[&]{return remaining==0;});}for(const auto& error:errors)if(error)std::rethrow_exception(error);for(auto& job:jobs)job={};}
};
HydroWorkers& hydroWorkers(){static HydroWorkers workers;return workers;}

}
SubmergedBox clipBuoyancyBox(const vsg::dvec3& size,const vsg::dmat4& pose,const vsg::dvec3& n,double plane,bool referencePath,bool divideReference){
    SubmergedBox result;result.centre=pose*(size*.5);
    const auto origin=pose*vsg::dvec3{};vsg::dvec3 coefficients;
    for(int i=0;i<3;++i){const auto axis=pose*vsg::dvec4(i==0?size.x:0,i==1?size.y:0,i==2?size.z:0,0);coefficients[i]=vsg::dot(n,vsg::dvec3(axis.x,axis.y,axis.z));}
    const double offset=vsg::dot(n,origin)-plane;
    // Exact central-prism case: integrate the linear waterline over the
    // other two coordinates analytically. Most nearly upright hydro boxes
    // intersect the sea in this topology; no polygon clipping is needed.
    vsg::dvec3 positive;double threshold=-offset;for(int a=0;a<3;++a){positive[a]=std::abs(coefficients[a]);if(coefficients[a]<0)threshold-=coefficients[a];}
    const double sum=positive.x+positive.y+positive.z;if(!referencePath&&!divideReference){if(sum==0){result.fraction=offset<=0?1:0;return result;}if(threshold<=0)return result;if(threshold>=sum){result.fraction=1;return result;}}
    int dominant=0;for(int a=1;a<3;++a)if(positive[a]>positive[dominant])dominant=a;
    const int b=(dominant+1)%3,c=(dominant+2)%3;const double A=positive[dominant],B=positive[b],C=positive[c];
    if(!referencePath&&A>0&&threshold>=B+C&&threshold<=A){
        const double fraction=(threshold-(B+C)*.5)/A;
        if(fraction>1e-14){vsg::dvec3 moment;moment[b]=(threshold*.5-B/3.-C*.25)/A;moment[c]=(threshold*.5-C/3.-B*.25)/A;moment[dominant]=(threshold*threshold-threshold*(B+C)+(B*B+C*C)/3.+B*C*.5)/(2*A*A);auto centre=moment/fraction;for(int a=0;a<3;++a)if(coefficients[a]<0)centre[a]=1-centre[a];result.fraction=fraction;result.centre=pose*vsg::dvec3(centre.x*size.x,centre.y*size.y,centre.z*size.z);return result;}
    }
    // Exact box-spline moments in the well-conditioned region. Long double
    // and complement symmetry avoid subtracting large nearly equal terms;
    // ill-conditioned near-axis cases retain the convex-polyhedron path.
    std::array<int,3> active{};int dimensions=0;double smallest=1e30,total=0;for(int a=0;a<3;++a){total+=positive[a];if(positive[a]>0){active[dimensions++]=a;smallest=std::min(smallest,positive[a]);}}
    if(!referencePath&&dimensions&&smallest>=A*.001){
        if(threshold<=0)return result;if(threshold>=total){result.fraction=1;return result;}
        const bool complement=threshold>total*.5;const long double t=complement?total-threshold:threshold;
        long double product=1;for(int i=0;i<dimensions;++i)product*=positive[active[i]];
        long double fraction=0;std::array<long double,3> moment{};const int factorial=dimensions==3?6:dimensions==2?2:1;
        const long double inverse=divideReference?0:1/(factorial*product);std::array<long double,3> inverseAxis{};if(!divideReference)for(int i=0;i<dimensions;++i)inverseAxis[i]=1/(static_cast<long double>(dimensions+1)*positive[active[i]]);
        for(int corner=0;corner<(1<<dimensions);++corner){long double h=t;int sign=1;for(int i=0;i<dimensions;++i)if(corner&(1<<i)){h-=positive[active[i]];sign=-sign;}if(h<=0)continue;const long double power=dimensions==3?h*h*h:dimensions==2?h*h:h,volume=divideReference?sign*power/(factorial*product):sign*power*inverse;fraction+=volume;for(int i=0;i<dimensions;++i){const int a=active[i];moment[a]+=volume*((divideReference?h/((dimensions+1)*positive[a]):h*inverseAxis[i])+((corner&(1<<i))?1:0));}}
        for(int a=0;a<3;++a)if(positive[a]==0)moment[a]=fraction*.5;
        if(complement){for(int a=0;a<3;++a)moment[a]=.5L-fraction+moment[a];fraction=1-fraction;}
        if(fraction>1e-14){vsg::dvec3 centre;for(int a=0;a<3;++a){centre[a]=double(moment[a]/fraction);if(coefficients[a]<0)centre[a]=1-centre[a];}result.fraction=double(fraction);result.centre=pose*vsg::dvec3(centre.x*size.x,centre.y*size.y,centre.z*size.z);return result;}
    }
    std::array<vsg::dvec3,8> vertices;std::array<double,8> distances;int inside=0;
    for(int i=0;i<8;++i){vertices[i]={double(i&1),double((i>>1)&1),double((i>>2)&1)};distances[i]=vsg::dot(coefficients,vertices[i])+offset;inside+=distances[i]<=0;}
    if(!inside)return result;if(inside==8){result.fraction=1;return result;}
    struct Polygon {std::array<vsg::dvec3,12> p;int count=0;};std::array<Polygon,7> polygons;int count=0;Polygon cap;
    const int faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    for(const auto& face:faces){Polygon poly;int old=face[3];for(int next:face){const bool a=distances[old]<=0,b=distances[next]<=0;if(a!=b){const double u=distances[old]/(distances[old]-distances[next]);const auto point=vertices[old]+(vertices[next]-vertices[old])*u;poly.p[poly.count++]=point;bool unique=true;for(int k=0;k<cap.count;++k)unique=unique&&vsg::length(cap.p[k]-point)>1e-10;if(unique)cap.p[cap.count++]=point;}if(b)poly.p[poly.count++]=vertices[next];old=next;}if(poly.count>=3)polygons[count++]=poly;}
    if(cap.count>=3){vsg::dvec3 centre{};for(int k=0;k<cap.count;++k)centre+=cap.p[k];centre/=double(cap.count);const auto normal=vsg::normalize(coefficients);const auto tangent=vsg::normalize(vsg::cross(normal,std::abs(normal.x)<.8?vsg::dvec3(1,0,0):vsg::dvec3(0,1,0)));const auto bitangent=vsg::cross(normal,tangent);std::sort(cap.p.begin(),cap.p.begin()+cap.count,[&](const auto& a,const auto& b){const auto x=a-centre,y=b-centre;return std::atan2(vsg::dot(x,bitangent),vsg::dot(x,tangent))<std::atan2(vsg::dot(y,bitangent),vsg::dot(y,tangent));});polygons[count++]=cap;}
    // A convex-combination reference inside the clipped polyhedron makes
    // every tetrahedron positive. No subtraction of nearly equal moments at
    // a near-axis waterline, and no unbounded centre of buoyancy.
    vsg::dvec3 reference{};int points=0;for(int f=0;f<count;++f)for(int k=0;k<polygons[f].count;++k){reference+=polygons[f].p[k];++points;}reference/=double(points);
    double volume=0;vsg::dvec3 moment{};
    for(int f=0;f<count;++f){const auto& face=polygons[f];for(int k=1;k+1<face.count;++k){const auto a=face.p[0],b=face.p[k],c=face.p[k+1];const double v=std::abs(vsg::dot(a-reference,vsg::cross(b-reference,c-reference)))/6;volume+=v;moment+=(reference+a+b+c)*(v*.25);}}
    result.fraction=std::clamp(volume,0.,1.);if(volume>1e-14){const auto c=moment/volume;result.centre=pose*vsg::dvec3(c.x*size.x,c.y*size.y,c.z*size.z);}return result;
}
std::shared_ptr<const std::vector<HydroBox>> makeHydroBoxes(const VoxelTerrain& volume,double span,bool verticalColumns){
    auto out=std::make_shared<std::vector<HydroBox>>();const auto n=volume.dimensions();const auto& cells=volume.occupancy();std::vector<bool> used(cells.size());const double cell=volume.resolution_m;const int cap=std::max(1,int(span/cell));
    auto at=[&](int x,int y,int z){return(size_t(z)*n[1]+y)*n[0]+x;};auto solid=[&](int x,int y,int z){return x>=0&&x<int(n[0])&&y>=0&&y<int(n[1])&&z>=0&&z<int(n[2])&&cells[at(x,y,z)];};auto available=[&](int x,int y,int z){return solid(x,y,z)&&!used[at(x,y,z)];};
    for(int z=0;z<int(n[2]);++z)for(int y=0;y<int(n[1]);++y)for(int x=0;x<int(n[0]);++x)if(available(x,y,z)){
        int ex=x+1,ey=y+1,ez=z+1;while(ex<int(n[0])&&ex-x<cap&&available(ex,y,z))++ex;
        while(ey<int(n[1])&&ey-y<(verticalColumns?int(n[1]):cap)){bool ok=true;for(int i=x;i<ex;++i)ok=ok&&available(i,ey,z);if(!ok)break;++ey;}
        while(ez<int(n[2])&&ez-z<cap){bool ok=true;for(int j=y;j<ey&&ok;++j)for(int i=x;i<ex;++i)ok=ok&&available(i,j,ez);if(!ok)break;++ez;}
        HydroBox box;box.low=volume.gridOrigin()+vsg::dvec3(x,y,z)*cell;box.size=vsg::dvec3(ex-x,ey-y,ez-z)*cell;
        std::array<vsg::dvec3,6> centres{};std::array<size_t,6> counts{};
        for(int k=z;k<ez;++k)for(int j=y;j<ey;++j)for(int i=x;i<ex;++i){used[at(i,j,k)]=true;const auto p=volume.gridOrigin()+vsg::dvec3(i+.5,j+.5,k+.5)*cell;
            for(int a=0;a<3;++a)for(int s:{-1,1}){int neighbour[3]={i,j,k};neighbour[a]+=s;if(solid(neighbour[0],neighbour[1],neighbour[2]))continue;const int face=a*2+(s>0);auto c=p;c[a]+=s*cell*.5;centres[face]+=c;++counts[face];}}
        for(int f=0;f<6;++f)if(counts[f]){vsg::dvec3 normal{};normal[f/2]=f%2?1:-1;box.faces.push_back({centres[f]/double(counts[f]),normal,counts[f]*cell*cell});}out->push_back(std::move(box));
    }return out;
}
std::shared_ptr<const std::vector<HydroNode>> makeHydroTree(const std::vector<HydroBox>& boxes){
    auto tree=std::make_shared<std::vector<HydroNode>>();std::vector<int> order(boxes.size());for(size_t i=0;i<order.size();++i)order[i]=int(i);tree->reserve(boxes.size()*2);
    auto build=[&](auto&& self,size_t first,size_t last)->int{const int index=int(tree->size());tree->push_back({});HydroNode node;node.count=last-first;node.low={1e30,1e30,1e30};vsg::dvec3 high{-1e30,-1e30,-1e30};for(size_t i=first;i<last;++i){const auto& b=boxes[order[i]];const double v=b.size.x*b.size.y*b.size.z;node.volume+=v;node.moment+=(b.low+b.size*.5)*v;for(int a=0;a<3;++a){node.low[a]=std::min(node.low[a],b.low[a]);high[a]=std::max(high[a],b.low[a]+b.size[a]);}}node.size=high-node.low;if(last-first==1)node.box=order[first];else{int axis=0;for(int a=1;a<3;++a)if(node.size[a]>node.size[axis])axis=a;const size_t middle=(first+last)/2;std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+last,[&](int a,int b){return boxes[a].low[axis]+boxes[a].size[axis]*.5<boxes[b].low[axis]+boxes[b].size[axis]*.5;});node.left=self(self,first,middle);node.right=self(self,middle,last);}(*tree)[index]=node;return index;};if(!boxes.empty())build(build,0,boxes.size());return tree;
}
WaterTiming applyVoxelWater(btRigidBody& body,const std::vector<HydroBox>& boxes,const vsg::dmat4& pose,const vsg::dvec3& origin,const VoxelOcean& ocean,double seconds,double dt,const vsg::dvec3& gravity,const std::vector<HydroNode>* tree,const std::vector<HydroBox>* pressureBoxes,const std::vector<HydroNode>* pressureTree){
    const bool profile=true;const bool logProfile=std::getenv("VSGRAYSIM_OCEAN_PROFILE");using Clock=std::chrono::steady_clock;const auto started=profile?Clock::now():Clock::time_point{};double pressureMs=0,dragMs=0;
    const bool classic=std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO");auto clipped=[&](const vsg::dvec3& size,const vsg::dmat4& matrix,const vsg::dvec3& normal,double level){return clipBuoyancyBox(size,matrix,normal,level,false,classic);};
    const auto& volumeBoxes=pressureBoxes?*pressureBoxes:boxes;const auto* volumeTree=pressureTree?pressureTree:tree;
    const vsg::dvec3 axes[3]={{pose[0].x,pose[0].y,pose[0].z},{pose[1].x,pose[1].y,pose[1].z},{pose[2].x,pose[2].y,pose[2].z}},translation{pose[3].x,pose[3].y,pose[3].z};
    auto point=[&](const vsg::dvec3& p){return translation+axes[0]*p.x+axes[1]*p.y+axes[2]*p.z;};
    OceanPressureQuery pressure(ocean,seconds);
    const double activeAmplitude=pressure.amplitudeBound();double meanLow=ocean.seaLevel+ocean.referenceMin,meanHigh=ocean.seaLevel+ocean.referenceMax;
    if(volumeTree&&!volumeTree->empty()){const auto& n=volumeTree->front();double lx=1e30,lz=1e30,hx=-1e30,hz=-1e30;for(int i=0;i<8;++i){const auto p=pose*(n.low+vsg::dvec3(i&1?n.size.x:0,i&2?n.size.y:0,i&4?n.size.z:0));lx=std::min(lx,p.x);lz=std::min(lz,p.z);hx=std::max(hx,p.x);hz=std::max(hz,p.z);}const auto bounds=ocean.meanBounds(lx,lz,hx,hz);meanLow=bounds.first;meanHigh=bounds.second;}

    std::array<int,4> roots{0,-1,-1,-1};int partitions=1;
    if(volumeTree&&volumeTree->size()>128&&!classic){while(partitions<4){int split=-1;size_t count=0;for(int i=0;i<partitions;++i)if((*volumeTree)[roots[i]].box<0&&(*volumeTree)[roots[i]].count>count){count=(*volumeTree)[roots[i]].count;split=i;}if(split<0)break;const auto& n=(*volumeTree)[roots[split]];roots[split]=n.left;roots[partitions++]=n.right;}}
    std::array<double,4> volumes{};std::array<vsg::dvec3,4> moments{};
    auto integratePartition=[&](int partition){const int root=roots[partition];
        OceanPressureQuery pressure(ocean,seconds);double displacedVolume=0;vsg::dvec3 displacedMoment{};
    auto integrate=[&](auto&& self,const vsg::dvec3& low,const vsg::dvec3& size,int depth)->void {
        const auto centre=point(low+size*.5);double radius=0;for(int a=0;a<3;++a)radius+=std::abs(pose[a][1])*size[a]*.5;
        if(centre.y-radius>meanHigh+activeAmplitude||centre.y+radius<ocean.bottom)return;
        const double fullVolume=size.x*size.y*size.z;
        if(centre.y+radius<meanLow-activeAmplitude&&centre.y-radius>=ocean.bottom){displacedVolume+=fullVolume;displacedMoment+=centre*fullVolume;return;}
        const auto wave=pressure.surface(centre.x,centre.z);double planeRadius=0;for(int a=0;a<3;++a){const vsg::dvec3 axis(pose[a][0],pose[a][1],pose[a][2]);planeRadius+=std::abs(vsg::dot(wave.normal,axis))*size[a]*.5;}
        double horizontalSquared=0;vsg::dvec3 projected;for(int a=0;a<3;++a){projected[a]=std::hypot(pose[a][0],pose[a][2])*size[a];horizontalSquared+=projected[a]*projected[a];for(int b=0;b<a;++b)horizontalSquared+=2*std::abs(pose[a][0]*pose[b][0]+pose[a][2]*pose[b][2])*size[a]*size[b];}const double horizontalRadius=.5*std::sqrt(horizontalSquared);
        const double curvature=pressure.curvature(centre.x,centre.z,horizontalRadius,&wave.hessian);
        const double signedCentre=wave.normal.y*(centre.y-wave.height),error=curvature*horizontalRadius*horizontalRadius*.5+.00005+ocean.meanKinkError(centre.x,centre.z,horizontalRadius);
        if(signedCentre-planeRadius>error)return;
        if(signedCentre+planeRadius< -error&&centre.y-radius>=ocean.bottom){displacedVolume+=fullVolume;displacedMoment+=centre*fullVolume;return;}
        int axis=0;for(int a=1;a<3;++a)if(projected[a]>projected[axis])axis=a;
        // Only horizontal extent bends a height field. Vertical splitting of
        // near-upright volumeBoxes was repeating the same waterline integration.
        if(error>.0025&&depth<24){auto half=size;half[axis]*=.5;auto next=low;next[axis]+=half[axis];self(self,low,half,depth+1);self(self,next,half,depth+1);return;}
        auto local=pose;const auto corner=point(low);for(int a=0;a<3;++a)local[3][a]=corner[a];
        const auto wet=clipped(size,local,wave.normal,vsg::dot(wave.normal,vsg::dvec3(centre.x,wave.height,centre.z)));const auto bed=centre.y-radius>=ocean.bottom?SubmergedBox{}:clipped(size,local,{0,1,0},ocean.bottom);
        const double fraction=std::max(0.,wet.fraction-bed.fraction);if(fraction<1e-12)return;
        const auto centroid=(wet.centre*wet.fraction-bed.centre*bed.fraction)/fraction;displacedVolume+=fraction*fullVolume;displacedMoment+=centroid*(fraction*fullVolume);
    };
    auto traverse=[&](auto&& self,int index)->void{
        const auto& n=(*volumeTree)[index];const auto centre=point(n.low+n.size*.5);double radiusY=0,radiusXZ=0;for(int a=0;a<3;++a){radiusY+=std::abs(pose[a][1])*n.size[a]*.5;radiusXZ+=std::hypot(pose[a][0],pose[a][2])*n.size[a]*.5;}
        if(centre.y-radiusY>meanHigh+activeAmplitude||centre.y+radiusY<ocean.bottom)return;
        // A node contains an exact sum of occupied cuboids, including holes.
        // Only wholly wet/dry nodes are aggregated; intersected leaves retain
        // the original adaptive integration and collision geometry.
        const bool inside=std::abs(centre.x)+radiusXZ<=ocean.halfExtent&&std::abs(centre.z)+radiusXZ<=ocean.halfExtent;bool full=inside&&centre.y+radiusY<meanLow-activeAmplitude&&centre.y-radiusY>=ocean.bottom;


        if(full){displacedVolume+=n.volume;displacedMoment+=point(n.moment/n.volume)*n.volume;return;}
        if(n.box>=0){const auto& b=volumeBoxes[n.box];if(ocean.contains(centre.x,centre.z))integrate(integrate,b.low,b.size,0);}else{self(self,n.left);self(self,n.right);}
    };
    if(volumeTree&&!volumeTree->empty())traverse(traverse,root);else for(const auto& b:volumeBoxes)if(ocean.contains((point(b.low+b.size*.5)).x,(point(b.low+b.size*.5)).z))integrate(integrate,b.low,b.size,0);

        volumes[partition]=displacedVolume;moments[partition]=displacedMoment;
    };
    if(partitions==4&&!std::getenv("VSGRAYSIM_OCEAN_SERIAL_HYDRO")){std::array<std::function<void()>,4> jobs;for(int i=0;i<4;++i)jobs[i]=[&,i]{integratePartition(i);};hydroWorkers().run(std::move(jobs));}
    else for(int i=0;i<partitions;++i)integratePartition(i);
    double displacedVolume=0;vsg::dvec3 displacedMoment{};for(int i=0;i<partitions;++i){displacedVolume+=volumes[i];displacedMoment+=moments[i];}
    pressureMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();const auto dragStarted=Clock::now();
    // Water kinematics depend only on this pose/time, so evaluate them in
    // parallel. Apply the implicit impulses below in the original face order.
    struct WetBox {size_t index;vsg::dvec3 centre;double radius;};std::vector<WetBox> wetBoxes;
    auto addWet=[&](size_t index){const auto& box=boxes[index];if(box.faces.empty())return;const auto centre=point(box.low+box.size*.5);if(!ocean.contains(centre.x,centre.z))return;double radius=0;for(int a=0;a<3;++a)radius+=std::abs(pose[a][1])*box.size[a]*.5;if(centre.y-radius>meanHigh+activeAmplitude||centre.y+radius<ocean.bottom)return;wetBoxes.push_back({index,centre,radius});};
    if(tree&&!tree->empty()&&!std::getenv("VSGRAYSIM_OCEAN_EXHAUSTIVE_DRAG")){
        auto select=[&](auto&& self,int index)->void{const auto& node=(*tree)[index];const auto centre=point(node.low+node.size*.5);double radius=0;for(int a=0;a<3;++a)radius+=std::abs(pose[a][1])*node.size[a]*.5;if(centre.y-radius>meanHigh+activeAmplitude+1e-9||centre.y+radius<ocean.bottom-1e-9)return;if(node.box>=0)addWet(size_t(node.box));else{self(self,node.left);self(self,node.right);}};select(select,0);
        // Point drag changes the body's velocity sequentially. Preserve exactly
        // the original occupied-box/face order after spatial culling.
        std::sort(wetBoxes.begin(),wetBoxes.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    }else for(size_t i=0;i<boxes.size();++i)addWet(i);
    std::vector<OceanSample> flows(wetBoxes.size());
    auto prepareFlows=[&](int partition){OceanPressureQuery query(ocean,seconds);const size_t first=wetBoxes.size()*partition/partitions,last=wetBoxes.size()*(partition+1)/partitions;for(size_t i=first;i<last;++i){const auto& c=wetBoxes[i].centre;flows[i]=classic?ocean.sample(c.x,c.z,seconds,c.y):query.sample(c.x,c.z,c.y);}};
    if(partitions==4&&!std::getenv("VSGRAYSIM_OCEAN_SERIAL_HYDRO")){std::array<std::function<void()>,4> jobs;for(int i=0;i<4;++i)jobs[i]=[&,i]{prepareFlows(i);};hydroWorkers().run(std::move(jobs));}else for(int i=0;i<partitions;++i)prepareFlows(i);
    for(size_t wi=0;wi<wetBoxes.size();++wi){const auto& wet=wetBoxes[wi];const auto& box=boxes[wet.index];const auto& centre=wet.centre;const double radius=wet.radius;
        const bool fullyWet=centre.y+radius<meanLow-activeAmplitude&&centre.y-radius>=ocean.bottom;
        const auto& wave=flows[wi];
        for(const auto& face:box.faces){const auto facePoint=point(face.centre);const auto lever=bt(facePoint-origin)-body.getCenterOfMassPosition();
            // One locally linear wave/velocity expansion per integration box.
            // Pressure is refined by the spectrum curvature bound; drag uses
            // a local velocity gradient. We do
            // not evaluate identical trigonometry for each exposed face.
            const auto offset=facePoint-centre;const double height=wave.height-(wave.normal.x*offset.x+wave.normal.z*offset.z)/wave.normal.y;
            const auto velocity=wave.velocity+wave.velocityDx*offset.x+wave.velocityDy*offset.y+wave.velocityDz*offset.z;
            const auto relative=vec(body.getVelocityInLocalPoint(lever))-velocity;const double speed=vsg::length(relative);if(speed<1e-9)continue;
            auto faceSize=box.size;for(int a=0;a<3;++a)if(std::abs(face.normal[a])>.5)faceSize[a]=0;auto facePose=pose;const auto faceLow=point(face.centre-faceSize*.5);for(int a=0;a<3;++a)facePose[3][a]=faceLow[a];
            double faceRadius=0;for(int a=0;a<3;++a)faceRadius+=std::abs(wave.normal.x*pose[a][0]+wave.normal.y*pose[a][1]+wave.normal.z*pose[a][2])*faceSize[a]*.5;const double signedFace=wave.normal.y*(facePoint.y-height);
            const double wetArea=(fullyWet||(signedFace+faceRadius<0&&facePoint.y-radius>=ocean.bottom))?1.:(signedFace-faceRadius>0?0.:std::max(0.,clipped(faceSize,facePose,wave.normal,vsg::dot(wave.normal,vsg::dvec3(facePoint.x,height,facePoint.z))).fraction-(facePoint.y-radius>=ocean.bottom?0.:clipped(faceSize,facePose,{0,1,0},ocean.bottom).fraction)));
            if(wetArea<=0)continue;
            const auto direction=relative/speed;const auto n4=pose*vsg::dvec4(face.normal,0.);const vsg::dvec3 n(n4.x,n4.y,n4.z);const double area=face.area*wetArea*(std::max(0.,vsg::dot(direction,n))+.008);
            const auto d=bt(direction);const double invMass=body.getInvMass()+d.dot((body.getInvInertiaTensorWorld()*(lever.cross(d))).cross(lever));
            const double resistance=.5*ocean.density*ocean.drag*area*(speed+ocean.linearDrag);
            // Implicit point drag is passive even for a thin/light fragment;
            // it cannot overshoot the relative fluid velocity in one update.
            const double impulse=speed*resistance*dt/(1+resistance*dt*invMass);body.applyImpulse(-d*btScalar(impulse),lever);
        }

    }
    dragMs=std::chrono::duration<double,std::milli>(Clock::now()-dragStarted).count();
    if(logProfile)std::fprintf(stderr,"OCEAN pressure_ms=%.6f drag_ms=%.6f total_ms=%.6f boxes=%zu\n",pressureMs,dragMs,std::chrono::duration<double,std::milli>(Clock::now()-started).count(),boxes.size());
    // Sum pressure volume/moments in double precision before crossing Bullet's
    // float API. Full/dry interior cuboids need no wave evaluations at all.
    if(displacedVolume>0)body.applyForce(bt(-gravity*(ocean.density*displacedVolume)),bt(displacedMoment/displacedVolume-origin)-body.getCenterOfMassPosition());
    return {pressureMs,dragMs,std::chrono::duration<double,std::milli>(Clock::now()-started).count()};
}
}
