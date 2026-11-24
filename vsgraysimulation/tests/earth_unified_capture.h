#pragma once
#include <QString>
void runUnifiedEarth(const QString& scene,const QString& contextMap,const QString& output,bool interactive);
void runEarthTargetDestruction(const QString& output,int maxDimension=512);

void runEarthTerrainRegionCapture(const QString& output,bool highResolution=false);
void runEarthUrbanCapture(const QString& baseline, const QString& district, const QString& output,
                         const QString& inspection={},bool compareClusters=false);
