/*

Base class for all C++ objects.  Provides fast run-time type checking and run-time
instancing of objects.

*/

#include "precompiled.h"
#pragma hdrstop

#include "../Game_local.h"

#include "NoGameTypeInfo.h"

/***********************************************************************

  idTypeInfo

***********************************************************************/

// this is the head of a singly linked list of all the idTypes
static idTypeInfo* typelist = NULL;
static idHierarchy<idTypeInfo>	classHierarchy;
static int						eventCallbackMemory = 0;

/*
================
idTypeInfo::idClassType()

Constructor for class.  Should only be called from CLASS_DECLARATION macro.
Handles linking class definition into class hierarchy.  This should only happen
at startup as idTypeInfos are statically defined.  Since static variables can be
initialized in any order, the constructor must handle the case that subclasses
are initialized before superclasses.
================
*/
idTypeInfo::idTypeInfo(const char* classname, const char* superclass, idEventFunc<idClass>* eventCallbacks, idClass* (*CreateInstance)(void),
	// RAVEN BEGIN
	// bdube: added states
	void (idClass::* Spawn)(void),
	rvStateFunc<idClass>* stateCallbacks,
	void (idClass::* Save)(idSaveGame* savefile) const, void (idClass::* Restore)(idRestoreGame* savefile)) {
	// RAVEN END

	idTypeInfo* type;
	idTypeInfo** insert;

	this->classname = classname;
	this->superclass = superclass;
	this->eventCallbacks = eventCallbacks;
	this->eventMap = NULL;
	this->Spawn = Spawn;
	this->Save = Save;
	this->Restore = Restore;
	this->CreateInstance = CreateInstance;
	this->super = idClass::GetClass(superclass);
	this->freeEventMap = false;
	typeNum = 0;
	lastChild = 0;

	// RAVEN BEGIN
	// bdube: added states
	this->stateCallbacks = stateCallbacks;
	// RAVEN END

		// Check if any subclasses were initialized before their superclass
	for (type = typelist; type != NULL; type = type->next) {
		if ((type->super == NULL) && !idStr::Cmp(type->superclass, this->classname) &&
			idStr::Cmp(type->classname, "idClass")) {
			type->super = this;
		}
	}

	// Insert sorted
	for (insert = &typelist; *insert; insert = &(*insert)->next) {
		assert(idStr::Cmp(classname, (*insert)->classname));
		if (idStr::Cmp(classname, (*insert)->classname) < 0) {
			next = *insert;
			*insert = this;
			break;
		}
	}
	if (!*insert) {
		*insert = this;
		next = NULL;
	}
}

/*
================
idTypeInfo::~idTypeInfo
================
*/
idTypeInfo::~idTypeInfo() {
	Shutdown();
}

/*
================
idTypeInfo::Init

Initializes the event callback table for the class.  Creates a
table for fast lookups of event functions.  Should only be called once.
================
*/
void idTypeInfo::Init(void) {
	idTypeInfo* c;
	idEventFunc<idClass>* def;
	int						ev;
	int						i;
	bool* set;
	int						num;

	if (eventMap) {
		// we've already been initialized by a subclass
		return;
	}

	// make sure our superclass is initialized first
	if (super && !super->eventMap) {
		super->Init();
	}

	// add to our node hierarchy
	if (super) {
		node.ParentTo(super->node);
	}
	else {
		node.ParentTo(classHierarchy);
	}
	node.SetOwner(this);

	// keep track of the number of children below each class
	for (c = super; c != NULL; c = c->super) {
		c->lastChild++;
	}

	// if we're not adding any new event callbacks, we can just use our superclass's table
	if ((!eventCallbacks || !eventCallbacks->event) && super) {
		eventMap = super->eventMap;
		return;
	}

	// set a flag so we know to delete the eventMap table
	freeEventMap = true;

	// Allocate our new table.  It has to have as many entries as there
	// are events.  NOTE: could save some space by keeping track of the maximum
	// event that the class responds to and doing range checking.
	num = idEventDef::NumEventCommands();
	eventMap = new eventCallback_t[num];
	memset(eventMap, 0, sizeof(eventCallback_t) * num);
	eventCallbackMemory += sizeof(eventCallback_t) * num;

	// allocate temporary memory for flags so that the subclass's event callbacks
	// override the superclass's event callback
	set = new bool[num];
	memset(set, 0, sizeof(bool) * num);

	// go through the inheritence order and copies the event callback function into
	// a list indexed by the event number.  This allows fast lookups of
	// event functions.
	for (c = this; c != NULL; c = c->super) {
		def = c->eventCallbacks;
		if (!def) {
			continue;
		}

		// go through each entry until we hit the NULL terminator
		for (i = 0; def[i].event != NULL; i++) {
			ev = def[i].event->GetEventNum();

			if (set[ev]) {
				continue;
			}
			set[ev] = true;
			eventMap[ev] = def[i].function;
		}
	}

	delete[] set;
}

