//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/modules/managed_queue/test_managed_queue_placeholder.cpp
//
//  NOTE: ManagedQueue depends on MonitorInstance (singleton).
//        These tests require a test fixture that initializes monitoring.
//        See the note below for how to set up the test environment.
//
//==============================================================================
#include <gtest/gtest.h>

TEST(ManagedQueuePlaceholder, TodoImplementTests)
{
	GTEST_SKIP() << "ManagedQueue tests require monitoring initialization - see tests/modules/managed_queue/";
}
