/*
 * Description: smap_user_log ut code
 */
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "smap_log_core.h"
#include "smap_user_log.h"

using namespace std;

static int g_logCallCount = 0;
static int g_lastLogLevel = 0;
static string g_lastLogMsg;
static string g_lastLogModule;

static void MockLogCallback(int level, const char *str, const char *moduleName)
{
    g_logCallCount++;
    g_lastLogLevel = level;
    if (str)
        g_lastLogMsg = str;
    if (moduleName)
        g_lastLogModule = moduleName;
}

class SmapUserLogTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        cout << "[SmapUserLogTest SetUp Begin]" << endl;
        g_logCallCount = 0;
        g_lastLogLevel = 0;
        g_lastLogMsg.clear();
        g_lastLogModule.clear();
        /* Set to TRACE so subscriber-path tests pass regardless of prior state */
        SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_TRACE);
        cout << "[SmapUserLogTest SetUp End]" << endl;
    }
    void TearDown() override
    {
        cout << "[SmapUserLogTest TearDown Begin]" << endl;
        SmapLoggerExit();
        GlobalMockObject::verify();
        cout << "[SmapUserLogTest TearDown End]" << endl;
    }
};

TEST_F(SmapUserLogTest, TestUpstreamSubscribeLogger)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "test message");
    EXPECT_EQ(1, g_logCallCount);
    EXPECT_EQ(1, g_lastLogLevel);
    EXPECT_TRUE(g_lastLogModule.find("SMAP") != string::npos);
}

TEST_F(SmapUserLogTest, TestUpstreamSubscribeLoggerNull)
{
    UpstreamSubscribeLogger(NULL);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "test message");
    EXPECT_EQ(0, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestSmapStartULogNullPath)
{
    int ret = SmapStartULog(NULL);
    EXPECT_EQ(-22, ret);
}

TEST_F(SmapUserLogTest, TestSmapStartULogValidPath)
{
    int ret = SmapStartULog("/tmp/test_smap_ulog");
    EXPECT_EQ(0, ret);
    SmapLoggerExit();
}

TEST_F(SmapUserLogTest, TestSMAP_UlogDebug)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_DEBUG, "testFunc", 10, "test.c", "debug message");
    EXPECT_EQ(1, g_logCallCount);
    EXPECT_EQ(0, g_lastLogLevel);
}

TEST_F(SmapUserLogTest, TestSMAP_UlogWarning)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_WARNING, "testFunc", 10, "test.c", "warning message");
    EXPECT_EQ(1, g_logCallCount);
    EXPECT_EQ(2, g_lastLogLevel);
}

TEST_F(SmapUserLogTest, TestSMAP_UlogError)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_ERROR, "testFunc", 10, "test.c", "error message");
    EXPECT_EQ(1, g_logCallCount);
    EXPECT_EQ(3, g_lastLogLevel);
}

TEST_F(SmapUserLogTest, TestSMAP_UlogInvalidLevel)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(999, "testFunc", 10, "test.c", "invalid level message");
    EXPECT_EQ(0, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestSmapLoggerExitWithSubscriber)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLoggerExit();
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "after exit message");
    EXPECT_EQ(0, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestSMAP_UlogWithFormat)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "format test %d %s", 42, "string");
    EXPECT_EQ(1, g_logCallCount);
    EXPECT_TRUE(g_lastLogMsg.find("42") != string::npos);
    EXPECT_TRUE(g_lastLogMsg.find("string") != string::npos);
}

TEST_F(SmapUserLogTest, TestUpstreamSubscribeLoggerReplace)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "first message");
    EXPECT_EQ(1, g_logCallCount);

    g_logCallCount = 0;
    UpstreamSubscribeLogger(NULL);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "second message");
    EXPECT_EQ(0, g_logCallCount);
}

/* --- Level filtering tests --- */

TEST_F(SmapUserLogTest, TestLevelFilterDebugBlockedAtInfo)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_INFO);
    SMAP_Ulog(SMAP_LOG_DEBUG, "testFunc", 10, "test.c", "should be blocked");
    EXPECT_EQ(0, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestLevelFilterDebugPassAtDebug)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_DEBUG);
    SMAP_Ulog(SMAP_LOG_DEBUG, "testFunc", 10, "test.c", "should pass");
    EXPECT_EQ(1, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestLevelFilterInfoPassAtInfo)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_INFO);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "should pass");
    EXPECT_EQ(1, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestLevelFilterInfoBlockedAtWarn)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_WARN);
    SMAP_Ulog(SMAP_LOG_INFO, "testFunc", 10, "test.c", "should be blocked");
    EXPECT_EQ(0, g_logCallCount);
}

TEST_F(SmapUserLogTest, TestLevelFilterErrorPassAtWarn)
{
    UpstreamSubscribeLogger(MockLogCallback);
    SmapLogCoreSetMinLogLevel(SMAP_LOG_CORE_WARN);
    SMAP_Ulog(SMAP_LOG_ERROR, "testFunc", 10, "test.c", "should pass");
    EXPECT_EQ(1, g_logCallCount);
}

static std::atomic<int> g_concurrentCalls(0);
static void CountingLogCallback(int level, const char *str, const char *moduleName)
{
    (void)level;
    (void)str;
    (void)moduleName;
    g_concurrentCalls++;
}

TEST_F(SmapUserLogTest, TestConcurrentLogAndLoggerExit)
{
    /* SmapLoggerExit 并发置空订阅回调：日志线程不得调用到 NULL */
    std::atomic<bool> stop(false);
    std::vector<std::thread> loggers;
    g_concurrentCalls = 0;
    for (int t = 0; t < 4; t++) {
        loggers.emplace_back([&stop]() {
            while (!stop.load()) {
                SMAP_Ulog(SMAP_LOG_ERROR, "testFunc", 10, "test.c", "concurrent %d", 1);
            }
        });
    }
    for (int i = 0; i < 2000; i++) {
        UpstreamSubscribeLogger(CountingLogCallback);
        SmapLoggerExit();
    }
    stop = true;
    for (auto &l : loggers) {
        l.join();
    }
    SUCCEED();
}