/*
================
idTypeInfo::Shutdown

Should only be called when DLL or EXE is being shutdown.
Although it cleans up any allocated memory, it doesn't bother to remove itself
from the class list since the program is shutting down.
================
*/
void idTypeInfo::Shutdown() {
	// free up the memory used for event lookups
	if (eventMap) {
		if (freeEventMap) {
			delete[] eventMap;
		}
		eventMap = NULL;
	}
	typeNum = 0;
	lastChild = 0;
}


/***********************************************************************

  idClass

***********************************************************************/

const idEventDef EV_Remove("<immediateremove>", NULL);
const idEventDef EV_SafeRemove("remove", NULL);

ABSTRACT_DECLARATION(NULL, idClass)
EVENT(EV_Remove, idClass::Event_Remove)
EVENT(EV_SafeRemove, idClass::Event_SafeRemove)
END_CLASS

CLASS_STATES_DECLARATION(idClass)
END_CLASS_STATES

// alphabetical order
idList<idTypeInfo*>	idClass::types;
// typenum order
idList<idTypeInfo*>	idClass::typenums;

bool	idClass::initialized = false;
int		idClass::typeNumBits = 0;
int		idClass::memused = 0;
int		idClass::numobjects = 0;

/*
================
idClass::CallSpawn
================
*/
void idClass::CallSpawn(void) {
	idTypeInfo* type;

	type = GetType();
	CallSpawnFunc(type);
}

/*
================
idClass::CallSpawnFunc
================
*/
classSpawnFunc_t idClass::CallSpawnFunc(idTypeInfo* cls) {
	classSpawnFunc_t func;

	if (cls->super) {
		func = CallSpawnFunc(cls->super);
		if (func == cls->Spawn) {
			// don't call the same function twice in a row.
			// this can happen when subclasses don't have their own spawn function.
			return func;
		}
	}

	// RAVEN BEGIN
	// hmmm.... stompage of memory has occured
	assert(cls->Spawn != 0);
	// RAVEN END

	(this->*cls->Spawn)();

	return cls->Spawn;
}

/*
================
idClass::FindUninitializedMemory
================
*/
void idClass::FindUninitializedMemory(void) {
#ifdef ID_DEBUG_MEMORY
	unsigned long* ptr = ((unsigned long*)this) - 1;
	int size = *ptr;
	assert((size & 3) == 0);
	size >>= 2;
	for (int i = 0; i < size; i++) {
		if (ptr[i] == 0xcdcdcdcd) {
			// RAVEN BEGIN
			gameLocal.Warning("type '%s' has uninitialized variable (offset %d)", GetClassname(), i << 2);
			// RAVEN END
		}
	}
#endif
}

/*
================
idClass::Spawn
================
*/
void idClass::Spawn(void) {
}

/*
================
idClass::~idClass

Destructor for object.  Cancels any events that depend on this object.
================
*/
idClass::~idClass() {
	idEvent::CancelEvents(this);
}

/*
================
idClass::DisplayInfo_f
================
*/
void idClass::DisplayInfo_f(const idCmdArgs& args) {
	gameLocal.Printf("Class memory status: %i bytes allocated in %i objects\n", memused, numobjects);
}

/*
================
idClass::ListClasses_f
================
*/
void idClass::ListClasses_f(const idCmdArgs& args) {
	int			i;
	idTypeInfo* type;

	gameLocal.Printf("%-24s %-24s %-6s %-6s\n", "Classname", "Superclass", "Type", "Subclasses");
	gameLocal.Printf("----------------------------------------------------------------------\n");

	for (i = 0; i < types.Num(); i++) {
		type = types[i];
		gameLocal.Printf("%-24s // %-24s %6d %6d\n", type->classname, type->superclass, type->typeNum, type->lastChild - type->typeNum);
	}

	gameLocal.Printf("...%d classes", types.Num());
}

