// Icegame_local.cpp
//

#include "precompiled.h"
#include "Icegame_local.h"

CLASS_DECLARATION(idWorldspawn, iceWorldspawn)
END_CLASS

void iceGameLocal::Init(void) {
	idGameLocal::Init();

	common->Printf("-------- iceGameLocal::Init ---------\n");
}