/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie Firmware
 *
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */
#include "stm32f10x_conf.h" //CPU header file

#include "FreeRTOS.h"	//Operating system header file
#include "task.h"

#include "commander.h"
#include "crtp.h"		//Communication protocol
#include "configblock.h"
#include "param.h"

#define MIN_THRUST  10000
#define MAX_THRUST  60000

/*Commander takes input from client via transmitter CRTP protocol and asserts values.
 * Basically the gateway into the quadcopter from the transmitter*/


// Struct is used to group variables
// http://www.cprogramming.com/tutorial/c/lesson7.html
// __attribute__((packed)) ensures that variables are

//CommanderCrtpValues - Protocol used to send data to/from Crazyflie.
//Structure that contains Roll, pitch, yaw and thrust.
//CRTP = Crazyflie Real Time Protocol
//Each packet has 32 bytes
struct CommanderCrtpValues
{
  float roll;
  float pitch;
  float yaw;
  uint16_t thrust;
} __attribute__((packed));

//NEED TO UNDERSTAND WHAT TARGETVAL[2] means2
static struct CommanderCrtpValues targetVal[2];
static bool isInit;
static int side=0;
static uint32_t lastUpdate;
static bool isInactive;
static bool altHoldMode = FALSE;
static bool altHoldModeOld = FALSE;

//CommanderCrtpCB
static void commanderCrtpCB(CRTPPacket* pk);
static void commanderWatchdogReset(void);

void commanderInit(void)
{
  if(isInit)
    return;


  crtpInit();
  crtpRegisterPortCB(CRTP_PORT_COMMANDER, commanderCrtpCB);

  lastUpdate = xTaskGetTickCount();
  isInactive = TRUE;
  isInit = TRUE;
}

bool commanderTest(void)
{
  crtpTest();
  return isInit;
}

//UNDERSTAND WHAT PK is and WHERE
// "->" is equivalent to a pointer
static void commanderCrtpCB(CRTPPacket* pk)
{
  targetVal[!side] = *((struct CommanderCrtpValues*)pk->data);
  side = !side;
  commanderWatchdogReset();
}

void commanderWatchdog(void)
{
  int usedSide = side;
  uint32_t ticktimeSinceUpdate;

  ticktimeSinceUpdate = xTaskGetTickCount() - lastUpdate;

  if (ticktimeSinceUpdate > COMMANDER_WDT_TIMEOUT_STABALIZE)
  {
    targetVal[usedSide].roll = 0;
    targetVal[usedSide].pitch = 0;
    targetVal[usedSide].yaw = 0;
  }
  if (ticktimeSinceUpdate > COMMANDER_WDT_TIMEOUT_SHUTDOWN)
  {
    targetVal[usedSide].thrust = 0;
    altHoldMode = FALSE; // do we need this? It would reset the target altitude upon reconnect if still hovering
    isInactive = TRUE;
  }
  else
  {
    isInactive = FALSE;
  }
}

static void commanderWatchdogReset(void)
{
  lastUpdate = xTaskGetTickCount();
}

uint32_t commanderGetInactivityTime(void)
{
  return xTaskGetTickCount() - lastUpdate;
}

//Takes RPY from targetVal from transmitter and asserts to eulerRPYdesired
void commanderGetRPY(float* eulerRollDesired, float* eulerPitchDesired, float* eulerYawDesired)
{
  int usedSide = side;

  *eulerRollDesired  = targetVal[usedSide].roll;
  *eulerPitchDesired = targetVal[usedSide].pitch;
  *eulerYawDesired   = targetVal[usedSide].yaw;
}

void commanderGetAltHold(bool* altHold, bool* setAltHold, float* altHoldChange)
{
  *altHold = altHoldMode; // Still in altitude hold mode
  *setAltHold = !altHoldModeOld && altHoldMode; // Hover just activated
  *altHoldChange = altHoldMode ? ((float) targetVal[side].thrust - 32767.) / 32767. : 0.0; // Amount to change altitude hold target
  altHoldModeOld = altHoldMode;
}

//Setting types
void commanderGetRPYType(RPYType* rollType, RPYType* pitchType, RPYType* yawType)
{
  *rollType  = ANGLE;
  *pitchType = ANGLE;
  *yawType   = RATE;
}

//Taking thurst from client across transmitter via targetVal
void commanderGetThrust(uint16_t* thrust)
{
  int usedSide = side;
  uint16_t rawThrust = targetVal[usedSide].thrust;

  if (rawThrust > MIN_THRUST)
  {
    *thrust = rawThrust;
  }
  else
  {
    *thrust = 0;
  }

  if (rawThrust > MAX_THRUST)
  {
    *thrust = MAX_THRUST;
  }

  commanderWatchdog();
}

// Params for flight modes
PARAM_GROUP_START(flightmode)
PARAM_ADD(PARAM_UINT8, althold, &altHoldMode)
PARAM_GROUP_STOP(flightmode)

