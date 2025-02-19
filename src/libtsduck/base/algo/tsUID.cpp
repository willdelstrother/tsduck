//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  Implementation:
//  To ensure a reasonable level of uniqueness, a UID is composed of:
//    - 24 bits: LSB of process id
//    - 24 bits: LSB of initial UTC time (milliseconds)
//    - 16 bits: sequential index
//  The UID is incremented each time a new value is requested.
//  The index does not wrap, it overflows on time field.
//
//----------------------------------------------------------------------------

#include "tsUID.h"
#include "tsSysUtils.h"
#include "tsTime.h"

// Define singleton instance
TS_DEFINE_SINGLETON (ts::UID);


//----------------------------------------------------------------------------
// Default constructor
//----------------------------------------------------------------------------

ts::UID::UID() :
    _next_uid((uint64_t(CurrentProcessId()) << 40) | ((uint64_t(Time::CurrentUTC() - Time::Epoch) & 0x00FFFFFF) << 16))
{
}


//----------------------------------------------------------------------------
// Generate a new UID
//----------------------------------------------------------------------------

uint64_t ts::UID::newUID()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _next_uid++;
}