/*
================
idClass::CreateInstance
================
*/
idClass* idClass::CreateInstance(const char* name) {
	const idTypeInfo* type;
	idClass* obj;

	type = idClass::GetClass(name);
	if (!type) {
		return NULL;
	}

	obj = type->CreateInstance();
	return obj;
}

/*
================
idClass::Init

Should be called after all idTypeInfos are initialized, so must be called
manually upon game code initialization.  Tells all the idTypeInfos to initialize
their event callback table for the associated class.  This should only be called
once during the execution of the program or DLL.
================
*/
void idClass::Init(void) {
	idTypeInfo* c;
	int			num;
	// RAVEN BEGIN

	gameLocal.Printf("Initializing class hierarchy\n");

	if (initialized) {
		gameLocal.Printf("...already initialized\n");
		return;
	}

	// init the event callback tables for all the classes
	for (c = typelist; c != NULL; c = c->next) {
		// RAVEN BEGIN
		// jnewquist: Make sure the superclass was actually registered!
		if (c->super == NULL && (c->superclass && idStr::Cmp(c->superclass, "NULL"))) {
			common->Error("Superclass %s of %s was never registered!", c->superclass, c->classname);
		}
		// RAVEN END
		c->Init();
	}

	// number the types according to the class hierarchy so we can quickly determine if a class
	// is a subclass of another
	num = 0;
	for (c = classHierarchy.GetNext(); c != NULL; c = c->node.GetNext(), num++) {
		c->typeNum = num;
		c->lastChild += num;
	}

	// number of bits needed to send types over network
	typeNumBits = idMath::BitsForInteger(num);

	// create a list of the types so we can do quick lookups
	// one list in alphabetical order, one in typenum order
	types.SetGranularity(1);
	types.SetNum(num);
	typenums.SetGranularity(1);
	typenums.SetNum(num);
	num = 0;
	for (c = typelist; c != NULL; c = c->next, num++) {
		types[num] = c;
		typenums[c->typeNum] = c;
	}

	initialized = true;

	gameLocal.Printf("...%i classes, %i bytes for event callbacks\n", types.Num(), eventCallbackMemory);
}

/*
================
idClass::Shutdown
================
*/
void idClass::Shutdown(void) {
	idTypeInfo* c;

	for (c = typelist; c != NULL; c = c->next) {
		c->Shutdown();
	}
	types.Clear();
	typenums.Clear();

	initialized = false;
}

/*
================
idClass::new
================
*/
#ifdef ID_DEBUG_MEMORY
#undef new
#endif

void* idClass::operator new(size_t s) {
	int* p;

	s += sizeof(int);
	//RAVEN BEGIN
	//amccarthy: Added memory allocation tag
	p = (int*)Mem_Alloc(s);
	//RAVEN END
	*p = s;
	memused += s;
	numobjects++;

#ifdef ID_DEBUG_MEMORY
	unsigned long* ptr = (unsigned long*)p;
	int size = s;
	assert((size & 3) == 0);
	size >>= 3;
	for (int i = 1; i < size; i++) {
		ptr[i] = 0xcdcdcdcd;
	}
#endif

	return p + 1;
}

void* idClass::operator new(size_t s, int, int, char*, int) {
	int* p;

	s += sizeof(int);
	//RAVEN BEGIN
	//amccarthy: Added memory allocation tag
	p = (int*)Mem_Alloc(s);
	//RAVEN END
	*p = s;
	memused += s;
	numobjects++;

#ifdef ID_DEBUG_MEMORY
	unsigned long* ptr = (unsigned long*)p;
	int size = s;
	assert((size & 3) == 0);
	size >>= 3;
	for (int i = 1; i < size; i++) {
		ptr[i] = 0xcdcdcdcd;
	}
#endif

	return p + 1;
}

#ifdef ID_DEBUG_MEMORY
#define new ID_DEBUG_NEW
#endif

