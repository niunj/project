#pragma once
#include <cstdlib>

// The host build supplies its selected resource roots. Standalone consumers
// may leave them empty and use the dependency's normal discovery/environment.
namespace rockyenv::runtime {
#ifdef VSGRAYSIM_ROCKY_FILE_PATH
inline constexpr const char* rocky_files=VSGRAYSIM_ROCKY_FILE_PATH;
#else
inline constexpr const char* rocky_files="";
#endif
#ifdef VSGRAYSIM_PROJ_DATA_PATH
inline constexpr const char* proj_data=VSGRAYSIM_PROJ_DATA_PATH;
#else
inline constexpr const char* proj_data="";
#endif
#ifdef VSGRAYSIM_GDAL_DATA_PATH
inline constexpr const char* gdal_data=VSGRAYSIM_GDAL_DATA_PATH;
#else
inline constexpr const char* gdal_data="";
#endif
inline void setDefault(const char* name,const char* value) {
    if(!value||!*value||std::getenv(name))return;
#ifdef _WIN32
    _putenv_s(name,value);
#else
    ::setenv(name,value,0);
#endif
}
inline void configureDefaults() {
#if VSGRAYSIM_WITH_ROCKY
    setDefault("ROCKY_FILE_PATH",rocky_files);
#endif
    setDefault("PROJ_DATA",proj_data);
    setDefault("PROJ_LIB",proj_data);
    setDefault("GDAL_DATA",gdal_data);
}
}
