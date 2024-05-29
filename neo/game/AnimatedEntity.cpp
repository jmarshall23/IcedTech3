#include "precompiled.h"

#include "Game_local.h"

/*
===============================================================================

  idAnimatedEntity

===============================================================================
*/

const idEventDef EV_GetJointHandle("getJointHandle", "s", 'd');
const idEventDef EV_ClearAllJoints("clearAllJoints");
const idEventDef EV_ClearJoint("clearJoint", "d");
const idEventDef EV_SetJointPos("setJointPos", "ddv");
const idEventDef EV_SetJointAngle("setJointAngle", "ddv");
const idEventDef EV_GetJointPos("getJointPos", "d", 'v');
const idEventDef EV_GetJointAngle("getJointAngle", "d", 'v');

CLASS_DECLARATION(idEntity, idAnimatedEntity)
EVENT(EV_GetJointHandle, idAnimatedEntity::Event_GetJointHandle)
EVENT(EV_ClearAllJoints, idAnimatedEntity::Event_ClearAllJoints)
EVENT(EV_ClearJoint, idAnimatedEntity::Event_ClearJoint)
EVENT(EV_SetJointPos, idAnimatedEntity::Event_SetJointPos)
EVENT(EV_SetJointAngle, idAnimatedEntity::Event_SetJointAngle)
EVENT(EV_GetJointPos, idAnimatedEntity::Event_GetJointPos)
EVENT(EV_GetJointAngle, idAnimatedEntity::Event_GetJointAngle)
END_CLASS

/*
================
idAnimatedEntity::idAnimatedEntity
================
*/
idAnimatedEntity::idAnimatedEntity() {
	animator.SetEntity(this);
	damageEffects = NULL;
}

/*
================
idAnimatedEntity::~idAnimatedEntity
================
*/
idAnimatedEntity::~idAnimatedEntity() {
	damageEffect_t* de;

	for (de = damageEffects; de; de = damageEffects) {
		damageEffects = de->next;
		delete de;
	}
}

/*
================
idAnimatedEntity::Save

archives object for save game file
================
*/
void idAnimatedEntity::Save(idSaveGame* savefile) const {
	animator.Save(savefile);

	// Wounds are very temporary, ignored at this time
	//damageEffect_t			*damageEffects;
}

/*
================
idAnimatedEntity::Restore

unarchives object from save game file
================
*/
void idAnimatedEntity::Restore(idRestoreGame* savefile) {
	animator.Restore(savefile);

	// check if the entity has an MD5 model
	if (animator.ModelHandle()) {
		// set the callback to update the joints
		renderEntity.callback = idEntity::ModelCallback;
		animator.GetJoints(&renderEntity.numJoints, &renderEntity.joints);
		animator.GetBounds(gameLocal.time, renderEntity.bounds);
		if (modelDefHandle != -1) {
			gameRenderWorld->UpdateEntityDef(modelDefHandle, &renderEntity);
		}
	}
}

/*
================
idAnimatedEntity::ClientPredictionThink
================
*/
void idAnimatedEntity::ClientPredictionThink(void) {
	RunPhysics();
	UpdateAnimation();
	Present();
}

/*
================
idAnimatedEntity::Think
================
*/
void idAnimatedEntity::Think(void) {
	RunPhysics();
	UpdateAnimation();
	Present();
	UpdateDamageEffects();
}

/*
================
idAnimatedEntity::UpdateAnimation
================
*/
void idAnimatedEntity::UpdateAnimation(void) {
	// don't do animations if they're not enabled
	if (!(thinkFlags & TH_ANIMATE)) {
		return;
	}

	// is the model an MD5?
	if (!animator.ModelHandle()) {
		// no, so nothing to do
		return;
	}

	// call any frame commands that have happened in the past frame
	if (!fl.hidden) {
		animator.ServiceAnims(gameLocal.previousTime, gameLocal.time);
	}

	// if the model is animating then we have to update it
	if (!animator.FrameHasChanged(gameLocal.time)) {
		// still fine the way it was
		return;
	}

	// get the latest frame bounds
	animator.GetBounds(gameLocal.time, renderEntity.bounds);
	if (renderEntity.bounds.IsCleared() && !fl.hidden) {
		gameLocal.DPrintf("%d: inside out bounds\n", gameLocal.time);
	}

	// update the renderEntity
	UpdateVisuals();

	// the animation is updated
	animator.ClearForceUpdate();
}

