#pragma once
#include <QString>
void runUnifiedEarth(const QString& scene,const QString& contextMap,const QString& output,bool interactive);
void runEarthTargetDestruction(const QString& output,int maxDimension=512,
                               const QString& comparisonAsset={},double unitsToMetres=1.);

void runEarthTerrainRegionCapture(const QString& output,bool highResolution=false);
void runEarthUrbanCapture(const QString& baseline, const QString& district, const QString& output,
                         const QString& inspection={},bool compareClusters=false);

enum class EarthTopicCapture {
    Still, TerrainTraverse, TerrainCollision, Thermal, G63Motion, DeckTakeoff, SonarCrossing, DieselPropeller, EnvironmentMotion, MarineAudio
};
void runEarthTopicStillCapture(const QString& topic, const QString& output,
                               EarthTopicCapture mode = EarthTopicCapture::Still);
