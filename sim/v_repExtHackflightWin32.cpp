/*
   V-REP simulator plugin code for Hackflight

   Copyright (C) Simon D. Levy and Julio Hidalgo Lopez 2016

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

#include "v_repExtHackflight.h"
#include "scriptFunctionData.h"
#include <iostream>
#include "v_repLib.h"

// Generic C includes
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>

#define CONTROLLER_TARANIS
//#define CONTROLLER_SPEKTRUM
//#define CONTROLLER_EXTREME3DPRO
//#define CONTROLLER_PS3
//#define CONTROLLER_KEYBOARD


#ifdef _WIN32
	#ifdef QT_COMPIL
		#include <direct.h>
	#else
		#include <shlwapi.h>
		#pragma comment(lib, "Shlwapi.lib")
	#endif
#include <conio.h>
#endif /* _WIN32 */
#if defined (__linux) || defined (__APPLE__)
	#include <unistd.h>
	#define WIN_AFX_MANAGE_STATE
	#define LUA_GET_JOYSTICK_COUNT_COMMAND "simExtJoyGetCount"
	#define LUA_GET_JOYSTICK_DATA_COMMAND  "simExtJoyGetData"
#endif /* __linux || __APPLE__ */

#define CONCAT(x,y,z) x y z
#define strConCat(x,y,z)	CONCAT(x,y,z)

#define PLUGIN_NAME "Hackflight"

LIBRARY vrepLib;

// Hackflight interface
extern void setup(void);
extern void loop(void);
static uint32_t micros;

// Launch support
static bool ready;

// Unused for Windows
static int joyfd;

// needed for spring-mounted throttle stick
static int ps3throttle;
#define PS3_THROTTLE_INC .01  

// stick keyboardIncrement for each keyboard down cycle
#define KEYBOARD_INC 10

// Joystick support for Linux, OS X ==========================================
#ifndef _WIN32

#define JOY_DEV "/dev/input/js0"

#include <linux/joystick.h>
#include <termios.h>

static struct termios oldSettings;

void kbinit(void)
{
    struct termios newSettings;

    // Save keyboard settings for restoration later
    tcgetattr(fileno( stdin ), &oldSettings);

    // Create new keyboard settings
    newSettings = oldSettings;
    newSettings.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr(fileno(stdin), TCSANOW, &newSettings);
}

int _kbhit(void)
{
    fd_set set;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 1000;

    FD_ZERO( &set );
    FD_SET( fileno( stdin ), &set );

    int res = select( fileno( stdin )+1, &set, NULL, NULL, &tv );

    char c = 0;

    if( res > 0 )
    {
        read(fileno( stdin ), &c, 1);
    }

    else if( res < 0 ) {
        perror( "select error" );
    }

    return (int)c;
}

static void kbdone(void)
{
    tcsetattr(fileno(stdin), TCSANOW, &oldSettings);
}


// Controller axis maps

#ifdef CONTROLLER_TARANIS
static int axes[5] = {0, 1, 2, 3, 4}; // roll, pitch, yaw, throttle, aux
#endif

#ifdef CONTROLLER_SPEKTRUM
static int axes[5] = {1, 2, 5, 0, 3}; 
#endif

#ifdef CONTROLLER_EXTREME3DPRO
static int axes[5] = {0, 1, 2, 3, 4}; 
#endif

#ifdef CONTROLLER_PS3
static int axes[5] = {2, 3, 0, 1, -1};  // aux unused
#endif

#ifdef CONTROLLER_KEYBOARD
static int axes[5] = {-1, -1, -1, -1, -1};  // all unused
#endif

void LUA_GET_JOYSTICK_COUNT_COMMAND_CALLBACK(SLuaCallBack* p)
{
	// Prepare the return value:
	p->outputArgCount=1; // 1 return value

    // x return values takes x*2 simInt for the type and size buffer
	p->outputArgTypeAndSize=(simInt*)simCreateBuffer(p->outputArgCount*2*sizeof(simInt)); 
	p->outputArgTypeAndSize[2*0+0]=sim_lua_arg_int;	        // The return value is an int
	p->outputArgTypeAndSize[2*0+1]=1;					    // Not used (table size if the return value was a table)
	p->outputInt=(simInt*)simCreateBuffer(1*sizeof(int));   // One int return value

    int retval = 0;

    joyfd = open(JOY_DEV, O_RDONLY);

    if (joyfd > 0) {
        fcntl(joyfd, F_SETFL, O_NONBLOCK);
        retval = 1;
    }

    // Use keyboard as fallback
    else {
        kbinit();
    }

	p->outputInt[0] = retval;                              // The integer value we want to return
}

static int scaleAxis(int value)
{
    return 1000 * value / 32767.;
}

