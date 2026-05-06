"""test_cases模块初始化"""

from .base_test import BaseTest, TestResult
from .process_tests import PROCESS_TESTS

__all__ = [
    'BaseTest',
    'TestResult',
    'PROCESS_TESTS',
]