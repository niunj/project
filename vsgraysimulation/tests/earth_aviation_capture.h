#pragma once
#include <QString>
void runEarthAviationCapture(const QString& output,bool videoFrames=false);
void runEarthAircraftDebrisCapture(const QString& output,bool impactCrush=false);
void runEarthAh64RotorCapture(const QString& scenePath,const QString& output);
void runEarthPlatformMotionCapture(const QString& tankAssetPath,const QString& output,const QString& shipAssetPath={},bool displayVehicle=false);
void runEarthFormalPlatformCapture(const QString& worker,const QString& topic,const QString& output,bool brokerOnly=false);
void runEarthPublishedMotionCapture(const QString& input,const QString& output);
