/*
   stabilize.hpp : PID-based stablization code

   This file is part of Hackflight.

   Hackflight is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Hackflight is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with Hackflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <limits>

#include "rc.hpp"
#include "config.hpp"
#include "common.hpp"

namespace hf {

class Stabilize {
public:
    int16_t axisPID[3];

    void init(const PidConfig& _pidConfig, const ImuConfig& _imuConfig, Board * _board);

    void update(int16_t rcCommand[4], int16_t gyroADC[3], float eulerAngles[3]);

    void resetIntegral(void);

private:

    int16_t lastGyro[2];
    int32_t delta1[2]; 
    int32_t delta2[2];
    int32_t errorGyroI[3];
    int32_t errorAngleI[2];

    Board * board;

    ImuConfig imuConfig;
    PidConfig pidConfig;

    int32_t computeITermGyro(float rateP, float rateI, int16_t rcCommand[4], int16_t gyroADC[3], uint8_t axis);
    int16_t computePid(float rateP, int32_t PTerm, int32_t ITerm, int32_t DTerm, int16_t gyroADC[3], uint8_t axis);
    int16_t computeLevelPid(int16_t rcCommand[4], int16_t gyroADC[3], float eulerAngles[3], uint8_t axis);
}; 


/********************************************* CPP ********************************************************/

void Stabilize::init(const PidConfig& _pidConfig, const ImuConfig& _imuConfig, Board * _board)
{
    // a hack for debugging
    board = _board;

    // We'll use PID, IMU config values in update() below
    memcpy(&pidConfig, &_pidConfig, sizeof(PidConfig));
    memcpy(&imuConfig, &_imuConfig, sizeof(ImuConfig));

    // Zero-out previous values for D term
    for (uint8_t axis=0; axis<2; ++axis) {
        lastGyro[axis] = 0;
        delta1[axis] = 0;
        delta2[axis] = 0;
    }

    resetIntegral();
}

int32_t Stabilize::computeITermGyro(float rateP, float rateI, int16_t rcCommand[4], int16_t gyroADC[3], uint8_t axis)
{
    int32_t error = ((int32_t)rcCommand[axis] * rateP) - gyroADC[axis];

    // Avoid integral windup
    errorGyroI[axis] = constrain(errorGyroI[axis] + error, -16000, +16000);

    if ((std::abs(gyroADC[axis]) > 640) || ((axis == AXIS_YAW) && (std::abs(rcCommand[axis]) > 100)))
        errorGyroI[axis] = 0;

    return ((int32_t)(errorGyroI[axis] * rateI)) >> 6;
}

int16_t Stabilize::computePid(float rateP, int32_t PTerm, int32_t ITerm, int32_t DTerm, int16_t gyroADC[3], uint8_t axis)
{
    PTerm -= (int32_t)gyroADC[axis] * rateP;
    return PTerm + ITerm - DTerm + pidConfig.softwareTrim[axis];
}

int16_t Stabilize::computeLevelPid(int16_t rcCommand[4], int16_t gyroADC[3], float eulerAngles[3], uint8_t axis)
{
    int32_t ITermGyro = computeITermGyro(pidConfig.ratePitchrollP, pidConfig.ratePitchrollI, rcCommand, gyroADC, axis);

    // max inclination
    int32_t errorAngle = constrain(2 * rcCommand[axis], 
            - imuConfig.maxAngleInclination, 
            + imuConfig.maxAngleInclination) 
        - 10*eulerAngles[axis];

    int32_t PTermAccel = errorAngle * pidConfig.levelP; 

    // Avoid integral windup
    errorAngleI[axis] = constrain(errorAngleI[axis] + errorAngle, -10000, +10000);

    int32_t prop = (std::max)(std::abs(rcCommand[DEMAND_PITCH]), 
            std::abs(rcCommand[DEMAND_ROLL])); // range [0;500]

    int32_t PTerm = (PTermAccel * (500 - prop) + rcCommand[axis] * prop) / 500;
    int32_t ITerm = (ITermGyro * prop) / 500;

    int32_t delta = gyroADC[axis] - lastGyro[axis];
    lastGyro[axis] = gyroADC[axis];
    int32_t deltaSum = delta1[axis] + delta2[axis] + delta;
    delta2[axis] = delta1[axis];
    delta1[axis] = delta;
    int32_t DTerm = deltaSum * pidConfig.ratePitchrollD;

    return computePid(pidConfig.ratePitchrollP, PTerm, ITerm, DTerm, gyroADC, axis);
}

void Stabilize::update(int16_t rcCommand[4], int16_t gyroADC[3], float eulerAngles[3])
{
    // Pitch, roll use leveling based on Euler angles
    axisPID[AXIS_ROLL]  = computeLevelPid(rcCommand, gyroADC, eulerAngles, AXIS_ROLL);
    axisPID[AXIS_PITCH] = computeLevelPid(rcCommand, gyroADC, eulerAngles, AXIS_PITCH);

    // For yaw, P term comes directly from RC command, and D term is zero
    int32_t ITermGyroYaw = computeITermGyro(pidConfig.yawP, pidConfig.yawI, rcCommand, gyroADC, AXIS_YAW);
    axisPID[AXIS_YAW] = computePid(pidConfig.yawP, rcCommand[AXIS_YAW], ITermGyroYaw, 0, gyroADC, AXIS_YAW);

    // Prevent "yaw jump" during yaw correction
    axisPID[AXIS_YAW] = constrain(axisPID[AXIS_YAW], 
            -100 - std::abs(rcCommand[DEMAND_YAW]), +100 + std::abs(rcCommand[DEMAND_YAW]));
}

void Stabilize::resetIntegral(void)
{
    errorGyroI[AXIS_ROLL] = 0;
    errorGyroI[AXIS_PITCH] = 0;
    errorGyroI[AXIS_YAW] = 0;
    errorAngleI[AXIS_ROLL] = 0;
    errorAngleI[AXIS_PITCH] = 0;
}

} // namespace
