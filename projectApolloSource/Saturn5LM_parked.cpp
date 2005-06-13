/***************************************************************************
  This file is part of Project Apollo - NASSP
  Copyright 2004-2005

  ORBITER vessel module: Saturn V Module Parked/Docked mode

  Project Apollo is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Project Apollo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Project Apollo; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  See http://nassp.sourceforge.net/license/ for more details.

  **************************** Revision History ****************************
  *	$Log$
  *	Revision 1.6  2005/06/09 14:18:23  lazyd
  *	Added code for function SetGimbal which sets the value of GMBLswitch
  *	
  *	Revision 1.5  2005/05/27 15:43:08  tschachim
  *	Fixed bug: virtual engines are always on
  *	
  *	Revision 1.4  2005/05/21 16:14:36  movieman523
  *	Pass Realism and AudioLanguage correctly from CSM to LEM.
  *	
  *	Revision 1.3  2005/04/11 23:43:21  yogenfrutz
  *	Added LEM Panel_ID
  *	
  *	Revision 1.2  2005/02/24 00:27:56  movieman523
  *	Revised to make LEVA sounds work with Orbitersound 3.
  *	
  *	Revision 1.1  2005/02/11 12:54:06  tschachim
  *	Initial version
  *	
  **************************************************************************/

#include "Orbitersdk.h"
#include "stdio.h"
#include "math.h"
#include "OrbiterSoundSDK3.h"
#include "resource.h"

#include "nasspdefs.h"
#include "nasspsound.h"

#include "soundlib.h"
#include "apolloguidance.h"
#include "LEMcomputer.h"
#include "dsky.h"

#include "landervessel.h"
#include "sat5_lmpkd.h"

// ==============================================================
// Global parameters
// ==============================================================

static int refcount;

const double N   = 1.0;
const double kN  = 1000.0;
const double KGF = N*G;
const double SEC = 1.0*G;
const double KG  = 1.0;

const VECTOR3 OFS_STAGE1 =  { 0, 0, -8.935};
const VECTOR3 OFS_STAGE2 =  { 0, 0, 9.25-12.25};
const VECTOR3 OFS_STAGE21 =  { 1.85,1.85,24.5-12.25};
const VECTOR3 OFS_STAGE22 =  { -1.85,1.85,24.5-12.25};
const VECTOR3 OFS_STAGE23 =  { 1.85,-1.85,24.5-12.25};
const VECTOR3 OFS_STAGE24 =  { -1.85,-1.85,24.5-12.25};

const int TO_EVA=1;

//Begin code

LanderVessel::LanderVessel(OBJHANDLE hObj, int fmodel) : VESSEL (hObj, fmodel)

{
	// Nothing special to do.
}

sat5_lmpkd::sat5_lmpkd(OBJHANDLE hObj, int fmodel) : VESSEL (hObj, fmodel), dsky(soundlib, agc), agc(soundlib, dsky)

{
	// VESSELSOUND **********************************************************************
	// initialisation

	soundlib.InitSoundLib(hObj, SOUND_DIRECTORY);

	Init();
}

sat5_lmpkd::~sat5_lmpkd()

{
	//
	// Nothing for now.
	//
}

void sat5_lmpkd::Init()

{
	//bAbort =false;
	RCS_Full=true;
	Eds=true;
	
	toggleRCS =false;

	InitPanel();

	ABORT_IND=false;

	high=false;
	bToggleHatch=false;
	bModeDocked=false;
	bModeHover=false;
	HatchOpen=false;
	bManualSeparate=false;
	ToggleEva=false;
	EVA_IP=false;
	refcount = 0;
	lmpview=true;
	cdrview=true;
	startimer=false;
	ContactOK = false;
	stage = 0;
	status = 0;

	InVC = false;
	Crewed = true;
	AutoSlow = false;

	MissionTime = 0;
	FirstTimestep = true;

	SwitchFocusToLeva = 0;

	agc.ControlVessel(this);
	dsky.Init();

	soundlib.SoundOptionOnOff(PLAYCOUNTDOWNWHENTAKEOFF, FALSE);
	soundlib.SoundOptionOnOff(PLAYCABINAIRCONDITIONING, FALSE);

	ph_Dsc = 0;
	ph_Asc = 0;
	ph_rcslm0 = 0;
	ph_rcslm1 = 0;

	Realism = REALISM_DEFAULT;

	strncpy(AudioLanguage, "English", 64);
	soundlib.SetLanguage(AudioLanguage);

	SoundsLoaded = false;

	SetLmVesselDockStage();
}

