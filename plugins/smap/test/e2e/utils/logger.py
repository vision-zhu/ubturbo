"""日志模块"""

import os
import time
from datetime import datetime


class Logger:
    """测试日志记录器"""

    def __init__(self, log_path: str = "/tmp/smap_test.log", verbose: bool = True):
        self.log_path = log_path
        self.verbose = verbose
        self._file = None

    def open(self):
        """打开日志文件"""
        os.makedirs(os.path.dirname(self.log_path), exist_ok=True)
        self._file = open(self.log_path, 'a', encoding='utf-8')

    def close(self):
        """关闭日志文件"""
        if self._file:
            self._file.close()
            self._file = None

    def _write(self, level: str, msg: str):
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        line = f"[{timestamp}] [{level}] {msg}\n"
        if self._file:
            self._file.write(line)
            self._file.flush()
        if self.verbose:
            print(line.strip())

    def info(self, msg: str):
        self._write("INFO", msg)

    def error(self, msg: str):
        self._write("ERROR", msg)

    def warning(self, msg: str):
        self._write("WARN", msg)

    def debug(self, msg: str):
        if self.verbose:
            self._write("DEBUG", msg)

    def test_start(self, test_name: str):
        self.info(f"========== Test {test_name} START ========== ")

    def test_end(self, test_name: str, result: str):
        self.info(f"========== Test {test_name} END: {result} ========== ")


# 全局日志实例
_global_logger = None


def get_logger(log_path: str = None, verbose: bool = True) -> Logger:
    """获取全局日志实例"""
    global _global_logger
    if _global_logger is None:
        _global_logger = Logger(log_path or "/tmp/smap_test.log", verbose)
        _global_logger.open()
    return _global_logger