void LUA_GET_JOYSTICK_DATA_CALLBACK(SLuaCallBack* p)
{
	bool error=true;
	if (p->inputArgCount>0)
	{ // Ok, we have at least 1 input argument
		if (p->inputArgTypeAndSize[0*2+0]==sim_lua_arg_int)
		{ // Ok, we have an int as argument 1
			if ( (p->inputInt[0]<1)&&(p->inputInt[0]>=0) )
			{ // Ok, there is a device at this index!
				error=false;
			}
			else
				simSetLastError(LUA_GET_JOYSTICK_DATA_COMMAND,"Invalid index."); // output an error
		}
		else
			simSetLastError(LUA_GET_JOYSTICK_DATA_COMMAND,"Wrong argument type/size."); // output an error
	}
	else
		simSetLastError(LUA_GET_JOYSTICK_DATA_COMMAND,"Not enough arguments."); // output an error


	// Now we prepare the return value(s):
	if (error) {
		p->outputArgCount=0; // 0 return values --> nil (error)
	}
	else {
		p->outputArgCount=5; // 5 return values

        // x return values takes x*2 simInt for the type and size buffer
		p->outputArgTypeAndSize=(simInt*)simCreateBuffer(p->outputArgCount*10*sizeof(simInt)); 
		p->outputArgTypeAndSize[2*0+0]=sim_lua_arg_int|sim_lua_arg_table;	// The return value is an int table
		p->outputArgTypeAndSize[2*0+1]=3;					// table size is 3 (the 3 axes)
		p->outputArgTypeAndSize[2*1+0]=sim_lua_arg_int;	// The return value is an int
		p->outputArgTypeAndSize[2*1+1]=1;					// Not used (not a table)
		p->outputArgTypeAndSize[2*2+0]=sim_lua_arg_int|sim_lua_arg_table;	// The return value is an int table
		p->outputArgTypeAndSize[2*2+1]=3;					// table size is 3 (the 3 rot axes)
		p->outputArgTypeAndSize[2*3+0]=sim_lua_arg_int|sim_lua_arg_table;	// The return value is an int table
		p->outputArgTypeAndSize[2*3+1]=2;					// table size is 2 (the 2 sliders)
		p->outputArgTypeAndSize[2*4+0]=sim_lua_arg_int|sim_lua_arg_table;	// The return value is an int table
		p->outputArgTypeAndSize[2*4+1]=4;					// table size is 4 (the 4 pov values)

        // 13 int return values (3 for the axes + 1 for the buttons + 3 for rot axis, +2 for slider, +4 for pov)
		p->outputInt=(simInt*)simCreateBuffer(13*sizeof(int)); 

        static int roll, pitch, yaw, throttle, aux;

        if (joyfd > 0) {

            struct js_event js;

            read(joyfd, &js, sizeof(struct js_event));

            if (js.type & JS_EVENT_AXIS) {

                    if (js.number == axes[0]) 
                        roll = scaleAxis(js.value);

                    if (js.number == axes[1]) 
                        pitch = scaleAxis(js.value);

                    if (js.number == axes[2]) 
                        yaw = scaleAxis(js.value);

                    if (js.number == axes[3]) 
                        throttle = scaleAxis(js.value);

                    if (js.number == axes[4]) 
                        aux = scaleAxis(js.value);
                }

        }

        // We only need to specify these five return values
        p->outputInt[1]= roll;
        p->outputInt[2]= pitch;
        p->outputInt[4]= yaw;
        p->outputInt[0]= throttle;
        p->outputInt[6]= aux;
    }
}

#endif // non-Windows

struct sBubbleRob
{
	int handle;
	int motorHandles[2];
	int sensorHandle;
	float backRelativeVelocities[2];
	float duration;
	float backMovementDuration;
	char* waitUntilZero;
};

std::vector<sBubbleRob> allBubbleRobs;
int nextBubbleRobHandle=0;

int getBubbleRobIndexFromHandle(int bubbleRobHandle)
{
	for (unsigned int i=0;i<allBubbleRobs.size();i++)
	{
		if (allBubbleRobs[i].handle==bubbleRobHandle)
			return(i);
	}
	return(-1);
}

// Stick demands from controller
static int demands[5];

// IMU support
static double accel[3];
static double gyro[3];

// Barometer support
static int baroPressure;

// Motor support
static double thrusts[4];

// Timestep for current run, used for simulating microsend timer
static double timestep;

// Keyboard-handling helpers ----------------------------------------------

static void keyboardChange(int index, int dir)
{
    demands[index] += dir*KEYBOARD_INC;

    if (demands[index] > 1000)
        demands[index] = 1000;

    if (demands[index] < -1000)
        demands[index] = -1000;
}

static void keyboardIncrement(int index) 
{
    keyboardChange(index, +1);
}

static void keyboardDecrement(int index) 
{
    keyboardChange(index, -1);
}