void sat5_lmpkd::DoFirstTimestep()

{
	if (!SoundsLoaded) {
		LoadDefaultSounds();
	}

	if (CabinFansActive()) {
		CabinFans.play(LOOP,255);
	}

	char VName10[256]="";

	strcpy (VName10, GetName()); strcat (VName10, "-LEVA");
	hLEVA=oapiGetVesselByName(VName10);
}

void sat5_lmpkd::LoadDefaultSounds()

{
	//
	// load sounds now that the audio language has been set up.
	//

	soundlib.LoadMissionSound(LunarAscent, LUNARASCENT_SOUND, LUNARASCENT_SOUND);
	soundlib.LoadSound(StageS, "Stagesep.wav");
	soundlib.LoadMissionSound(Scontact, LUNARCONTACT_SOUND, LUNARCONTACT_SOUND);
	soundlib.LoadSound(Sclick, CLICK_SOUND, INTERNAL_ONLY);
	soundlib.LoadSound(Bclick, "button.wav", INTERNAL_ONLY);
	soundlib.LoadSound(Gclick, "guard.wav", INTERNAL_ONLY);
	soundlib.LoadSound(CabinFans, "cabin.wav", INTERNAL_ONLY);
	soundlib.LoadSound(Vox, "vox.wav");
	soundlib.LoadSound(Afire, "des_abort.wav");

	SoundsLoaded = true;
}

void sat5_lmpkd::AttitudeLaunch1()
{
	//Original code function by Richard Craig From MErcury Sample by Rob CONLEY
	// Modification for NASSP specific needs by JL Rocca-Serra
	VECTOR3 ang_vel;
	GetAngularVel(ang_vel);// gets current angular velocity for stabilizer and rate control
// variables to store each component deflection vector	
	VECTOR3 rollvectorl={0.0,0.0,0.0};
	VECTOR3 rollvectorr={0.0,0.0,0.0};
	VECTOR3 pitchvector={0.0,0.0,0.0};
	VECTOR3 yawvector={0.0,0.0,0.0};
	VECTOR3 yawvectorm={0.0,0.0,0.0};
	VECTOR3 pitchvectorm={0.0,0.0,0.0};
//************************************************************
// variables to store Manual control levels for each axis
	double tempP = 0.0;
	double tempY = 0.0;
	double tempR = 0.0; 
//************************************************************
// Variables to store correction factors for rate control
	double rollcorrect = 0.0;
	double yawcorrect= 0.0;
	double pitchcorrect = 0.0;
//************************************************************
// gets manual control levels in each axis, this code copied directly from Rob Conley's Mercury Atlas	
	if (GMBLswitch){
		tempP = GetManualControlLevel(THGROUP_ATT_PITCHDOWN, MANCTRL_ANYDEVICE, MANCTRL_ANYMODE) - GetManualControlLevel(THGROUP_ATT_PITCHUP, MANCTRL_ANYDEVICE, MANCTRL_ANYMODE);
	}
	if (GMBLswitch){
		tempR = GetManualControlLevel(THGROUP_ATT_BANKLEFT, MANCTRL_ANYDEVICE, MANCTRL_ANYMODE) - GetManualControlLevel(THGROUP_ATT_BANKRIGHT, MANCTRL_ANYDEVICE, MANCTRL_ANYMODE);
	}
	
	
	//sprintf (oapiDebugString(), "roll input: %f, roll vel: %f,pitch input: %f, pitch vel: %f", tempR, ang_vel.z,tempP, ang_vel.x);
	
//*************************************************************
//Creates correction factors for rate control in each axis as a function of input level
// and current angular velocity. Varies from 1 to 0 as angular velocity approaches command level
// multiplied by maximum rate desired
	if(tempR != 0.0)	{
		rollcorrect = (1/(fabs(tempR)*0.175))*((fabs(tempR)*0.175)-fabs(ang_vel.z));
			if((tempR > 0 && ang_vel.z > 0) || (tempR < 0 && ang_vel.z < 0))	{
						rollcorrect = 1;
					}
	}
	if(tempP != 0.0)	{
		pitchcorrect = (1/(fabs(tempP)*0.275))*((fabs(tempP)*0.275)-fabs(ang_vel.x));
		if((tempP > 0 && ang_vel.x > 0) || (tempP < 0 && ang_vel.x < 0))	{
						pitchcorrect = 1;
					}
	}
	
//*************************************************************	
// Create deflection vectors in each axis
	pitchvector = _V(0.0,0.0,0.05*tempP*pitchcorrect);
	pitchvectorm = _V(0.0,0.0,-0.2*tempP*pitchcorrect);
	yawvector = _V(0.05*tempY*yawcorrect,0.0,0.0);
	yawvectorm = _V(0.05*tempY*yawcorrect,0.0,0.0);
	rollvectorl = _V(0.0,0.60*tempR*rollcorrect,0.0);
	rollvectorr = _V(0.60*tempR*rollcorrect,0.0,0.0);

//*************************************************************
// create opposite vectors for "gyro stabilization" if command levels are 0
	if(tempP==0.0 && GMBLswitch) {
		pitchvectorm=_V(0.0,0.0,-0.8*ang_vel.x*3);
	}
	if(tempR==0.0 && GMBLswitch) {
		
		rollvectorr=_V(0.8*ang_vel.z*3,0.0,0.0);
	}
	
//**************************************************************	
// Sets thrust vectors by simply adding up all the axis deflection vectors and the 
// "neutral" default vector
	SetThrusterDir(th_hover[0],pitchvectorm+rollvectorr+_V( 0,1,0));//4
	SetThrusterDir(th_hover[1],pitchvectorm+rollvectorr+_V( 0,1,0));

//	sprintf (oapiDebugString(), "pitch vector: %f, roll vel: %f", tempP, ang_vel.z);

}

