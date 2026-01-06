#include "stdafx.h"
#include "SilverLining.h"

#include "SampleState.h"
#include "LayerManager.h"

SampleState::SampleState(LayerManager& _layerManger)
    : layerManger(_layerManger)
{
    yaw = 0;
    pitch = -10;
    roll = 0;
    translation = 0;
}

using namespace SilverLining;

static void AdvanceTime(long delta, Atmosphere* atm)
{
    SilverLining::LocalTime lt = atm->GetConditions()->GetTime();
    lt.AddSeconds(delta);
    atm->GetConditions()->SetTime(lt);
}

void SampleState::DoAction(Action action)
{
    if (action == PITCH_UP) {
        pitch = pitch - 1;
    } else if (action == PITCH_DOWN) {
        pitch = pitch + 1;
    } else if (action == YAW_LEFT) {
        yaw = yaw - 1;
    } else if (action == YAW_RIGHT) {
        yaw = yaw + 1;
    } else if (action == MOVE_FORWARD) {
        translation += 100;
    } else if (action == MOVE_BACK) {
        translation -= 100;
    } else if (action == ADVANCE_TIME) {
        AdvanceTime(600, layerManger.GetAtmosphere());
    } else if (action == UNADVANCE_TIME) {
        AdvanceTime(-600, layerManger.GetAtmosphere());
    } else if (action == NEXT_CLOUD_LAYER) {
        layerManger.NextCloudLayer();
    } else if (action == NEXT_PRECIPITATION_TYPE) {
        layerManger.NextPrecipitationType();
    }
}