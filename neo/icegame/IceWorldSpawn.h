// IceWorldSpawn.h
//

#pragma once

class iceWorldspawn : public idWorldspawn {
public:
	CLASS_PROTOTYPE(iceWorldspawn);

	~iceWorldspawn();

	virtual void			Spawn(void);

private:	
	const iceDeclAtmosphere* defaultAtmosphere;
};