// ==============================================================
// API interface
// ==============================================================
// ==============================================================
// DLL entry point
// ==============================================================

BOOL WINAPI DllMain (HINSTANCE hModule,
					 DWORD ul_reason_for_call,
					 LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		InitGParam(hModule);
		break;
	case DLL_PROCESS_DETACH:
		FreeGParam();
		break;
	}
	return TRUE;
}

// ==============================================================
// API interface
// ==============================================================

DLLCLBK VESSEL *ovcInit (OBJHANDLE hvessel, int flightmodel)
{
	sat5_lmpkd *lem;

	if (!refcount++) {
		LEMLoadMeshes();
	}
	
	// VESSELSOUND 

	lem = new sat5_lmpkd(hvessel, flightmodel);
	return (VESSEL *) lem;
}

int sat5_lmpkd::ConsumeDirectKey (const char *keystate)

{
	if (KEYMOD_SHIFT (keystate)) 
	{
		return 0; 
	}
	else if (KEYMOD_CONTROL (keystate)) 
	{
	
	}
	else 
	{ 
		if (KEYDOWN (keystate, OAPI_KEY_J)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_J, 1.0)){
			if (status !=0)
			//bManualSeparate = true;
			return 1;
			}
		}
		if (KEYDOWN (keystate, OAPI_KEY_K)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_K, 1.0)){
				bToggleHatch = true;
				
			return 1;
			}
		}
		
		if (KEYDOWN (keystate, OAPI_KEY_1)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_1, 1.0)){
				
			return 1;
			}
		}
		if (KEYDOWN (keystate, OAPI_KEY_2)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_2, 1.0)){
				
				
				
			return 1;
			}
		}
		if (KEYDOWN (keystate, OAPI_KEY_3)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_3, 1.0)){
			}
			return 1;
			
		}
		
		if (KEYDOWN (keystate, OAPI_KEY_E)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_E, 1.0)){
		if (status==1){
					//ToggleEva = true;
				}
			return 1;
			}
		}

		if (KEYDOWN (keystate, OAPI_KEY_7)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_7, 1.0))
				lmpview = true;
			return 1;
		}
		if (KEYDOWN (keystate, OAPI_KEY_6)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_6, 1.0))
				cdrview = true;
			return 1;
		}

		//
		// keys used by LM landing programs P64 and P66
		//
		
		if (KEYDOWN (keystate, OAPI_KEY_MINUS)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_MINUS, 1.0)) {
				agc.ChangeDescentRate(-0.3077);
			}
			return 1;
		}

		if (KEYDOWN (keystate, OAPI_KEY_EQUALS)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_EQUALS, 1.0)) {
				agc.ChangeDescentRate(0.3077);
			}
			return 1;
		}

		if (KEYDOWN (keystate, OAPI_KEY_HOME)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_HOME, 1.0))
				//move the landing site downrange
				agc.RedesignateTarget(0,1.0);
			return 1;
		}

		if (KEYDOWN (keystate, OAPI_KEY_END)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_END, 1.0))
				//move the landing site closer
				agc.RedesignateTarget(0,-1.0);
			return 1;
		}

		if (KEYDOWN (keystate, OAPI_KEY_DELETE)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_DELETE, 1.0))
				// move landing site left
				agc.RedesignateTarget(1,1.0);
			return 1;
		}

		if (KEYDOWN (keystate, OAPI_KEY_INSERT)) {
			if (oapiAcceptDelayedKey (OAPI_KEY_INSERT, 1.0))
				// move landing site right
				agc.RedesignateTarget(1,-1.0);
			return 1;
		}

		//
		// end of keys used by LM landing programs P64 and P66
		//
	}
	return 0;
}

