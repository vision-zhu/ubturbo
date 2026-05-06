"""服务管理模块"""

import time
from typing import Optional

from .ssh_executor import SSHExecutor


class ServiceManager:
    """管理ubturbo服务"""

    def __init__(self, executor: SSHExecutor, service_name: str = "ubturbo"):
        self.executor = executor
        self.service_name = service_name

    def start_service(self) -> bool:
        """启动服务"""
        print(f"Starting {self.service_name} service...")
        if not self.executor.systemctl("start", self.service_name):
            print(f"Failed to start {self.service_name}")
            return False
        return self.wait_service_ready(timeout=30)

    def stop_service(self) -> bool:
        """停止服务"""
        print(f"Stopping {self.service_name} service...")
        return self.executor.systemctl("stop", self.service_name)

    def restart_service(self) -> bool:
        """重启服务"""
        print(f"Restarting {self.service_name} service...")
        if not self.executor.systemctl("restart", self.service_name):
            return False
        return self.wait_service_ready(timeout=30)

    def check_service_status(self) -> str:
        """检查服务状态"""
        cmd = f"systemctl is-active {self.service_name}"
        code, out, err = self.executor.execute(cmd)
        return out.strip() if code == 0 else "inactive"

    def is_service_running(self) -> bool:
        """检查服务是否运行"""
        return self.check_service_status() == "active"

    def wait_service_ready(self, timeout: int = 30) -> bool:
        """等待服务就绪"""
        print(f"Waiting for {self.service_name} to be ready (timeout={timeout}s)...")
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.is_service_running():
                print(f"{self.service_name} is running")
                return True
            time.sleep(1)
        print(f"Timeout waiting for {self.service_name}")
        return False

    def get_service_info(self) -> dict:
        """获取服务详细信息"""
        cmd = f"systemctl status {self.service_name}"
        code, out, err = self.executor.execute(cmd)
        return {
            "status": self.check_service_status(),
            "output": out,
            "error": err
        }