/*
================
idAnimatedEntity::GetAnimator
================
*/
idAnimator* idAnimatedEntity::GetAnimator(void) {
	return &animator;
}

/*
================
idAnimatedEntity::SetModel
================
*/
void idAnimatedEntity::SetModel(const char* modelname) {
	FreeModelDef();

	renderEntity.hModel = animator.SetModel(modelname);
	if (!renderEntity.hModel) {
		idEntity::SetModel(modelname);
		return;
	}

	if (!renderEntity.customSkin) {
		renderEntity.customSkin = animator.ModelDef()->GetDefaultSkin();
	}

	// set the callback to update the joints
	renderEntity.callback = idEntity::ModelCallback;
	animator.GetJoints(&renderEntity.numJoints, &renderEntity.joints);
	animator.GetBounds(gameLocal.time, renderEntity.bounds);

	UpdateVisuals();
}

/*
=====================
idAnimatedEntity::GetJointWorldTransform
=====================
*/
bool idAnimatedEntity::GetJointWorldTransform(jointHandle_t jointHandle, int currentTime, idVec3& offset, idMat3& axis) {
	if (!animator.GetJointTransform(jointHandle, currentTime, offset, axis)) {
		return false;
	}

	ConvertLocalToWorldTransform(offset, axis);
	return true;
}

/*
==============
idAnimatedEntity::GetJointTransformForAnim
==============
*/
bool idAnimatedEntity::GetJointTransformForAnim(jointHandle_t jointHandle, int animNum, int frameTime, idVec3& offset, idMat3& axis) const {
	const idAnim* anim;
	int				numJoints;
	idJointMat* frame;

	anim = animator.GetAnim(animNum);
	if (!anim) {
		assert(0);
		return false;
	}

	numJoints = animator.NumJoints();
	if ((jointHandle < 0) || (jointHandle >= numJoints)) {
		assert(0);
		return false;
	}

	frame = (idJointMat*)_alloca16(numJoints * sizeof(idJointMat));
	gameEdit->ANIM_CreateAnimFrame(animator.ModelHandle(), anim->MD5Anim(0), renderEntity.numJoints, frame, frameTime, animator.ModelDef()->GetVisualOffset(), animator.RemoveOrigin());

	offset = frame[jointHandle].ToVec3();
	axis = frame[jointHandle].ToMat3();

	return true;
}

/*
==============
idAnimatedEntity::AddDamageEffect

  Dammage effects track the animating impact position, spitting out particles.
==============
*/
void idAnimatedEntity::AddDamageEffect(const trace_t& collision, const idVec3& velocity, const char* damageDefName) {
	jointHandle_t jointNum;
	idVec3 origin, dir, localDir, localOrigin, localNormal;
	idMat3 axis;

	if (!g_bloodEffects.GetBool() || renderEntity.joints == NULL) {
		return;
	}

	const idDeclEntityDef* def = gameLocal.FindEntityDef(damageDefName, false);
	if (def == NULL) {
		return;
	}

	jointNum = CLIPMODEL_ID_TO_JOINT_HANDLE(collision.c.id);
	if (jointNum == INVALID_JOINT) {
		return;
	}

	dir = velocity;
	dir.Normalize();

	axis = renderEntity.joints[jointNum].ToMat3() * renderEntity.axis;
	origin = renderEntity.origin + renderEntity.joints[jointNum].ToVec3() * renderEntity.axis;

	localOrigin = (collision.c.point - origin) * axis.Transpose();
	localNormal = collision.c.normal * axis.Transpose();
	localDir = dir * axis.Transpose();

	AddLocalDamageEffect(jointNum, localOrigin, localNormal, localDir, def, collision.c.material);

	if (gameLocal.isServer) {
		idBitMsg	msg;
		byte		msgBuf[MAX_EVENT_PARAM_SIZE];

		msg.Init(msgBuf, sizeof(msgBuf));
		msg.BeginWriting();
		msg.WriteShort((int)jointNum);
		msg.WriteFloat(localOrigin[0]);
		msg.WriteFloat(localOrigin[1]);
		msg.WriteFloat(localOrigin[2]);
		msg.WriteDir(localNormal, 24);
		msg.WriteDir(localDir, 24);
		msg.WriteLong(gameLocal.ServerRemapDecl(-1, DECL_ENTITYDEF, def->Index()));
		msg.WriteLong(gameLocal.ServerRemapDecl(-1, DECL_MATERIAL, collision.c.material->Index()));
		ServerSendEvent(EVENT_ADD_DAMAGE_EFFECT, &msg, false, -1);
	}
}