//
// Timestep code.
//

void sat5_lmpkd::PostStep(double simt, double simdt, double mjd)

{
	if (FirstTimestep)
	{
		DoFirstTimestep();
		FirstTimestep = false;
		return;
	}

	//
	// If we switch focus to the astronaut immediately after creation, Orbitersound doesn't
	// play any sounds, or plays LEM sounds rather than astronauts sounds. We need to delay
	// the focus switch a few timesteps to allow it to initialise properly in the background.
	//

	if (SwitchFocusToLeva > 0 && hLEVA) {
		SwitchFocusToLeva--;
		if (!SwitchFocusToLeva) {
			oapiSetFocusObject(hLEVA);
		}
	}

	SetView();
	
	VECTOR3 RVEL = _V(0.0,0.0,0.0);
	GetRelativeVel(GetGravityRef(),RVEL);

	MissionTime = MissionTime + oapiGetSimStep();

	agc.Timestep(MissionTime);
	dsky.Timestep(MissionTime);
	
	actualVEL = (sqrt(RVEL.x *RVEL.x + RVEL.y * RVEL.y + RVEL.z * RVEL.z)/1000*3600);
	actualALT = GetAltitude() ;
	if (actualALT < 1470){
		actualVEL = actualVEL-1470+actualALT;
	}
	if (GroundContact()){
	actualVEL =0;
	}
	if (status !=0 && Sswitch2){
				bManualSeparate = true;
	}
	actualFUEL = GetFuelMass()/GetMaxFuelMass()*100;	
	double dTime,aSpeed,DV,aALT,DVV,DVA;//aVAcc;aHAcc;ALTN1;SPEEDN1;aTime aVSpeed;
		aSpeed = actualVEL/3600*1000;
		aALT=actualALT;
		dTime=simt-aTime;
		if(dTime > 0.1){
			DV= aSpeed - SPEEDN1;
			aHAcc= (DV / dTime);
			DVV = aALT - ALTN1;
			aVSpeed = DVV / dTime;
			DVA = aVSpeed- VSPEEDN1;
			

			aVAcc=(DVA/ dTime);
			aTime = simt;
			VSPEEDN1 = aVSpeed;
			ALTN1 = aALT;
			SPEEDN1= aSpeed;
		}
	AttitudeLaunch1();
	if( toggleRCS){
			if(P44switch){
			SetAttitudeMode(2);
			toggleRCS =false;
			}
			else if (!P44switch){
			SetAttitudeMode(1);
			toggleRCS =false;
			}
		}
		if (GetAttitudeMode()==1){
		P44switch=false;
		}
		else if (GetAttitudeMode()==2 ){
		P44switch=true;
		}
	if(Sswitch1){
		Sswitch1=false;
		Undock(0);
		}
	if (GetNavmodeState(NAVMODE_KILLROT)&& !ATT2switch && !ATT3switch){
		if (GetThrusterLevel(th_att_rot[10]) <0.00001 && GetThrusterLevel(th_att_rot[18]) <0.00001 ){
			DeactivateNavmode(NAVMODE_KILLROT);
		}
	}
	if (stage == 0)	{
		if (ENGARMswitch && !DESHE1switch && !DESHE2switch && ED1switch && ED2switch && ED5switch){
			SetThrusterResource(th_hover[0], ph_Dsc);
			SetThrusterResource(th_hover[1], ph_Dsc);
		} else {
			SetThrusterResource(th_hover[0], NULL);
			SetThrusterResource(th_hover[1], NULL);
		}
		
		if(!AFEED1switch && !AFEED2switch && !AFEED3switch && !AFEED4switch){
			SetRCS(ph_rcslm0);
		} else {
			SetRCS(ph_rcslm1);
		}
	}else if (stage == 1 || stage == 5)	{
		if(!AFEED1switch && !AFEED2switch && !AFEED3switch && !AFEED4switch){
			SetRCS(ph_rcslm0);
		}
		else{
			SetRCS(ph_rcslm1);
		}
		if (ENGARMswitch && !DESHE1switch && !DESHE2switch && ED1switch && ED2switch && ED5switch){
			SetThrusterResource(th_hover[0], ph_Dsc);
			SetThrusterResource(th_hover[1], ph_Dsc);
		} else {
			SetThrusterResource(th_hover[0], NULL);
			SetThrusterResource(th_hover[1], NULL);
		}
		if (EVA_IP){
			if(!hLEVA){
				ToggleEVA();
			}
			else{
				if(high){
				}
				else{
					high=true;
					SetTouchdownPoints (_V(0,-5,10), _V(-1,-5,-10), _V(1,-5,-10));
				}
			}
		}
		if (ToggleEva && GroundContact()){
			ToggleEVA();
		}
		
		if (bToggleHatch){
			VESSELSTATUS vs;
			GetStatus(vs);
			if (vs.status == 1){
				//PlayVesselWave(Scontact,NOLOOP,255);
				//SetLmVesselHoverStage2(vessel);
				if(high){
					high=false;
					SetTouchdownPoints (_V(0,-0,10), _V(-1,-0,-10), _V(1,-0,-10));
				}
				else{
					high=true;
					SetTouchdownPoints (_V(0,-5,10), _V(-1,-5,-10), _V(1,-5,-10));
				}
			}
			bToggleHatch=false;
		}

		if (actualALT < 5.3 && !ContactOK && actualALT > 5.0){
			Scontact.play();
			SetEngineLevel(ENGINE_HOVER,0);
			ContactOK = true;

		}
		if (CPswitch && HATCHswitch && EVAswitch && GroundContact()){
			ToggleEva = true;
			EVAswitch = false;
		}
		
		if (GetPropellantMass(ph_Dsc)<=50 && actualALT > 10){
			Abortswitch=true;
			SeparateStage(stage);
			SetEngineLevel(ENGINE_HOVER,1);
			ActivateNavmode(NAVMODE_HLEVEL);
			stage = 2;
			AbortFire();
		}
		if (bManualSeparate && !startimer){
			VESSELSTATUS vs;
			GetStatus(vs);

			if (vs.status == 1){
				countdown=simt;
				LunarAscent.play(NOLOOP,200);
				startimer=true;
				//vessel->SetTouchdownPoints (_V(0,-0,10), _V(-1,-0,-10), _V(1,-0,-10));
			}
			else{
				SeparateStage(stage);
				stage = 2;
			}
		}
		if ((simt-(10+countdown))>=0 && startimer ){
			StartAscent();
		}
		//sprintf (oapiDebugString(),"FUEL %d",GetPropellantMass(ph_Dsc));
	}
	else if (stage == 2) {
		if (AscentEngineArmed()) {
			SetThrusterResource(th_hover[0], ph_Asc);
			SetThrusterResource(th_hover[1], ph_Asc);
		} else {
			SetThrusterResource(th_hover[0], NULL);
			SetThrusterResource(th_hover[1], NULL);
		}
		if (!AscentRCSArmed()) {
			SetRCS(NULL);
		}
		else{
			SetRCS(ph_rcslm1);
		}
	}
	else if (stage == 3){
		if (AscentEngineArmed()) {
			SetThrusterResource(th_hover[0], ph_Asc);
			SetThrusterResource(th_hover[1], ph_Asc);
		} else {
			SetThrusterResource(th_hover[0], NULL);
			SetThrusterResource(th_hover[1], NULL);
		}
		if(!AscentRCSArmed()){
			SetRCS(NULL);
		}
		else{
			SetRCS(ph_rcslm1);
		}
	}
	else if (stage == 4)
	{	
	}
}

