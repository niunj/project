#pragma once
#include "VoxelModelVoxelizer.h"
#include "VoxelTerrain.h"
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cerrno>
#include <spawn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
extern char** environ;
namespace vsgraysim::earth {
inline nlohmann::json isolatedAsset(const nlohmann::json& job,const std::string& requestedWorker,unsigned timeoutMs,std::shared_ptr<std::atomic<bool>> cancellation={}){
    namespace fs=std::filesystem;std::string worker=requestedWorker;
    if(worker.empty()){if(const auto* env=std::getenv("VSGRAYSIM_VOXEL_WORKER"))worker=env;else{std::error_code error;auto executable=fs::read_symlink("/proc/self/exe",error);for(auto candidate:{executable.parent_path()/"voxel_scene",executable.parent_path().parent_path()/"voxel_scene"})if(fs::is_regular_file(candidate)){worker=candidate.string();break;}}}
    if(worker.empty()||!fs::is_regular_file(worker))throw std::runtime_error("isolated scene preparation requires the voxel_scene worker; set VSGRAYSIM_VOXEL_WORKER");
    std::string pattern=(fs::temp_directory_path()/"vsgraysim-asset-XXXXXX").string();std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);const auto* made=mkdtemp(name.data());if(!made)throw std::runtime_error("cannot create isolated model job directory");const fs::path directory(made);
    struct Cleanup{fs::path directory;~Cleanup(){std::error_code error;fs::remove_all(directory,error);}} cleanup{directory};
    const auto input=(directory/"input.json").string(),output=(directory/"result.cbor").string(),log=(directory/"worker.log").string();

    {std::ofstream file(input);file<<job.dump();if(!file)throw std::runtime_error("cannot write isolated model job");}
    std::vector<char*> args{worker.data(),const_cast<char*>("compile-model"),const_cast<char*>(input.c_str()),const_cast<char*>(output.c_str()),nullptr};
    posix_spawn_file_actions_t actions;const int actionResult=posix_spawn_file_actions_init(&actions);if(actionResult)throw std::runtime_error("cannot initialize model worker launch");
    // Redirect only this child's diagnostics; never change the host process's IO.
    const int redirect=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,log.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    pid_t child=-1;const int spawned=redirect?redirect:posix_spawn(&child,worker.c_str(),&actions,nullptr,args.data(),environ);posix_spawn_file_actions_destroy(&actions);if(spawned)throw std::runtime_error("cannot launch isolated model worker: "+std::to_string(spawned));
    const auto start=std::chrono::steady_clock::now();int status=0;bool timeout=false,cancelled=false;
    for(;;){const auto finished=waitpid(child,&status,WNOHANG);if(finished==child)break;if(finished<0&&errno!=EINTR){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}throw std::runtime_error("cannot collect isolated model worker");}
        if(cancellation&&cancellation->load()){cancelled=true;kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}break;}
        if(std::chrono::steady_clock::now()-start>std::chrono::milliseconds(timeoutMs)){timeout=true;kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}break;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    if(cancelled||timeout||!WIFEXITED(status)||WEXITSTATUS(status)!=0){std::ifstream file(log);std::string diagnostic(4096,'\0');file.read(diagnostic.data(),diagnostic.size());diagnostic.resize(size_t(file.gcount()));throw std::runtime_error(std::string("isolated scene preparation ")+(cancelled?"cancelled":timeout?"timed out":WIFSIGNALED(status)?"terminated by signal "+std::to_string(WTERMSIG(status)):"exited "+std::to_string(WEXITSTATUS(status)))+": "+diagnostic);}
    if(!fs::is_regular_file(output)||fs::file_size(output)>256ULL*1024*1024)throw std::runtime_error("invalid isolated model output budget");std::ifstream file(output,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});return nlohmann::json::from_cbor(bytes);
}
inline std::shared_ptr<VoxelTerrain> isolatedVoxelModel(const std::string& path,const VoxelModelConfig& config,const std::string& worker,unsigned timeoutMs){
    nlohmann::json job={{"kind","model"},{"path",path},{"max_dimension",config.maxDimension},{"voxel_size_m",config.voxelSize},{"margin_m",config.margin},{"units_to_metres",config.unitsToMetres},{"up_axis",config.upAxis},{"fill_interior",config.fillInterior},{"source_surface",config.sourceSurface},{"workers",config.workers}};
    return VoxelTerrain::decodeVolume(isolatedAsset(job,worker,timeoutMs));
}
inline nlohmann::json isolatedSceneAsset(const char* kind,const nlohmann::json& source,const simcore::geo::GeoAnchor& anchor,const std::string& document,const std::string& worker,unsigned timeoutMs){
    return isolatedAsset({{"kind",kind},{"source",source},{"document_path",document},{"anchor",{{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",anchor.alt_m},{"heading_deg",anchor.heading_deg}}}},worker,timeoutMs);
}

}
