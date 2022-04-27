#include <inttypes.h>
#include "utils.h"

int isLittleEndianSystem()
{
    volatile uint32_t i = 0x01234567;
    return (*((uint8_t*)(&i))) == 0x67;
}
