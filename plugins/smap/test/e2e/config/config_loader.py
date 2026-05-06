"""配置加载模块"""

import json
import os
from typing import Dict, List, Optional


class ConfigLoader:
    """加载和管理JSON配置文件"""

    DEFAULT_CONFIG = {
        "environment": {
            "host": "127.0.0.1",
            "port": 22,
            "username": "root",
            "password": "",
            "ssh_key_path": ""
        },
        "smap": {
            "ko_path": "/lib/modules/smap",
            "lib_path": "/opt/ubturbo/lib/libubturbo_client.so",
            "service_name": "ubturbo"
        },
        "numa": {
            "local_numa_ids": [0],
            "remote_numa_config": [],
            "default_remote_nid": 4,
            "default_size_mb": 1024
        },
        "test": {
            "mode": "process",
            "test_process_memory_mb": 256,
            "migrate_ratio": 25,
            "wait_time_sec": 5,
            "verbose": True,
            "log_path": "/tmp/smap_test.log"
        }
    }

    def __init__(self, config_path: str = "test_config.json"):
        self.config_path = config_path
        self.config = self.DEFAULT_CONFIG.copy()
        if os.path.exists(config_path):
            self.load()

    def load(self) -> Dict:
        """加载JSON配置文件"""
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                loaded = json.load(f)
                # 合并配置
                for key in loaded:
                    if key in self.config:
                        if isinstance(self.config[key], dict):
                            self.config[key].update(loaded[key])
                        else:
                            self.config[key] = loaded[key]
                    else:
                        self.config[key] = loaded[key]
            return self.config
        except Exception as e:
            print(f"Warning: Failed to load config from {self.config_path}: {e}")
            print("Using default config")
            return self.config

    def validate(self) -> bool:
        """验证配置完整性"""
        required_keys = ["environment", "smap", "numa", "test"]
        for key in required_keys:
            if key not in self.config:
                print(f"Error: Missing required config key: {key}")
                return False
        return True

    def get_env_config(self) -> Dict:
        """获取SSH连接配置"""
        return self.config.get("environment", self.DEFAULT_CONFIG["environment"])

    def get_smap_config(self) -> Dict:
        """获取SMAP配置"""
        return self.config.get("smap", self.DEFAULT_CONFIG["smap"])

    def get_numa_config(self) -> Dict:
        """获取NUMA配置"""
        return self.config.get("numa", self.DEFAULT_CONFIG["numa"])

    def get_test_config(self) -> Dict:
        """获取测试配置"""
        return self.config.get("test", self.DEFAULT_CONFIG["test"])

    def get_remote_numa_for_local(self, local_nid: int) -> List[Dict]:
        """获取指定本地NUMA对应的远端NUMA配置"""
        numa_config = self.get_numa_config()
        result = []
        for item in numa_config.get("remote_numa_config", []):
            if item.get("local_nid") == local_nid or item.get("local_nid") == -1:
                result.append(item)
        return result

    def is_local(self) -> bool:
        """检查是否本地执行"""
        env = self.get_env_config()
        return env.get("host") == "127.0.0.1" or env.get("host") == "localhost"

    def save_template(self, path: str) -> None:
        """生成配置模板"""
        template = json.dumps(self.DEFAULT_CONFIG, indent=4, ensure_ascii=False)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(template)
        print(f"Config template saved to {path}")