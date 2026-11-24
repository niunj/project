#pragma once
#include <QString>
namespace vsgraysim::gui::display {
// Runs the normal workbench with actual voxel sensor channels and a native Earth view.
int runVoxelEquipmentWorkbench(const QString& config,const QString& captureDirectory={},const QString& initialConsole="mission",const QString& cockpitProfile={});
}
