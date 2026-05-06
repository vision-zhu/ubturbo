"""测试基类"""

import time
from typing import Dict, Any
from dataclasses import dataclass
import sys
import os

# 添加父目录到路径
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.logger import Logger


@dataclass
class TestResult:
    """测试结果"""
    name: str
    passed: bool
    message: str
    duration: float = 0.0
    details: Dict[str, Any] = None


class BaseTest:
    """测试基类"""

    def __init__(self, config: Dict, logger: Logger, ko_manager, service_manager,
                 smap_client, process_helper):
        self.config = config
        self.logger = logger
        self.ko_manager = ko_manager
        self.service_manager = service_manager
        self.smap_client = smap_client
        self.process_helper = process_helper
        self.test_processes = []  # 当前测试创建的进程

    @property
    def name(self) -> str:
        """测试名称"""
        return self.__class__.__name__

    @property
    def description(self) -> str:
        """测试描述"""
        return ""

    @property
    def mode(self) -> str:
        """测试模式: process 或 vm"""
        return "process"

    def setup(self) -> bool:
        """环境准备"""
        return True

    def run(self) -> bool:
        """执行测试"""
        return True

    def verify(self) -> bool:
        """验证结果"""
        return True

    def teardown(self) -> bool:
        """环境清理"""
        # 清理测试进程
        for pid in self.test_processes:
            self.process_helper.kill_process(pid)
        self.test_processes = []
        return True

    def execute(self) -> TestResult:
        """完整执行测试"""
        self.logger.test_start(self.name)
        start_time = time.time()

        try:
            # setup
            if not self.setup():
                self.logger.error(f"{self.name} setup failed")
                return TestResult(self.name, False, "Setup failed",
                                  time.time() - start_time)

            # run
            if not self.run():
                self.logger.error(f"{self.name} run failed")
                self.teardown()
                return TestResult(self.name, False, "Run failed",
                                  time.time() - start_time)

            # verify
            if not self.verify():
                self.logger.error(f"{self.name} verify failed")
                self.teardown()
                return TestResult(self.name, False, "Verify failed",
                                  time.time() - start_time)

            # teardown
            self.teardown()

            duration = time.time() - start_time
            self.logger.test_end(self.name, "PASSED")
            return TestResult(self.name, True, "Success", duration)

        except Exception as e:
            self.logger.error(f"{self.name} exception: {e}")
            self.teardown()
            duration = time.time() - start_time
            self.logger.test_end(self.name, "FAILED")
            return TestResult(self.name, False, str(e), duration)

    def wait(self, seconds: int = None):
        """等待指定时间"""
        wait_time = seconds or self.config.get("test", {}).get("wait_time_sec", 5)
        self.logger.debug(f"Waiting {wait_time} seconds...")
        time.sleep(wait_time)

    def create_test_process(self, memory_mb: int, numa_node: int = None) -> int:
        """创建测试进程"""
        pid = self.process_helper.create_memory_process(memory_mb, numa_node)
        if pid > 0:
            self.test_processes.append(pid)
        return pid