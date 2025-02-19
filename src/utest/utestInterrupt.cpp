//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  TSUnit test suite for tsUserInterrupt.h
//
//  Since the purpose of this test is to interrupt the application, we don't do
//  it blindly! The interrupt is effective only if the environment variable
//  UTEST_INTERRUPT_ALLOWED is defined.
//
//----------------------------------------------------------------------------

#include "tsUserInterrupt.h"
#include "tsEnvironment.h"
#include "tsSysUtils.h"
#include "tsunit.h"


//----------------------------------------------------------------------------
// The test fixture
//----------------------------------------------------------------------------

class InterruptTest: public tsunit::Test
{
public:
    virtual void beforeTest() override;
    virtual void afterTest() override;

    void testInterrupt();

    TSUNIT_TEST_BEGIN(InterruptTest);
    TSUNIT_TEST(testInterrupt);
    TSUNIT_TEST_END();
};

TSUNIT_REGISTER(InterruptTest);


//----------------------------------------------------------------------------
// Initialization.
//----------------------------------------------------------------------------

// Test suite initialization method.
void InterruptTest::beforeTest()
{
}

// Test suite cleanup method.
void InterruptTest::afterTest()
{
}


//----------------------------------------------------------------------------
// Unitary tests.
//----------------------------------------------------------------------------

namespace {
    class TestHandler: public ts::InterruptHandler
    {
    public:
        virtual void handleInterrupt() override
        {
            std::cout << "* Got user-interrupt, next time should kill the process" << std::endl;
        }
    };
}

void InterruptTest::testInterrupt()
{
    if (ts::EnvironmentExists(u"UTEST_INTERRUPT_ALLOWED")) {
        std::cerr << "InterruptTest: Unset UTEST_INTERRUPT_ALLOWED to skip the interrupt test" << std::endl;

        TestHandler handler;
        ts::UserInterrupt ui(&handler, true, true);

        TSUNIT_ASSERT(ui.isActive());
        std::cerr << "* Established one-shot handler" << std::endl;
        for (;;) {
            std::cerr << "* Press Ctrl+C..." << std::endl;
            ts::SleepThread(5000);
        }
    }
    else {
        debug() << "InterruptTest: interrupt test skipped, define UTEST_INTERRUPT_ALLOWED to force it" << std::endl;
    }
}