//
// Set GMBLswitch
//

void sat5_lmpkd::SetGimbal(bool setting)
{
	GMBLswitch=setting;
}

//
// Initiate ascent.
//

void sat5_lmpkd::StartAscent()

{
	SeparateStage(stage);
	stage = 2;
	SetEngineLevel(ENGINE_HOVER,1);
	startimer = false;

	LunarAscent.done();
}

//
// Scenario state functions.
//

void sat5_lmpkd::LoadStateEx (FILEHANDLE scn, void *vs)

{
    char *line;
	int	SwitchState;
	float ftcp;

		// default panel
	PanelId = 1;
	
	while (oapiReadScenario_nextline (scn, line)) {
        if (!strnicmp (line, "CONFIGURATION", 13)) {
            sscanf (line+13, "%d", &status);
		}
		else if (!strnicmp (line, "EVA", 3)) {
			EVA_IP = true;
		} 
		else if (!strnicmp (line, "CSWITCH", 7)) {
            SwitchState = 0;
			sscanf (line+7, "%d", &SwitchState);
			SetCSwitchState(SwitchState);
		} 
		else if (!strnicmp (line, "SSWITCH", 7)) {
            SwitchState = 0;
			sscanf (line+7, "%d", &SwitchState);
			SetSSwitchState(SwitchState);
		} else if (!strnicmp (line, "LPSWITCH", 8)) {
            SwitchState = 0;
			sscanf (line+8, "%d", &SwitchState);
			SetLPSwitchState(SwitchState);
		} else if (!strnicmp (line, "RPSWITCH", 8)) {
            SwitchState = 0;
			sscanf (line+8, "%d", &SwitchState);
			SetRPSwitchState(SwitchState);
		} else if (!strnicmp(line, "MISSNTIME", 9)) {
            sscanf (line+9, "%f", &ftcp);
			MissionTime = ftcp;
		}
		else if (!strnicmp(line, "UNMANNED", 8)) {
			int i;
			sscanf(line + 8, "%d", &i);
			Crewed = (i == 0);
		}
		else if (!strnicmp (line, "LANG", 4)) {
			strncpy (AudioLanguage, line + 4, 64);
		}
		else if (!strnicmp (line, "REALISM", 7)) {
			sscanf (line+7, "%d", &Realism);
		} 
		else if (!strnicmp(line, DSKY_START_STRING, sizeof(DSKY_START_STRING))) {
			dsky.LoadState(scn);
		}
		else if (!strnicmp(line, AGC_START_STRING, sizeof(AGC_START_STRING))) {
			agc.LoadState(scn);
		}
		else if (!strnicmp (line, "PANEL_ID", 8)) { 
			sscanf (line+8, "%d", &PanelId);
		} 
		else 
		{
            ParseScenarioLineEx (line, vs);
        }
    }

	switch (status) {
	case 0:
		stage=0;
		SetLmVesselDockStage();
		break;

	case 1:
		stage=1;
		SetLmVesselHoverStage();
		if (EVA_IP){
			SetupEVA();
		}
		break;
	case 2:
		stage=2;
		SetLmAscentHoverStage();
		break;
	}

	//
	// This may not be the best place for loading sounds, but it's the best place available
	// for now!
	//

	soundlib.SetLanguage(AudioLanguage);
	LoadDefaultSounds();
}

