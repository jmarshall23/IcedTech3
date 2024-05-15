// Icegame.h
//

#pragma once

//
// iceGameLocal
//
class iceGameLocal : public idGameLocal {
public:
	virtual void			Init(void) override;
};

extern iceGameLocal gameLocal;