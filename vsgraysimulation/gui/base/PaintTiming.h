#pragma once
#include <QWidget>
#include <QVariant>
#include <chrono>
namespace vsgraysim::gui {
// CPU wall time in the actual Qt paint handler, not a GPU/compositor fence.
struct PaintTiming {
    QWidget* widget;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~PaintTiming(){const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto samples=widget->property("paintSamples").toList();samples.push_back(ms);if(samples.size()>600)samples.removeFirst();widget->setProperty("paintSamples",samples);widget->setProperty("paintMs",ms);}
};
}