// --------------------------------------------------------------------------------------
// simExtHackflight_start
// --------------------------------------------------------------------------------------
#define LUA_START_COMMAND "simExtHackflight_start"

static const int inArgs_START[]={
    1,
    sim_script_arg_double,0 // timestep
};

void LUA_START_CALLBACK(SScriptCallBack* cb)
{
    // Run Hackflight setup()
    // XXX setup();

    // Grab timestep from input stack and return success
    CScriptFunctionData D;
    if (D.readDataFromStack(cb->stackID,inArgs_START,inArgs_START[0],LUA_START_COMMAND)) {
        std::vector<CScriptFunctionDataItem>* inData=D.getInDataPtr();
        timestep = inData->at(0).doubleData[0];  
    }
    D.pushOutData(CScriptFunctionDataItem(true)); // success
    D.writeDataToStack(cb->stackID);

    // Need this to handle keyboard for throttle and springy PS3 collective stick
    ps3throttle = -1000;
    demands[3] = -1000;

    // Now we're ready
    ready = true;
}


// --------------------------------------------------------------------------------------
// simExtHackflight_stop
// --------------------------------------------------------------------------------------

#define LUA_STOP_COMMAND "simExtHackflight_stop"

void LUA_STOP_CALLBACK(SScriptCallBack* cb)
{
#ifndef _WIN32
    // Close joystick connection or keyboard if open
    if (joyfd > 0)
        close(joyfd);
    else
        kbdone();
#endif
    CScriptFunctionData D;
    D.pushOutData(CScriptFunctionDataItem(true)); // success
    D.writeDataToStack(cb->stackID);
}


// --------------------------------------------------------------------------------------
// simExtHackflight_update
// --------------------------------------------------------------------------------------

#define LUA_UPDATE_COMMAND "simExtHackflight_update"

static const int inArgs_UPDATE[]={
    4,
    sim_script_arg_int32|sim_script_arg_table,5,  // RC axis values
    sim_script_arg_double|sim_script_arg_table,3, // Gyro values
    sim_script_arg_double|sim_script_arg_table,3, // Accelerometer values
    sim_script_arg_int32                          // Barometric pressure
};

void LUA_UPDATE_CALLBACK(SScriptCallBack* cb)
{
    CScriptFunctionData D;
    if (D.readDataFromStack(cb->stackID,inArgs_UPDATE,inArgs_UPDATE[0],LUA_UPDATE_COMMAND)) {
        std::vector<CScriptFunctionDataItem>* inData=D.getInDataPtr();

        // Read RC axes if joystick available
        if (joyfd > 0) {
            for (int k=0; k<5; ++k)
                demands[k] = inData->at(0).int32Data[k];
        }

#if defined(CONTROLLER_PS3) || defined(CONTROLLER_EXTREME3DPRO)

        // Scale down non-RC controllers
        demands[0] /= 2;
        demands[1] /= 2;

        // and negate their throttle and pitch values
        demands[1] *= -1;
        demands[3] *= -1;
#endif

#ifdef CONTROLLER_PS3

        // PS3 spring-mounted throttle requires special handling
        ps3throttle += demands[3] * PS3_THROTTLE_INC;     
        if (ps3throttle < -1000)
            ps3throttle = -1000;
        if (ps3throttle > 1000)
            ps3throttle = 1000;
        demands[3] = ps3throttle;
#endif

        //printf("r: %4d    p: %4d    y: %4d    t: %4d    a: %4d\n", demands[0], demands[1], demands[2], demands[3], demands[4]);


        // Read gyro, accelerometer
        for (int k=0; k<3; ++k) {
            gyro[k]   = inData->at(1).doubleData[k]; 
            accel[k]  = inData->at(2).doubleData[k]; 
        }

        // Read barometer
        baroPressure = inData->at(3).int32Data[0];

        // Set thrust for each motor
        for (int i=0; i<4; ++i) {
            char signame[10];
            sprintf_s(signame, "thrust%d", i+1);
            simSetFloatSignal(signame, (simFloat)thrusts[i]);
        }
    }

    // Increment microsecond count
    micros += (uint32_t)(1e6 * timestep);

    // Return success
    D.pushOutData(CScriptFunctionDataItem(true)); 
    D.writeDataToStack(cb->stackID);

}


// -------------------------------------------------------------------------------------------------------

// Generic V-REP plugin support

