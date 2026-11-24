#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <array>
#if VOXEL_EARTH_TEST_RASTER
#include <gdal.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#endif

namespace earth_fixture {
using json=nlohmann::json;namespace fs=std::filesystem;
inline void write(const fs::path& path,const json& value){std::ofstream out(path);out<<value.dump(2);if(!out)throw std::runtime_error("fixture write failed");}
inline json create(const fs::path& root,bool imagery=true,bool hole=false) {
    fs::create_directories(root);
    json earth={{"coverage",{{"min_x",-4},{"max_x",4},{"min_z",-4},{"max_z",4}}},{"grid_size",65},{"imagery_size",32},
        {"strict",true},{"vertical_datum","ellipsoidal"},{"lwir_params",{{"base_temp",314},{"emissivity",.83}}},{"vis_params",{{"albedo",.4}}},
        {"radar_x_params",{{"dielectric_real",6},{"dielectric_imag",.1},{"surface_rough",.4}}},{"sar_c_params",{{"sigma0_db",-8}}}};
    std::string reference;
#if VOXEL_EARTH_TEST_RASTER
    GDALAllRegister();auto driver=GDALGetDriverByName("GTiff");if(!driver)throw std::runtime_error("GTiff fixture unavailable");
    OGRSpatialReference crs;crs.SetWellKnownGeogCS("WGS84");crs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);char* wkt=nullptr;crs.exportToWkt(&wkt);
    auto raster=[&](const char* file,bool color){
        auto ds=GDALCreate(driver,(root/file).c_str(),64,64,color?3:1,color?GDT_Byte:GDT_Float64,nullptr);if(!ds)throw std::runtime_error("raster fixture create failed");
        double transform[6]={-.000064,.000002,0,.000064,0,-.000002};GDALSetGeoTransform(ds,transform);GDALSetProjection(ds,wkt);
        if(color)for(int c=0;c<3;++c) {
            std::vector<uint8_t> data(4096);
            for(int z=0;z<64;++z)for(int x=0;x<64;++x)data[z*64+x]=z<32?std::array<uint8_t,3>{220,80,40}[c]:std::array<uint8_t,3>{30,100,230}[c];
            auto band=GDALGetRasterBand(ds,c+1);GDALSetRasterColorInterpretation(band,GDALColorInterp(GCI_RedBand+c));
            if(GDALRasterIO(band,GF_Write,0,0,64,64,data.data(),64,64,GDT_Byte,0,0)!=CE_None)throw std::runtime_error("image fixture write failed");
        } else {
            std::vector<double> data(4096);
            for(int z=0;z<64;++z)for(int x=0;x<64;++x) {
                const double east=(-.000064+(x+.5)*.000002)*111319.490793,south=-(.000064-(z+.5)*.000002)*110574.27582;
                data[z*64+x]=101.25+.05*east+.08*south;
                if(hole&&x>=28&&x<=35&&z>=28&&z<=35)data[z*64+x]=-9999;
            }
            auto band=GDALGetRasterBand(ds,1);GDALSetRasterNoDataValue(band,-9999);
            if(GDALRasterIO(band,GF_Write,0,0,64,64,data.data(),64,64,GDT_Float64,0,0)!=CE_None)throw std::runtime_error("DEM fixture write failed");
        }
        GDALClose(ds);
    };
    raster("dem.tif",false);if(imagery)raster("imagery.tif",true);CPLFree(wkt);
    json layers=json::array({{{"type","GDALElevation"},{"uri","dem.tif"}}});if(imagery)layers.push_back({{"type","GDALImage"},{"uri","imagery.tif"}});
    write(root/"earth.map.json",{{"earth",earth},{"layers",layers}});reference="earth.map.json";
#else
    (void)imagery;(void)hole;
    const auto path=root/"terrain.ter";std::ofstream out(path,std::ios::binary);out.write("VSGTER1\n",8);uint32_t n=17,zero=0;double extent=8,scale=1,seed=0,water=0;
    out.write(reinterpret_cast<char*>(&n),4);for(auto v:{extent,scale,seed,water})out.write(reinterpret_cast<char*>(&v),8);
    out.write(reinterpret_cast<char*>(&zero),4);out.write(reinterpret_cast<char*>(&zero),4);
    for(int z=0;z<17;++z)for(int x=0;x<17;++x){double height=1.25+.05*(-4+x*.5)+.08*(-4+z*.5);out.write(reinterpret_cast<char*>(&height),8);}
    reference="terrain.ter";
#endif
    json terrain={{"id","earth"},{"source",{{"type","earth"},{"map",reference},{"earth",earth},{"cell_size_m",1},{"bottom_m",-3},{"hd_subdivisions",2},{"mode","hd"}}}};
    json scene={{"format","voxel-scene-v1"},{"geo_anchor",{{"lat",0},{"lon",0},{"alt",100}}},{"objects",{terrain}}};
    write(root/"earth.vscene.json",scene);return scene;
}
}