/*
==============
idAnimatedEntity::GetDefaultSurfaceType
==============
*/
int	idAnimatedEntity::GetDefaultSurfaceType(void) const {
	return SURFTYPE_METAL;
}

/*
==============
idAnimatedEntity::AddLocalDamageEffect
==============
*/
void idAnimatedEntity::AddLocalDamageEffect(jointHandle_t jointNum, const idVec3& localOrigin, const idVec3& localNormal, const idVec3& localDir, const idDeclEntityDef* def, const idMaterial* collisionMaterial) {
	const char* sound, * splat, * decal, * bleed, * key;
	damageEffect_t* de;
	idVec3 origin, dir;
	idMat3 axis;

	axis = renderEntity.joints[jointNum].ToMat3() * renderEntity.axis;
	origin = renderEntity.origin + renderEntity.joints[jointNum].ToVec3() * renderEntity.axis;

	origin = origin + localOrigin * axis;
	dir = localDir * axis;

	int type = collisionMaterial->GetSurfaceType();
	if (type == SURFTYPE_NONE) {
		type = GetDefaultSurfaceType();
	}

	const char* materialType = gameLocal.sufaceTypeNames[type];

	// start impact sound based on material type
	key = va("snd_%s", materialType);
	sound = spawnArgs.GetString(key);
	if (*sound == '\0') {
		sound = def->dict.GetString(key);
	}
	if (*sound != '\0') {
		StartSoundShader(declManager->FindSound(sound), SND_CHANNEL_BODY, 0, false, NULL);
	}

	// blood splats are thrown onto nearby surfaces
	key = va("mtr_splat_%s", materialType);
	splat = spawnArgs.RandomPrefix(key, gameLocal.random);
	if (*splat == '\0') {
		splat = def->dict.RandomPrefix(key, gameLocal.random);
	}
	if (*splat != '\0') {
		gameLocal.BloodSplat(origin, dir, 64.0f, splat);
	}

	// can't see wounds on the player model in single player mode
	if (!(IsType(idPlayer::GetClassType()) && !gameLocal.isMultiplayer)) {
		// place a wound overlay on the model
		key = va("mtr_wound_%s", materialType);
		decal = spawnArgs.RandomPrefix(key, gameLocal.random);
		if (*decal == '\0') {
			decal = def->dict.RandomPrefix(key, gameLocal.random);
		}
		if (*decal != '\0') {
			ProjectOverlay(origin, dir, 20.0f, decal);
		}
	}

	// a blood spurting wound is added
	key = va("smoke_wound_%s", materialType);
	bleed = spawnArgs.GetString(key);
	if (*bleed == '\0') {
		bleed = def->dict.GetString(key);
	}
	if (*bleed != '\0') {
		de = new damageEffect_t;
		de->next = this->damageEffects;
		this->damageEffects = de;

		de->jointNum = jointNum;
		de->localOrigin = localOrigin;
		de->localNormal = localNormal;
		de->type = static_cast<const idDeclParticle*>(declManager->FindType(DECL_PARTICLE, bleed));
		de->time = gameLocal.time;
	}
}

/*
==============
idAnimatedEntity::UpdateDamageEffects
==============
*/
void idAnimatedEntity::UpdateDamageEffects(void) {
	damageEffect_t* de, ** prev;

	// free any that have timed out
	prev = &this->damageEffects;
	while (*prev) {
		de = *prev;
		if (de->time == 0) {	// FIXME:SMOKE
			*prev = de->next;
			delete de;
		}
		else {
			prev = &de->next;
		}
	}

	if (!g_bloodEffects.GetBool()) {
		return;
	}

	// emit a particle for each bleeding wound
	for (de = this->damageEffects; de; de = de->next) {
		idVec3 origin, start;
		idMat3 axis;

		animator.GetJointTransform(de->jointNum, gameLocal.time, origin, axis);
		axis *= renderEntity.axis;
		origin = renderEntity.origin + origin * renderEntity.axis;
		start = origin + de->localOrigin * axis;
		if (!gameLocal.smokeParticles->EmitSmoke(de->type, de->time, gameLocal.random.CRandomFloat(), start, axis)) {
			de->time = 0;
		}
	}
}