/*
================
idClass::delete
================
*/
void idClass::operator delete(void* ptr) {
	int* p;

	if (ptr) {
		p = ((int*)ptr) - 1;
		memused -= *p;
		numobjects--;
		Mem_Free(p);
	}
}

void idClass::operator delete(void* ptr, int, int, char*, int) {
	int* p;

	if (ptr) {
		p = ((int*)ptr) - 1;
		memused -= *p;
		numobjects--;
		Mem_Free(p);
	}
}

/*
================
idClass::GetClass

Returns the idTypeInfo for the name of the class passed in.  This is a static function
so it must be called as idClass::GetClass( classname )
================
*/
idTypeInfo* idClass::GetClass(const char* name) {
	idTypeInfo* c;
	int			order;
	int			mid;
	int			min;
	int			max;

	if (!initialized) {
		// idClass::Init hasn't been called yet, so do a slow lookup
		for (c = typelist; c != NULL; c = c->next) {
			if (!idStr::Cmp(c->classname, name)) {
				return c;
			}
		}
	}
	else {
		// do a binary search through the list of types
		min = 0;
		max = types.Num() - 1;
		while (min <= max) {
			mid = (min + max) / 2;
			c = types[mid];
			order = idStr::Cmp(c->classname, name);
			if (!order) {
				return c;
			}
			else if (order > 0) {
				max = mid - 1;
			}
			else {
				min = mid + 1;
			}
		}
	}

	return NULL;
}

/*
================
idClass::GetType
================
*/
idTypeInfo* idClass::GetType(const int typeNum) {
	idTypeInfo* c;

	if (!initialized) {
		for (c = typelist; c != NULL; c = c->next) {
			if (c->typeNum == typeNum) {
				return c;
			}
		}
	}
	else if ((typeNum >= 0) && (typeNum < types.Num())) {
		return typenums[typeNum];
	}

	return NULL;
}

/*
================
idClass::GetClassname

Returns the text classname of the object.
================
*/
const char* idClass::GetClassname(void) const {
	idTypeInfo* type;

	type = GetType();
	return type->classname;
}

/*
================
idClass::GetSuperclass

Returns the text classname of the superclass.
================
*/
const char* idClass::GetSuperclass(void) const {
	idTypeInfo* cls;

	cls = GetType();
	return cls->superclass;
}

/*
================
idClass::CancelEvents
================
*/
void idClass::CancelEvents(const idEventDef* ev) {
	idEvent::CancelEvents(this, ev);
}

// RAVEN BEGIN
// abahr:
/*
================
idClass::EventIsPosted
================
*/
bool idClass::EventIsPosted(const idEventDef* ev) const {
	return idEvent::EventIsPosted(this, ev);
}
// RAVEN END

