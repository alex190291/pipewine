#ifndef DRIVER_CLSID_H
#define DRIVER_CLSID_H

#include <wine/windows/guiddef.h>

/* {A4262EE4-C528-4FF9-87BE-56261AD792C3} */
#define CLSID_PipeWine_STRING(x) x##"{A4262EE4-C528-4FF9-87BE-56261AD792C3}"
static GUID const CLSID_PipeWine = {
    0xA4262EE4, 0xC528, 0x4FF9,
    {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}
};

#endif // DRIVER_CLSID_H
