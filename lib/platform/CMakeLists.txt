# -----------------------------------------------------------------------------
# vsgraysim_platform: 平台运动模型与装备联动库
#
# 包含:
#   - IPlatformMotion:        运动模型抽象接口
#   - GroundVehicleMotion:    地面目标 (坦克/装甲车) 运动
#   - AircraftMotion:         空中目标 (飞机/无人机) 运动
#   - NavalVesselMotion:      海面目标 (舰船) 运动
#   - MissileMotion:          导弹运动 (比例导引/追踪法)
#   - PlatformManager:        平台管理器 (ISimulationSubsystem)
#   - PlatformEquipment:      平台装备联动机制
# -----------------------------------------------------------------------------

add_library(vsgraysim_platform STATIC
    GroundVehicleMotion.cpp
    AircraftMotion.cpp
    NavalVesselMotion.cpp
    MissileMotion.cpp
    PlatformManager.cpp
    PlatformEquipment.cpp
    PlatformMotionFactory.cpp
    PlatformPhysicsBridge.cpp
    RoutePlan.cpp
    RelationshipGraph.cpp
)

target_include_directories(vsgraysim_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vsgraysim_platform PUBLIC
    vsgraysim_common
    vsgraysim_core
    vsgraysim_ipc
    Threads::Threads
)