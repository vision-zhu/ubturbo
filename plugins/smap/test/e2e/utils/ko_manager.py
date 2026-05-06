"""内核模块管理"""

import os
from typing import Dict, List, Tuple

from .ssh_executor import SSHExecutor


class KoManager:
    """管理内核模块的加载和卸载"""

    # 模块加载顺序和参数（参考User_Guide.md）
    # (ko_name, 目录, 参数)
    # 目录: "drivers" 或 "tiering"
    KO_MODULES = [
        ("smap_tracking_core.ko", "drivers", {}),
        ("smap_histogram_tracking.ko", "drivers", {}),  # 可选
        ("smap_access_tracking.ko", "drivers", {"smap_scene": 2}),
        ("smap_tiering.ko", "tiering", {"smap_scene": 2, "smap_pgsize": None})  # 动态设置
    ]

    def __init__(self, executor: SSHExecutor, ko_path: str, ko_tiering_path: str = None):
        self.executor = executor
        self.ko_path = ko_path  # drivers目录
        self.ko_tiering_path = ko_tiering_path or os.path.dirname(ko_path) + "/tiering"  # tiering目录
        self.loaded_modules = []

    def _get_ko_file(self, ko_name: str, ko_dir: str) -> str:
        """获取ko文件完整路径"""
        if ko_dir == "tiering":
            return os.path.join(self.ko_tiering_path, ko_name)
        else:
            return os.path.join(self.ko_path, ko_name)

    def load_kos(self, mode: str = "process") -> bool:
        """
        加载所有内核模块
        mode: "process" -> smap_pgsize=0 (4K页)
        mode: "vm" -> smap_pgsize不设置 (默认2M页)
        """
        # 先卸载可能已存在的模块
        self.unload_kos()

        for ko_name, ko_dir, params in self.KO_MODULES:
            ko_file = self._get_ko_file(ko_name, ko_dir)

            # 动态设置smap_pgsize参数
            actual_params = params.copy()
            if "smap_pgsize" in actual_params:
                if mode == "process":
                    actual_params["smap_pgsize"] = 0
                else:
                    # vm模式不传参，使用默认值
                    del actual_params["smap_pgsize"]

            # 检查ko文件是否存在
            if self.executor.is_local():
                if not os.path.exists(ko_file):
                    print(f"Warning: {ko_file} not found, skipping")
                    if ko_name == "smap_histogram_tracking.ko":
                        continue
                    self.unload_kos()
                    return False
            else:
                code, out, err = self.executor.execute(f"test -f {ko_file}")
                if code != 0:
                    print(f"Warning: {ko_file} not found, skipping")
                    if ko_name == "smap_histogram_tracking.ko":
                        continue
                    self.unload_kos()
                    return False

            # 加载模块
            print(f"Loading {ko_name} from {ko_file} with params: {actual_params}")
            if not self.executor.insmod(ko_file, actual_params):
                print(f"Failed to load {ko_name}")
                # smap_histogram_tracking.ko是可选的
                if ko_name == "smap_histogram_tracking.ko":
                    print("Warning: smap_histogram_tracking.ko is optional, continue")
                    continue
                # 其他模块失败需要回滚
                self.unload_kos()
                return False

            self.loaded_modules.append(ko_name)

        print(f"Loaded modules: {self.loaded_modules}")
        return True

    def unload_kos(self) -> bool:
        """卸载所有内核模块（按相反顺序）"""
        modules_to_unload = list(reversed(self.KO_MODULES))

        for ko_name, _, _ in modules_to_unload:
            ko_base = ko_name.replace('.ko', '')
            if self.executor.lsmod_check(ko_base):
                print(f"Unloading {ko_base}")
                if not self.executor.rmmod(ko_base):
                    print(f"Warning: Failed to unload {ko_base}")

        self.loaded_modules = []
        return True

    def check_all_loaded(self) -> bool:
        """检查所有必需模块是否已加载"""
        required = ["smap_tracking_core", "smap_access_tracking", "smap_tiering"]
        for mod in required:
            if not self.executor.lsmod_check(mod):
                return False
        return True

    def get_loaded_kos(self) -> List[str]:
        """获取已加载的模块列表"""
        cmd = "lsmod | grep smap"
        code, out, err = self.executor.execute(cmd)
        if code != 0:
            return []
        return [line.split()[0] for line in out.strip().split('\n') if line]