/*
================
idAnimatedEntity::ClientReceiveEvent
================
*/
bool idAnimatedEntity::ClientReceiveEvent(int event, int time, const idBitMsg& msg) {
	int damageDefIndex;
	int materialIndex;
	jointHandle_t jointNum;
	idVec3 localOrigin, localNormal, localDir;

	switch (event) {
	case EVENT_ADD_DAMAGE_EFFECT: {
		jointNum = (jointHandle_t)msg.ReadShort();
		localOrigin[0] = msg.ReadFloat();
		localOrigin[1] = msg.ReadFloat();
		localOrigin[2] = msg.ReadFloat();
		localNormal = msg.ReadDir(24);
		localDir = msg.ReadDir(24);
		damageDefIndex = gameLocal.ClientRemapDecl(DECL_ENTITYDEF, msg.ReadLong());
		materialIndex = gameLocal.ClientRemapDecl(DECL_MATERIAL, msg.ReadLong());
		const idDeclEntityDef* damageDef = static_cast<const idDeclEntityDef*>(declManager->DeclByIndex(DECL_ENTITYDEF, damageDefIndex));
		const idMaterial* collisionMaterial = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, materialIndex));
		AddLocalDamageEffect(jointNum, localOrigin, localNormal, localDir, damageDef, collisionMaterial);
		return true;
	}
	default: {
		return idEntity::ClientReceiveEvent(event, time, msg);
	}
	}
	return false;
}

/*
================
idAnimatedEntity::Event_GetJointHandle

looks up the number of the specified joint.  returns INVALID_JOINT if the joint is not found.
================
*/
void idAnimatedEntity::Event_GetJointHandle(const char* jointname) {
	jointHandle_t joint;

	joint = animator.GetJointHandle(jointname);
	idThread::ReturnInt(joint);
}

/*
================
idAnimatedEntity::Event_ClearAllJoints

removes any custom transforms on all joints
================
*/
void idAnimatedEntity::Event_ClearAllJoints(void) {
	animator.ClearAllJoints();
}

/*
================
idAnimatedEntity::Event_ClearJoint

removes any custom transforms on the specified joint
================
*/
void idAnimatedEntity::Event_ClearJoint(jointHandle_t jointnum) {
	animator.ClearJoint(jointnum);
}

/*
================
idAnimatedEntity::Event_SetJointPos

modifies the position of the joint based on the transform type
================
*/
void idAnimatedEntity::Event_SetJointPos(jointHandle_t jointnum, jointModTransform_t transform_type, const idVec3& pos) {
	animator.SetJointPos(jointnum, transform_type, pos);
}

/*
================
idAnimatedEntity::Event_SetJointAngle

modifies the orientation of the joint based on the transform type
================
*/
void idAnimatedEntity::Event_SetJointAngle(jointHandle_t jointnum, jointModTransform_t transform_type, const idAngles& angles) {
	idMat3 mat;

	mat = angles.ToMat3();
	animator.SetJointAxis(jointnum, transform_type, mat);
}

/*
================
idAnimatedEntity::Event_GetJointPos

returns the position of the joint in worldspace
================
*/
void idAnimatedEntity::Event_GetJointPos(jointHandle_t jointnum) {
	idVec3 offset;
	idMat3 axis;

	if (!GetJointWorldTransform(jointnum, gameLocal.time, offset, axis)) {
		gameLocal.Warning("Joint # %d out of range on entity '%s'", jointnum, name.c_str());
	}

	idThread::ReturnVector(offset);
}

/*
================
idAnimatedEntity::Event_GetJointAngle

returns the orientation of the joint in worldspace
================
*/
void idAnimatedEntity::Event_GetJointAngle(jointHandle_t jointnum) {
	idVec3 offset;
	idMat3 axis;

	if (!GetJointWorldTransform(jointnum, gameLocal.time, offset, axis)) {
		gameLocal.Warning("Joint # %d out of range on entity '%s'", jointnum, name.c_str());
	}

	idAngles ang = axis.ToAngles();
	idVec3 vec(ang[0], ang[1], ang[2]);
	idThread::ReturnVector(vec);
}

