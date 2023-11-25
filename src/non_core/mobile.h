#ifndef MOBILE_H
#define MOBILE_H

#include "socket.h"
#include "logger.h"
#include "files.h"
#include "framerate.h"
#include "libmobile/mobile.h"

int MobileInit(void);
void MobileDeinit(void);
void MobileLoop(unsigned);
uint8_t MobileTransfer(uint8_t);

int MobileConf(enum mobile_adapter_device*, bool*, struct mobile_addr*, struct mobile_addr*,
	struct mobile_addr*, unsigned*, unsigned char*, bool*);

#endif
