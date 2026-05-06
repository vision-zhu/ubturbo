"""SMAP自动化测试框架核心类"""

import sys
import os
import time
from typing import Dict, List, Optional

from config.config_loader import ConfigLoader
from utils.ssh_executor import SSHExecutor
from utils.ko_manager import KoManager
from utils.service_manager import ServiceManager
from utils.smap_client import SmapClient
from utils.process_helper import ProcessHelper
from utils.logger import Logger, get_logger
from test_cases.base_test import BaseTest, TestResult
from test_cases.process_tests import PROCESS_TESTS


class SmapTestFramework:
    """SMAP测试框架核心类"""

    def __init__(self, config: ConfigLoader):
        self.config = config
        self.logger = None
        self.executor = None
        self.ko_manager = None
        self.service_manager = None
        self.smap_client = None
        self.process_helper = None
        self.results: List[TestResult] = []

    def initialize(self) -> bool:
        """初始化框架组件"""
        test_config = self.config.get_test_config()

        # 初始化日志
        self.logger = get_logger(
            log_path=test_config.get("log_path", "/tmp/smap_test.log"),
            verbose=test_config.get("verbose", True)
        )
        self.logger.info("Initializing SMAP test framework...")

        # 初始化SSH执行器
        env_config = self.config.get_env_config()
        self.executor = SSHExecutor(env_config)
        if not self.executor.connect():
            self.logger.error("Failed to connect to target environment")
            return False
        self.logger.info(f"Connected to {env_config.get('host', 'localhost')}")

        # 初始化内核模块管理
        smap_config = self.config.get_smap_config()
        ko_path = smap_config.get("ko_path", "/lib/modules/smap")
        ko_tiering_path = smap_config.get("ko_tiering_path", None)
        self.ko_manager = KoManager(self.executor, ko_path, ko_tiering_path)

        # 初始化服务管理
        self.service_manager = ServiceManager(self.executor, smap_config.get("service_name", "ubturbo"))

        # 初始化smap客户端
        try:
            lib_path = smap_config.get("lib_path", "/opt/ubturbo/lib/libubturbo_client.so")
            smap_lib_path = smap_config.get("smap_lib_path", None)
            self.smap_client = SmapClient(lib_path, smap_lib_path)
        except Exception as e:
            self.logger.warning(f"Failed to load smap client library: {e}")
            self.logger.warning("Some tests may be skipped")
            self.smap_client = None

        # 初始化进程辅助工具
        self.process_helper = ProcessHelper(self.executor)

        # 编译mem_demo
        self.logger.info("Compiling mem_demo...")
        if not self.process_helper.compile_demo():
            self.logger.warning("Failed to compile mem_demo, some tests may fail")

        self.logger.info("Framework initialized successfully")
        return True

    def cleanup(self):
        """清理框架"""
        self.logger.info("Cleaning up framework...")

        # 清理测试进程
        if self.process_helper:
            self.process_helper.kill_all_test_processes()

        # 断开SSH连接
        if self.executor:
            self.executor.disconnect()

        self.logger.close()

    def get_test_classes(self, mode: str = "process", filter_name: str = None) -> List[BaseTest]:
        """获取要运行的测试类"""
        tests = []

        if mode == "process" or mode == "all":
            for test_class in PROCESS_TESTS:
                if filter_name and filter_name.lower() not in test_class.__name__.lower():
                    continue
                tests.append(test_class)

        # vm测试暂时不支持
        # if mode == "vm" or mode == "all":
        #     tests.extend(VM_TESTS)

        return tests

    def run_test(self, test_class: BaseTest) -> TestResult:
        """运行单个测试"""
        test_instance = test_class(
            self.config.config,
            self.logger,
            self.ko_manager,
            self.service_manager,
            self.smap_client,
            self.process_helper
        )
        return test_instance.execute()

    def run_tests(self, mode: str = None, filter_name: str = None) -> List[TestResult]:
        """运行所有测试"""
        test_config = self.config.get_test_config()
        actual_mode = mode or test_config.get("mode", "process")

        tests = self.get_test_classes(actual_mode, filter_name)
        if not tests:
            self.logger.warning("No tests to run")
            return []

        self.logger.info(f"Running {len(tests)} tests in {actual_mode} mode")
        self.results = []

        for test_class in tests:
            self.logger.info(f"--- Running {test_class.__name__} ---")
            result = self.run_test(test_class)
            self.results.append(result)

        return self.results

    def print_report(self):
        """打印测试报告"""
        passed = sum(1 for r in self.results if r.passed)
        failed = len(self.results) - passed

        print("\n" + "=" * 60)
        print("SMAP Test Report")
        print("=" * 60)

        for result in self.results:
            status = "PASS" if result.passed else "FAIL"
            print(f"  {result.name}: {status} ({result.duration:.2f}s)")
            if not result.passed:
                print(f"    Reason: {result.message}")

        print("-" * 60)
        print(f"Total: {len(self.results)}, Passed: {passed}, Failed: {failed}")
        print("=" * 60)

        return failed == 0

    def list_tests(self):
        """列出所有可用测试"""
        tests = self.get_test_classes("all")
        print("\nAvailable tests:")
        for test_class in tests:
            # 创建临时实例获取description
            desc = ""
            try:
                # description是property，需要通过实例访问
                temp = test_class.__new__(test_class)
                desc = temp.description
            except:
                pass
            print(f"  - {test_class.__name__}: {desc}")