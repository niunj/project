#include "SceneAudioOutput.h"

#ifdef VSG_HAS_SCENE_AUDIO
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#endif

namespace vsgraysim::audio {

struct SceneAudioOutput::Impl {
#ifdef VSG_HAS_SCENE_AUDIO
    using Clock=std::chrono::steady_clock;
    struct Mailbox {
        std::mutex lock;
        std::optional<MixFrame> latest;
        std::atomic<bool> active{false};
    } mailbox;

    class Worker : public QObject {
    public:
        explicit Worker(Mailbox& mail):mail_(mail) {}
        void start() {
            auto* timer=new QTimer(this);
            timer->setTimerType(Qt::PreciseTimer);
            connect(timer,&QTimer::timeout,this,[this]{update();});
            timer->start(8);
        }
    private:
        void closeOutput() {
            stream_=nullptr;
            if(sink_)sink_->reset();
            sink_.reset();
            used_=remaining_=0;
            mail_.active=false;
        }
        void openOutput() {
            lastTry_=Clock::now();
            auto device=QMediaDevices::defaultAudioOutput();
            const auto requested=qgetenv("VSGRAYSIM_AUDIO_DEVICE");
            if(!requested.isEmpty()){
                device={};
                for(const auto& output:QMediaDevices::audioOutputs())
                    if(output.id()==requested){device=output;break;}
            }
            QAudioFormat format;
            format.setSampleRate(SceneAudioMixer::sampleRate);
            format.setChannelCount(2);
            format.setSampleFormat(QAudioFormat::Int16);
            if(device.isNull()||!device.isFormatSupported(format)){
                if(!warned_){spdlog::warn("Scene audio has no compatible stereo output (requested device: {})",requested.constData());warned_=true;}
                return;
            }
            sink_=std::make_unique<QAudioSink>(device,format);
            sink_->setBufferSize(5760);
            stream_=sink_->start();
            if(!stream_)closeOutput();
        }
        void update() {
            std::optional<MixFrame> input;
            {
                std::lock_guard lock(mail_.lock);
                input=std::move(mail_.latest);
                mail_.latest.reset();
            }
            const auto now=Clock::now();
            if(input){
                if(input->playing&&!playing_)lastTry_={};
                if(!mixer_&&input->playing&&!input->voices.empty())mixer_.emplace();
                if(mixer_)mixer_->apply(*input);
                playing_=input->playing;
                lastFrame_=now;
            }
            if(!playing_||now-lastFrame_>std::chrono::milliseconds(800)){
                if(sink_)closeOutput();
                return;
            }
            if(!mixer_)return;
            if(sink_&&sink_->state()==QtAudio::StoppedState&&sink_->error()!=QtAudio::NoError)closeOutput();
            if(!sink_&&now-lastTry_>std::chrono::seconds(2))openOutput();
            if(!stream_)return;
            auto available=sink_->bytesFree();
            if(available<4)return;
            if(!remaining_){
                const auto samples=std::min<qsizetype>(qsizetype(pcm_.size()),(available/4)*2);
                mixer_->render(pcm_.data(),size_t(samples));
                used_=0;remaining_=samples*qsizetype(sizeof(int16_t));
            }
            const auto n=stream_->write(reinterpret_cast<const char*>(pcm_.data())+used_,std::min(available,remaining_));
            if(n<0){closeOutput();return;}
            if(n>0)mail_.active=true;
            used_+=n;remaining_-=n;
        }
        Mailbox& mail_;
        std::optional<SceneAudioMixer> mixer_;
        std::unique_ptr<QAudioSink> sink_;
        QIODevice* stream_=nullptr;
        std::array<int16_t,1536> pcm_{};
        qsizetype used_=0,remaining_=0;
        Clock::time_point lastFrame_{},lastTry_{};
        bool playing_=false,warned_=false;
    };
    QThread thread;
    Impl() {
        auto* worker=new Worker(mailbox);
        worker->moveToThread(&thread);
        QObject::connect(&thread,&QThread::started,worker,[worker]{worker->start();});
        QObject::connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        thread.start();
    }
    ~Impl(){thread.quit();thread.wait();}
#endif
};

SceneAudioOutput::SceneAudioOutput():impl_(std::make_unique<Impl>()){}
SceneAudioOutput::~SceneAudioOutput()=default;
void SceneAudioOutput::submit(MixFrame value) {
#ifdef VSG_HAS_SCENE_AUDIO
    std::lock_guard lock(impl_->mailbox.lock);
    impl_->mailbox.latest=std::move(value);
#else
    (void)value;
#endif
}
bool SceneAudioOutput::active() const {
#ifdef VSG_HAS_SCENE_AUDIO
    return impl_->mailbox.active;
#else
    return false;
#endif
}

}
