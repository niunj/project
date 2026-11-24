#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/SurfaceQuality.h>
#include <iostream>
#include <sstream>

namespace {
template<class T> T number(const char* text) {
    std::istringstream input(text);
    T value;
    if(!(input>>value) || input.peek()!=std::char_traits<char>::eof()) throw std::invalid_argument("invalid numeric argument");
    return value;
}
}

int main(int argc,char** argv) {
    if(argc==5 && std::string(argv[1])=="--inspect")try {
        simcore::geo::GeoAnchor anchor;anchor.lat_deg=number<double>(argv[3]);anchor.lon_deg=number<double>(argv[4]);anchor.alt_m=0;
        const auto surface=vsgraysim::earth::EarthDataService::loadMap(argv[2],anchor);
        std::cout<<vsgraysim::earth::surfaceQuality(*surface).json().dump(2)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    if(argc!=9 && !(argc==10 && std::string(argv[9])=="--global-roots")) {
        std::cerr<<"Usage: earth_voxel_bake MAP NEW_DIRECTORY LAT LON MIN_LOD MAX_LOD TILE_RADIUS BOTTOM_METRES [--global-roots]\n       earth_voxel_bake --inspect MAP LAT LON\n";
        return 2;
    }
    try {
        vsgraysim::earth::EarthDataService::bakeVolume(argv[1],argv[2],number<double>(argv[3]),number<double>(argv[4]),
            number<int>(argv[5]),number<int>(argv[6]),number<int>(argv[7]),number<double>(argv[8]),argc==10);
        std::cout<<"Created "<<argv[2]<<"/earth.scene.json\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