VREP_DLLEXPORT unsigned char v_repStart(void* reservedPointer,int reservedInt)
{ // This is called just once, at the start of V-REP.
	// Dynamically load and bind V-REP functions:
	char curDirAndFile[1024];
#ifdef _WIN32
	#ifdef QT_COMPIL
		_getcwd(curDirAndFile, sizeof(curDirAndFile));
	#else
		GetModuleFileName(NULL,curDirAndFile,1023);
		PathRemoveFileSpec(curDirAndFile);
	#endif
#elif defined (__linux) || defined (__APPLE__)
	getcwd(curDirAndFile, sizeof(curDirAndFile));
#endif

	std::string currentDirAndPath(curDirAndFile);
	std::string temp(currentDirAndPath);

#ifdef _WIN32
	temp+="\\v_rep.dll";
#elif defined (__linux)
	temp+="/libv_rep.so";
#elif defined (__APPLE__)
	temp+="/libv_rep.dylib";
#endif /* __linux || __APPLE__ */

	vrepLib=loadVrepLibrary(temp.c_str());
	if (vrepLib==NULL)
	{
		std::cout << "Error, could not find or correctly load v_rep.dll. Cannot start 'BubbleRob' plugin.\n";
		return(0); // Means error, V-REP will unload this plugin
	}
	if (getVrepProcAddresses(vrepLib)==0)
	{
		std::cout << "Error, could not find all required functions in v_rep.dll. Cannot start 'BubbleRob' plugin.\n";
		unloadVrepLibrary(vrepLib);
		return(0); // Means error, V-REP will unload this plugin
	}

	// Check the V-REP version:
	int vrepVer;
	simGetIntegerParameter(sim_intparam_program_version,&vrepVer);
	if (vrepVer<30200) // if V-REP version is smaller than 3.02.00
	{
		std::cout << "Sorry, your V-REP copy is somewhat old, V-REP 3.2.0 or higher is required. Cannot start 'BubbleRob' plugin.\n";
		unloadVrepLibrary(vrepLib);
		return(0); // Means error, V-REP will unload this plugin
	}

	// Register new Lua commands:
    simRegisterScriptCallbackFunction(strConCat(LUA_START_COMMAND,"@",PLUGIN_NAME), NULL, LUA_START_CALLBACK);
    simRegisterScriptCallbackFunction(strConCat(LUA_UPDATE_COMMAND,"@",PLUGIN_NAME), NULL, LUA_UPDATE_CALLBACK);
    simRegisterScriptCallbackFunction(strConCat(LUA_STOP_COMMAND,"@",PLUGIN_NAME), NULL, LUA_STOP_CALLBACK);

#ifndef _WIN32
	// Support joystick for non-Windows
	int inArgs1[]={0};
	simRegisterCustomLuaFunction(LUA_GET_JOYSTICK_COUNT_COMMAND,strConCat("number count=",
                LUA_GET_JOYSTICK_COUNT_COMMAND,"()"),inArgs1,LUA_GET_JOYSTICK_COUNT_COMMAND_CALLBACK);

	int inArgs2[]={1,sim_lua_arg_int};
	simRegisterCustomLuaFunction(LUA_GET_JOYSTICK_DATA_COMMAND,
            strConCat("table_3 axes, number buttons,table_3 rotAxes,table_2 slider,table_4 pov=",
                LUA_GET_JOYSTICK_DATA_COMMAND,"(number deviceIndex)"),inArgs2,LUA_GET_JOYSTICK_DATA_CALLBACK);
#endif

	return 8; // initialization went fine, we return the version number of this plugin (can be queried with simGetModuleName)
}

VREP_DLLEXPORT void v_repEnd()
{ // This is called just once, at the end of V-REP
	unloadVrepLibrary(vrepLib); // release the library
}

VREP_DLLEXPORT void* v_repMessage(int message,int* auxiliaryData,void* customData,int* replyData)
{ 
    // Don't do anything till start() has been called
    if (!ready)
        return NULL;

    // Default to keyboard if no joy
	/* XXX
    if (joyfd < 1) 
        switch (_kbhit()) {
            case 10:
                keyboardIncrement(2);
                break;
            case 50:
                keyboardDecrement(2);
                break;
            case 53:
                keyboardIncrement(3);
                break;
            case 54:
                keyboardDecrement(3);
                break;
            case 65:
                keyboardIncrement(1);
                break;
            case 66:
                keyboardDecrement(1);
                break;
            case 67:
                keyboardIncrement(0);
                break;
            case 68:
                keyboardDecrement(0);
                break;
            case 47:
                //this->aux = +1;
                break;
            case 42:
                //this->aux = 0;
                break;
            case 45:
                //this->aux = -1;
                break;
        }
		*/

    int errorModeSaved;
    simGetIntegerParameter(sim_intparam_error_report_mode,&errorModeSaved);
    simSetIntegerParameter(sim_intparam_error_report_mode,sim_api_errormessage_ignore);
	simSetIntegerParameter(sim_intparam_error_report_mode,errorModeSaved); // restore previous settings

    // Call Hackflight loop() from here for most realistic simulation
    // XXX loop();

	return NULL;
}

// -------------------------------------------------------------------------------------------------------
