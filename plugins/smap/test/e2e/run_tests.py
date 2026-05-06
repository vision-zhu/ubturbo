#!/usr/bin/env python3
"""
SMAP端到端自动化测试主入口

使用方式:
    python3 run_tests.py --config test_config.json --mode process
    python3 run_tests.py --list
    python3 run_tests.py --filter "MigrateOut"
"""

import argparse
import sys
import os

# 添加路径以便导入模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from config.config_loader import ConfigLoader
from smap_test_framework import SmapTestFramework


def main():
    parser = argparse.ArgumentParser(description="SMAP End-to-End Test Framework")
    parser.add_argument("--config", "-c", default="config/test_config.json",
                        help="配置文件路径 (default: config/test_config.json)")
    parser.add_argument("--mode", "-m", choices=["all", "process", "vm"],
                        default=None, help="测试模式 (default: 从配置读取)")
    parser.add_argument("--filter", "-f", default=None,
                        help="过滤测试用例名称")
    parser.add_argument("--list", "-l", action="store_true",
                        help="列出所有测试用例")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="详细输出")
    parser.add_argument("--init-config", action="store_true",
                        help="生成配置文件模板")

    args = parser.parse_args()

    # 生成配置模板
    if args.init_config:
        config_loader = ConfigLoader()
        config_loader.save_template("config/test_config.json")
        print("Config template generated at config/test_config.json")
        print("Please modify it according to your environment")
        return 0

    # 加载配置
    config_path = args.config
    if not os.path.exists(config_path):
        print(f"Error: Config file not found: {config_path}")
        print("Please create config file first, or use --init-config to generate template")
        return 1

    config = ConfigLoader(config_path)
    if not config.validate():
        print("Error: Invalid config")
        return 1

    # 创建框架
    framework = SmapTestFramework(config)

    # 初始化
    if not framework.initialize():
        print("Error: Failed to initialize framework")
        return 1

    try:
        # 列出测试
        if args.list:
            framework.list_tests()
            return 0

        # 运行测试
        results = framework.run_tests(mode=args.mode, filter_name=args.filter)

        # 打印报告
        success = framework.print_report()

        return 0 if success else 1

    finally:
        framework.cleanup()


if __name__ == "__main__":
    sys.exit(main())