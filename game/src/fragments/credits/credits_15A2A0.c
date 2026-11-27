#include "credits.h"
#include "StadionPort.h"

u16 Credits_GetRoleCount(void) {
    // PORT: the Spanish staff roll has one more line and the Italian one one fewer (VERSION_ES, VERSION_IT)
    return PORT_VERSION_ES ? 188 : PORT_VERSION_IT ? 186 : 0xBB;
}
