#ifndef MOBILE_H
#define MOBILE_H

#include "socket.h"
#include "logger.h"
#include "files.h"
#include "framerate.h"
#include "libmobile/mobile.h"

void MobileInit(void);
void MobileDeinit(void);
void MobileLoop(unsigned);
uint8_t MobileTransfer(uint8_t);

#endif