void sat5_lmpkd::SetClassCaps (FILEHANDLE cfg)
{
	SetLmVesselDockStage();
}

void sat5_lmpkd::PostCreation ()

{
	soundlib.SetLanguage(AudioLanguage);
	LoadDefaultSounds();
}

void sat5_lmpkd::SetStateEx(const void *status)

{
	VESSELSTATUS2 *vslm = (VESSELSTATUS2 *) status;

	DefSetStateEx(status);
}

void sat5_lmpkd::SaveState (FILEHANDLE scn)

{
	SaveDefaultState (scn);	
	oapiWriteScenario_int (scn, "CONFIGURATION", status);
	if (EVA_IP){
		oapiWriteScenario_int (scn, "EVA", int(TO_EVA));
	}

	oapiWriteScenario_int (scn, "CSWITCH",  GetCSwitchState());
	oapiWriteScenario_int (scn, "SSWITCH",  GetSSwitchState());
	oapiWriteScenario_int (scn, "LPSWITCH",  GetLPSwitchState());
	oapiWriteScenario_int (scn, "RPSWITCH",  GetRPSwitchState());
	oapiWriteScenario_float (scn, "MISSNTIME", MissionTime);
	oapiWriteScenario_string (scn, "LANG", AudioLanguage);
	oapiWriteScenario_int (scn, "PANEL_ID", PanelId);

	if (Realism != REALISM_DEFAULT) {
		oapiWriteScenario_int (scn, "REALISM", Realism);
	}

	if (!Crewed) {
		oapiWriteScenario_int (scn, "UNMANNED", 1);
	}

	dsky.SaveState(scn);
	agc.SaveState(scn);
}

