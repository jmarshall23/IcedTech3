// IceWorldSpawn.cpp
//

#include "precompiled.h"
#include "Icegame_local.h"

/*
================
iceWorldspawn::~iceWorldspawn
================
*/
iceWorldspawn::~iceWorldspawn() {

}

/*
================
iceWorldspawn::Spawn
================
*/
void iceWorldspawn::Spawn(void) {
	// Load in the atmosphere defnitions for this level.
	defaultAtmosphere = declManager->FindAtmosphere(spawnArgs.GetString("atmosphere", "doom/default"), false);

	// Set the atmosphere properties for this world. 
	iceWorldAtmosphere_t atmosphere;
	atmosphere.fogColor = defaultAtmosphere->GetFogColor();
	atmosphere.fogDensity = defaultAtmosphere->GetFogDensity();
	atmosphere.ambientLightColor = defaultAtmosphere->GetAmbientLightColor();
	atmosphere.fogStart = defaultAtmosphere->GetFogStart();
	atmosphere.fogEnd = defaultAtmosphere->GetFogEnd();
	gameRenderWorld->SetAtmosphere(atmosphere);
}
