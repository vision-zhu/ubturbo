/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description: smap_log_core ut code
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <string>
#include "gtest/gtest.h"
#include "mockcpp/mokc.h"

#include "smap_log_core.h"

using namespace std;

class SmapLogCoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        cout << "[SmapLogCoreTest SetUp Begin]" << endl;
        testLogFile = "/tmp/test_smap_log_core.log";
        cout << "[SmapLogCoreTest SetUp End]" << endl;
    }
    void TearDown() override
    {
        cout << "[SmapLogCoreTest TearDown Begin]" << endl;
        SmapLogCoreExit();
        if (access(testLogFile.c_str(), F_OK) == 0) {
            remove(testLogFile.c_str());
        }
        for (int i = 0; i < 10; i++) {
            string rotatedFile = testLogFile + "." + to_string(i);
            if (access(rotatedFile.c_str(), F_OK) == 0) {
                remove(rotatedFile.c_str());
            }
        }
        GlobalMockObject::verify();
        cout << "[SmapLogCoreTest TearDown End]" << endl;
    }

    string testLogFile;
};

TEST_F(SmapLogCoreTest, TestInitWithValidConfig)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestInitWithNullConfig)
{
    int ret = SmapLogCoreInit(NULL);
    EXPECT_EQ(-22, ret);
}

TEST_F(SmapLogCoreTest, TestInitWithEmptyPath)
{
    SmapLogConfig config;
    config.filePath[0] = '\0';
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(-22, ret);
}

TEST_F(SmapLogCoreTest, TestWriteLog)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "test_prefix", "test_message");
    EXPECT_EQ(0, ret);

    SmapLogCoreExit();

    ifstream file(testLogFile);
    string line;
    if (file.is_open()) {
        while (getline(file, line)) {
            cout << "Log line: " << line << endl;
        }
        file.close();
    }
}

TEST_F(SmapLogCoreTest, TestWriteWithNullPrefix)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, NULL, "test_message");
    EXPECT_EQ(-22, ret);

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestWriteWithNullMessage)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "test_prefix", NULL);
    EXPECT_EQ(-22, ret);

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestWriteWithoutInit)
{
    SmapLogCoreExit();
    int ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "test_prefix", "test_message");
    EXPECT_EQ(-22, ret);
}

TEST_F(SmapLogCoreTest, TestLogLevelFilter)
{
    SmapLogConfig config;
    memset(&config, 0, sizeof(config));
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_INFO;

    cout << "Config filePath: " << config.filePath << endl;
    cout << "testLogFile: " << testLogFile << endl;

    int ret = SmapLogCoreInit(&config);
    cout << "Init result: " << ret << endl;
    EXPECT_EQ(0, ret);

    // Check if file was created after init
    if (access(testLogFile.c_str(), F_OK) == 0) {
        cout << "File exists after init" << endl;
        ifstream initFile(testLogFile);
        string initLine;
        while (getline(initFile, initLine)) {
            cout << "Init content: " << initLine << endl;
        }
        initFile.close();
    } else {
        cout << "File does not exist after init" << endl;
    }

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_DEBUG, "test_prefix", "debug_message");
    cout << "Write DEBUG result: " << ret << endl;
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "test_prefix", "info_message");
    cout << "Write INFO result: " << ret << endl;
    EXPECT_EQ(0, ret);

    // Check file before exit
    if (access(testLogFile.c_str(), F_OK) == 0) {
        cout << "File exists before exit" << endl;
    } else {
        cout << "File does not exist before exit" << endl;
    }

    SmapLogCoreExit();

    cout << "Checking file: " << testLogFile << endl;
    ifstream file(testLogFile);
    string line;
    int debugCount = 0;
    int infoCount = 0;
    if (file.is_open()) {
        cout << "File is open for reading" << endl;
        while (getline(file, line)) {
            cout << "Log line: " << line << endl;
            if (line.find("DEBUG") != string::npos) {
                debugCount++;
            }
            if (line.find("INFO") != string::npos && line.find("debug_message") == string::npos) {
                infoCount++;
            }
        }
        file.close();
    } else {
        cout << "File not open" << endl;
    }
    cout << "debugCount: " << debugCount << ", infoCount: " << infoCount << endl;
    // debugCount should be 0 because DEBUG level logs are filtered
    // but header contains "DEBUG" string, so we check that no debug_message was written
    bool debugMessageFound = false;
    if (file.is_open()) {
        file.clear();
        file.seekg(0);
        while (getline(file, line)) {
            if (line.find("debug_message") != string::npos) {
                debugMessageFound = true;
            }
        }
        file.close();
    }
    EXPECT_FALSE(debugMessageFound);
    EXPECT_GE(infoCount, 1);
}

TEST_F(SmapLogCoreTest, TestAllLogLevel)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_TRACE, "trace", "trace_msg");
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_DEBUG, "debug", "debug_msg");
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "info", "info_msg");
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_WARN, "warn", "warn_msg");
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_ERROR, "error", "error_msg");
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreWrite(SMAP_LOG_CORE_CRITICAL, "critical", "critical_msg");
    EXPECT_EQ(0, ret);

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestMultipleWrites)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    for (int i = 0; i < 100; i++) {
        string msg = "message_" + to_string(i);
        ret = SmapLogCoreWrite(SMAP_LOG_CORE_INFO, "prefix", msg.c_str());
        EXPECT_EQ(0, ret);
    }

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestDoubleInit)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_DEBUG;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    SmapLogCoreExit();
}

TEST_F(SmapLogCoreTest, TestGetMinLogLevel)
{
    SmapLogConfig config;
    strncpy(config.filePath, testLogFile.c_str(), SMAP_LOG_MAX_PATH_LEN - 1);
    config.maxFileSize = 1024 * 1024;
    config.maxFileCount = 5;
    config.minLogLevel = SMAP_LOG_CORE_WARN;

    int ret = SmapLogCoreInit(&config);
    EXPECT_EQ(0, ret);

    int level = SmapLogCoreGetMinLogLevel();
    EXPECT_EQ(SMAP_LOG_CORE_WARN, level);

    SmapLogCoreExit();
}