bool sat5_lmpkd::LoadGenericCockpit ()

{
	SetCameraRotationRange(0.0, 0.0, 0.0, 0.0);
	SetCameraDefaultDirection(_V(0.0, 0.0, 1.0));
	InVC = false;
	InPanel = false;
	return true;
}

//
// Transfer important data from the CSM to the LEM when the LEM is first
// created.
//

void sat5_lmpkd::SetLanderData(LemSettings &ls)

{
	MissionTime = ls.MissionTime;
	agc.SetApolloNo(ls.MissionNo);
	agc.SetDesiredLanding(ls.LandingLatitude, ls.LandingLongitude, ls.LandingAltitude);
	strncpy (AudioLanguage, ls.language, 64);
	soundlib.SetLanguage(AudioLanguage);
	Crewed = ls.Crewed;
	AutoSlow = ls.AutoSlow;
	Realism = ls.Realism;
}

// ==============================================================
// API interface
// ==============================================================

DLLCLBK bool ovcLoadPanel (VESSEL *vessel, int id)
{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;

	return lem->LoadPanel(id);
}


DLLCLBK bool ovcPanelMouseEvent (VESSEL *vessel, int id, int event, int mx, int my)

{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;

	return lem->PanelMouseEvent(id, event, mx, my);
}


DLLCLBK bool ovcPanelRedrawEvent (VESSEL *vessel, int id, int event, SURFHANDLE surf)

{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;
	return lem->PanelRedrawEvent(id, event, surf);
}

DLLCLBK int ovcConsumeKey (VESSEL *vessel, const char *keystate)

{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;
	return lem->ConsumeDirectKey(keystate);
}

DLLCLBK void ovcTimestep (VESSEL *vessel, double simt)
{
	sat5_lmpkd *lem = (sat5_lmpkd *) vessel;
	lem->PostStep(simt, 0, 0);
}

DLLCLBK void ovcLoadStateEx (VESSEL *vessel, FILEHANDLE scn, VESSELSTATUS *vs)

{
	sat5_lmpkd *lem = (sat5_lmpkd *) vessel;
	lem->LoadStateEx(scn, (void *) vs);
}

DLLCLBK void ovcSetClassCaps (VESSEL *vessel, FILEHANDLE cfg)
{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;

	lem->SetClassCaps(cfg);
}

DLLCLBK void ovcSaveState (VESSEL *vessel, FILEHANDLE scn)

{
	sat5_lmpkd *lem = (sat5_lmpkd *)vessel;
	lem->SaveState(scn);
}

