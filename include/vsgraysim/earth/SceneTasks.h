#pragma once
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
namespace vsgraysim::earth {
enum class SceneTaskKind:size_t {Target,Terrain,Cloud,Ocean,Scene,Count};
// CPU-only independent preparation. Callbacks must not mutate the published
// world or submit Vulkan commands. Packaged tasks contain normal exceptions;
// native crash isolation is provided separately by the asset worker process.
class SceneTasks {
    struct Lane {std::deque<std::function<void()>> jobs;size_t active=0,limit=1;};
    std::array<Lane,size_t(SceneTaskKind::Count)> lanes_;
    std::mutex mutex_;std::condition_variable ready_;std::vector<std::thread> workers_;bool stopping_=false;size_t next_=0;
    bool pending()const {for(const auto& q:lanes_)if(!q.jobs.empty())return true;return false;}
    bool available()const {for(const auto& q:lanes_)if(!q.jobs.empty()&&q.active<q.limit)return true;return false;}
    void work(){for(;;){std::function<void()> task;size_t selected=0;{
        std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return available()||(stopping_&&!pending());});if(stopping_&&!pending())return;
        for(size_t j=0;j<lanes_.size();++j){selected=(next_+j)%lanes_.size();auto& lane=lanes_[selected];if(!lane.jobs.empty()&&lane.active<lane.limit){task=std::move(lane.jobs.front());lane.jobs.pop_front();++lane.active;next_=(selected+1)%lanes_.size();break;}}
      }task();{std::lock_guard lock(mutex_);--lanes_[selected].active;}ready_.notify_all();}}
public:
    explicit SceneTasks(unsigned workers=4,unsigned targetConcurrency=2){if(workers<1||workers>8||targetConcurrency<1||targetConcurrency>workers)throw std::invalid_argument("scene task workers must be 1..8 with bounded target concurrency");lanes_[size_t(SceneTaskKind::Target)].limit=targetConcurrency;try{for(unsigned i=0;i<workers;++i)workers_.emplace_back([this]{work();});}catch(...){ {std::lock_guard lock(mutex_);stopping_=true;}ready_.notify_all();for(auto& t:workers_)t.join();throw;}}
    ~SceneTasks(){{std::lock_guard lock(mutex_);stopping_=true;}ready_.notify_all();for(auto& t:workers_)t.join();}
    SceneTasks(const SceneTasks&)=delete;SceneTasks& operator=(const SceneTasks&)=delete;
    template<class F>auto submit(SceneTaskKind kind,F&& callback)->std::future<std::invoke_result_t<F>>{
        using Result=std::invoke_result_t<F>;if(size_t(kind)>=lanes_.size())throw std::invalid_argument("invalid scene task category");auto task=std::make_shared<std::packaged_task<Result()>>(std::forward<F>(callback));auto result=task->get_future();{
            std::lock_guard lock(mutex_);if(stopping_)throw std::logic_error("scene executor is stopping");lanes_[size_t(kind)].jobs.emplace_back([task]{(*task)();});}
        ready_.notify_all();return result;
    }
};
}