/*
================
idClass::PostEventArgs
================
*/
bool idClass::PostEventArgs(const idEventDef* ev, int time, int numargs, ...) {
	idTypeInfo* c;
	idEvent* event;
	va_list		args;

	assert(ev);

	if (!idEvent::initialized) {
		return false;
	}

	c = GetType();
	if (!c->eventMap[ev->GetEventNum()]) {
		// we don't respond to this event, so ignore it
		return false;
	}

	// we service events on the client to avoid any bad code filling up the event pool
	// we don't want them processed usually, unless when the map is (re)loading.
	// we allow threads to run fine, though.
// RAVEN BEGIN
// bdube: added a check to see if this is a client entity
// jnewquist: Use accessor for static class type 
	bool isClient = !IsClient() && gameLocal.isClient;

#ifdef _XENON
	// nrausch: We want to allow selectWeapon to pass through, but that's all
	if (idStr::Cmp(ev->GetName(), "selectWeapon") == 0) {
		isClient = false;
	}
#endif

	if (isClient && (gameLocal.GameState() != GAMESTATE_STARTUP) && !IsType(idThread::GetClassType())) {
		// RAVEN END
		return true;
	}

	va_start(args, numargs);
	event = idEvent::Alloc(ev, numargs, args);
	va_end(args);

	event->Schedule(this, c, time);

	return true;
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time) {
	return PostEventArgs(ev, time, 0);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1) {
	return PostEventArgs(ev, time, 1, &arg1);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2) {
	return PostEventArgs(ev, time, 2, &arg1, &arg2);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3) {
	return PostEventArgs(ev, time, 3, &arg1, &arg2, &arg3);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4) {
	return PostEventArgs(ev, time, 4, &arg1, &arg2, &arg3, &arg4);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5) {
	return PostEventArgs(ev, time, 5, &arg1, &arg2, &arg3, &arg4, &arg5);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6) {
	return PostEventArgs(ev, time, 6, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7) {
	return PostEventArgs(ev, time, 7, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7);
}

/*
================
idClass::PostEventMS
================
*/
bool idClass::PostEventMS(const idEventDef* ev, int time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8) {
	return PostEventArgs(ev, time, 8, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time) {
	return PostEventArgs(ev, SEC2MS(time), 0);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1) {
	return PostEventArgs(ev, SEC2MS(time), 1, &arg1);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2) {
	return PostEventArgs(ev, SEC2MS(time), 2, &arg1, &arg2);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3) {
	return PostEventArgs(ev, SEC2MS(time), 3, &arg1, &arg2, &arg3);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4) {
	return PostEventArgs(ev, SEC2MS(time), 4, &arg1, &arg2, &arg3, &arg4);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5) {
	return PostEventArgs(ev, SEC2MS(time), 5, &arg1, &arg2, &arg3, &arg4, &arg5);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6) {
	return PostEventArgs(ev, SEC2MS(time), 6, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7) {
	return PostEventArgs(ev, SEC2MS(time), 7, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7);
}

/*
================
idClass::PostEventSec
================
*/
bool idClass::PostEventSec(const idEventDef* ev, float time, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8) {
	return PostEventArgs(ev, SEC2MS(time), 8, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8);
}

/*
================
idClass::ProcessEventArgs
================
*/
bool idClass::ProcessEventArgs(const idEventDef* ev, int numargs, ...) {
	idTypeInfo* c;
	int			num;
	intptr_t	data[D_EVENT_MAXARGS];
	va_list		args;

	assert(ev);
	assert(idEvent::initialized);

	c = GetType();
	num = ev->GetEventNum();
	if (!c->eventMap[num]) {
		// we don't respond to this event, so ignore it
		return false;
	}

	va_start(args, numargs);
	idEvent::CopyArgs(ev, numargs, args, data);
	va_end(args);

	ProcessEventArgPtr(ev, data);

	return true;
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev) {
	return ProcessEventArgs(ev, 0);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1) {
	return ProcessEventArgs(ev, 1, &arg1);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2) {
	return ProcessEventArgs(ev, 2, &arg1, &arg2);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3) {
	return ProcessEventArgs(ev, 3, &arg1, &arg2, &arg3);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4) {
	return ProcessEventArgs(ev, 4, &arg1, &arg2, &arg3, &arg4);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5) {
	return ProcessEventArgs(ev, 5, &arg1, &arg2, &arg3, &arg4, &arg5);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6) {
	return ProcessEventArgs(ev, 6, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7) {
	return ProcessEventArgs(ev, 7, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7);
}

/*
================
idClass::ProcessEvent
================
*/
bool idClass::ProcessEvent(const idEventDef* ev, idEventArg arg1, idEventArg arg2, idEventArg arg3, idEventArg arg4, idEventArg arg5, idEventArg arg6, idEventArg arg7, idEventArg arg8) {
	return ProcessEventArgs(ev, 8, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8);
}

/*
================
idClass::ProcessEventArgPtr
================
*/
bool idClass::ProcessEventArgPtr(const idEventDef* ev, intptr_t* data) {
	idTypeInfo* c;
	int			num;
	eventCallback_t	callback;

	assert(ev);
	assert(idEvent::initialized);

	if (g_debugTriggers.GetBool() && (ev == &EV_Activate) && IsType(idEntity::GetClassType())) {
		const idEntity* ent = *reinterpret_cast<idEntity**>(data);
		gameLocal.Printf("%d: '%s' activated by '%s'\n", gameLocal.framenum, static_cast<idEntity*>(this)->GetName(), ent ? ent->GetName() : "NULL");
	}

	c = GetType();
	num = ev->GetEventNum();
	if (!c->eventMap[num]) {
		// we don't respond to this event, so ignore it
		return false;
	}

	callback = c->eventMap[num];

	switch (ev->GetFormatspecIndex()) {
	case 1 << D_EVENT_MAXARGS:
		(this->*callback)();
		break;

		// generated file - see CREATE_EVENT_CODE
#include "Callbacks.cpp"

	default:
		gameLocal.Warning("Invalid formatspec on event '%s'", ev->GetName());
		break;
	}

	return true;

}

/*
================
idClass::Event_Remove
================
*/
void idClass::Event_Remove(void) {
	delete this;
}

/*
================
idClass::Event_SafeRemove
================
*/
void idClass::Event_SafeRemove(void) {
	// Forces the remove to be done at a safe time
	PostEventMS(&EV_Remove, 0);
}

// RAVEN BEGIN
// bdube: client entities
/*
================
idClass::IsClient
================
*/
bool idClass::IsClient(void) const {
	return false;
}

/*
================
idClass::ProcessState
================
*/
stateResult_t idClass::ProcessState(const rvStateFunc<idClass>* state, const stateParms_t& parms) {
	return (this->*(state->function)) (parms);
}

stateResult_t idClass::ProcessState(const char* name, const stateParms_t& parms) {
	int				i;
	idTypeInfo* cls;

	for (cls = GetType(); cls; cls = cls->super) {
		for (i = 0; cls->stateCallbacks[i].function; i++) {
			if (!idStr::Icmp(cls->stateCallbacks[i].name, name)) {
				return (this->*(cls->stateCallbacks[i].function)) (parms);
			}
		}
	}

	return SRESULT_ERROR;
}

/*
================
idClass::FindState
================
*/
const rvStateFunc<idClass>* idClass::FindState(const char* name) const {
	int				i;
	idTypeInfo* cls;

	for (cls = GetType(); cls; cls = cls->super) {
		for (i = 0; cls->stateCallbacks[i].function; i++) {
			if (!idStr::Icmp(cls->stateCallbacks[i].name, name)) {
				return &cls->stateCallbacks[i];
			}
		}
	}

	return NULL;
}

/*
================
idClass::RegisterClasses
================
*/
void idClass::RegisterClasses(void)
{
// jmarshall: prevent deadstripping.
#define REGISTER(name) void Register_##name(void); Register_##name();
	REGISTER(idClass)
	REGISTER(idPhysics)
	REGISTER(idEntity)
	REGISTER(idCamera)
	REGISTER(idForce)
	REGISTER(idForce_Constant)
	REGISTER(idForce_Drag)
	REGISTER(idForce_Field)
	REGISTER(idForce_Spring)
	REGISTER(idPhysics_Static)
	REGISTER(idPhysics_StaticMulti)
	REGISTER(idPhysics_Base)
	REGISTER(idPhysics_Actor)
	REGISTER(idPhysics_Monster)
	REGISTER(idPhysics_Player)
	REGISTER(idPhysics_Parametric)
	REGISTER(idPhysics_RigidBody)
	REGISTER(idPhysics_AF)
	REGISTER(idAnimatedEntity)
	REGISTER(idCursor3D)
	REGISTER(idMultiModelAF)
	REGISTER(idChain)
	REGISTER(idAFAttachment)
	REGISTER(idAFEntity_Base)
	REGISTER(idAFEntity_Gibbable)
	REGISTER(idAFEntity_Generic)
	REGISTER(idAFEntity_WithAttachedHead)
	REGISTER(idAFEntity_Vehicle)
	REGISTER(idAFEntity_VehicleSimple)
	REGISTER(idAFEntity_VehicleFourWheels)
	REGISTER(idAFEntity_VehicleSixWheels)
	REGISTER(idAFEntity_SteamPipe)
	REGISTER(idAFEntity_ClawFourFingers)
	REGISTER(idSpawnableEntity)
	REGISTER(idPlayerStart)
	REGISTER(idActivator)
	REGISTER(idPathCorner)
	REGISTER(idDamagable)
	REGISTER(idExplodable)
	REGISTER(idSpring)
	REGISTER(idForceField)
	REGISTER(idAnimated)
	REGISTER(idStaticEntity)
	REGISTER(idFuncEmitter)
	REGISTER(idFuncSmoke)
	REGISTER(idFuncSplat)
	REGISTER(idTextEntity)
	REGISTER(idLocationEntity)
	REGISTER(idLocationSeparatorEntity)
	REGISTER(idVacuumSeparatorEntity)
	REGISTER(idVacuumEntity)
	REGISTER(idBeam)
	REGISTER(idLiquid)
	REGISTER(idShaking)
	REGISTER(idEarthQuake)
	REGISTER(idFuncPortal)
	REGISTER(idFuncAASPortal)
	REGISTER(idFuncAASObstacle)
	REGISTER(idFuncRadioChatter)
	REGISTER(idPhantomObjects)
	REGISTER(idActor)
	REGISTER(idProjectile)
	REGISTER(idGuidedProjectile)
	REGISTER(idSoulCubeMissile)
	REGISTER(idBFGProjectile)
	REGISTER(idDebris)
	REGISTER(idWeapon)
	REGISTER(idLight)
	REGISTER(idWorldspawn)
	REGISTER(idItem)
	REGISTER(idItemPowerup)
	REGISTER(idObjective)
	REGISTER(idVideoCDItem)
	REGISTER(idPDAItem)
	REGISTER(idMoveableItem)
	REGISTER(idMoveablePDAItem)
	REGISTER(idItemRemover)
	REGISTER(idObjectiveComplete)
	REGISTER(idPlayer)
	REGISTER(idMover)
	REGISTER(idSplinePath)
	REGISTER(idElevator)
	REGISTER(idMover_Binary)
	REGISTER(idDoor)
	REGISTER(idPlat)
	REGISTER(idMover_Periodic)
	REGISTER(idRotater)
	REGISTER(idBobber)
	REGISTER(idPendulum)
	REGISTER(idRiser)
	REGISTER(idCameraView)
	REGISTER(idCameraAnim)
	REGISTER(idMoveable)
	REGISTER(idBarrel)
	REGISTER(idExplodingBarrel)
	REGISTER(idTarget)
	REGISTER(idTarget_Remove)
	REGISTER(idTarget_Show)
	REGISTER(idTarget_Damage)
	REGISTER(idTarget_SessionCommand)
	REGISTER(idTarget_EndLevel)
	REGISTER(idTarget_WaitForButton)
	REGISTER(idTarget_SetGlobalShaderTime)
	REGISTER(idTarget_SetShaderParm)
	REGISTER(idTarget_SetShaderTime)
	REGISTER(idTarget_FadeEntity)
	REGISTER(idTarget_LightFadeIn)
	REGISTER(idTarget_LightFadeOut)
	REGISTER(idTarget_Give)
	REGISTER(idTarget_GiveEmail)
	REGISTER(idTarget_SetModel)
	REGISTER(idTarget_SetInfluence)
	REGISTER(idTarget_SetKeyVal)
	REGISTER(idTarget_SetFov)
	REGISTER(idTarget_SetPrimaryObjective)
	REGISTER(idTarget_LockDoor)
	REGISTER(idTarget_CallObjectFunction)
	REGISTER(idTarget_EnableLevelWeapons)
	REGISTER(idTarget_Tip)
	REGISTER(idTarget_GiveSecurity)
	REGISTER(idTarget_RemoveWeapons)
	REGISTER(idTarget_LevelTrigger)
	REGISTER(idTarget_EnableStamina)
	REGISTER(idTarget_FadeSoundClass)
	REGISTER(idTrigger)
	REGISTER(idTrigger_Multi)
	REGISTER(idTrigger_EntityName)
	REGISTER(idTrigger_Timer)
	REGISTER(idTrigger_Count)
	REGISTER(idTrigger_Hurt)
	REGISTER(idTrigger_Fade)
	REGISTER(idTrigger_Touch)
	REGISTER(idSound)
	REGISTER(idEntityFx)
	REGISTER(idTeleporter)
	REGISTER(idSecurityCamera)
	REGISTER(idBrittleFracture)
	REGISTER(idAI)
	REGISTER(idCombatNode)
	REGISTER(idTestModel)
	REGISTER(idThread)
	REGISTER(iceWorldspawn)
	REGISTER(idAI_Vagary)
	REGISTER(idWeaponBase)
	REGISTER(idWeaponFists)
	REGISTER(idWeaponPistol)
	REGISTER(idWeaponShotgun)
	REGISTER(idWeaponMachinegun)
#undef REGISTER
}

// RAVEN END
