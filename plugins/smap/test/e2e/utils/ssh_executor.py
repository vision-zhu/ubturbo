"""SSH远程执行模块，支持本地和远程两种模式"""

import subprocess
import os
from typing import Tuple, Optional


class SSHExecutor:
    """SSH远程命令执行，支持切换不同环境"""

    def __init__(self, config: dict):
        self.host = config.get("host", "127.0.0.1")
        self.port = config.get("port", 22)
        self.username = config.get("username", "root")
        self.password = config.get("password", "")
        self.ssh_key_path = config.get("ssh_key_path", "")
        self._ssh_client = None

    def is_local(self) -> bool:
        """检查是否本地执行"""
        return self.host == "127.0.0.1" or self.host == "localhost"

    def connect(self) -> bool:
        """建立SSH连接（远程模式）"""
        if self.is_local():
            return True
        # 使用paramiko建立连接（如果需要）
        try:
            import paramiko
            self._ssh_client = paramiko.SSHClient()
            self._ssh_client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            if self.ssh_key_path:
                self._ssh_client.connect(self.host, self.port, self.username, key_filename=self.ssh_key_path)
            else:
                self._ssh_client.connect(self.host, self.port, self.username, self.password)
            return True
        except ImportError:
            print("Warning: paramiko not installed, using subprocess for SSH")
            return True
        except Exception as e:
            print(f"SSH connect failed: {e}")
            return False

    def disconnect(self) -> bool:
        """断开SSH连接"""
        if self._ssh_client:
            self._ssh_client.close()
            self._ssh_client = None
        return True

    def execute(self, cmd: str, sudo: bool = False, timeout: int = 60) -> Tuple[int, str, str]:
        """执行命令，返回(exit_code, stdout, stderr)"""
        if sudo and not self.is_local():
            # 远程sudo需要特殊处理
            cmd = f"sudo {cmd}"
        elif sudo:
            cmd = f"sudo {cmd}"

        if self.is_local():
            # 本地执行
            try:
                result = subprocess.run(
                    cmd, shell=True, capture_output=True, text=True, timeout=timeout
                )
                return (result.returncode, result.stdout, result.stderr)
            except subprocess.TimeoutExpired:
                return (-1, "", "Command timeout")
            except Exception as e:
                return (-1, "", str(e))
        else:
            # 远程执行
            if self._ssh_client:
                try:
                    stdin, stdout, stderr = self._ssh_client.exec_command(cmd, timeout=timeout)
                    return (0, stdout.read().decode(), stderr.read().decode())
                except Exception as e:
                    return (-1, "", str(e))
            else:
                # 使用ssh命令
                ssh_cmd = f"ssh -p {self.port} {self.username}@{self.host} '{cmd}'"
                try:
                    result = subprocess.run(
                        ssh_cmd, shell=True, capture_output=True, text=True, timeout=timeout
                    )
                    return (result.returncode, result.stdout, result.stderr)
                except Exception as e:
                    return (-1, "", str(e))

    def upload(self, local_path: str, remote_path: str) -> bool:
        """上传文件到远程"""
        if self.is_local():
            # 本地复制
            try:
                import shutil
                shutil.copy(local_path, remote_path)
                return True
            except Exception as e:
                print(f"Copy failed: {e}")
                return False
        else:
            # 使用scp
            scp_cmd = f"scp -P {self.port} {local_path} {self.username}@{self.host}:{remote_path}"
            result = subprocess.run(scp_cmd, shell=True, capture_output=True)
            return result.returncode == 0

    def download(self, remote_path: str, local_path: str) -> bool:
        """从远程下载文件"""
        if self.is_local():
            try:
                import shutil
                shutil.copy(remote_path, local_path)
                return True
            except Exception as e:
                print(f"Copy failed: {e}")
                return False
        else:
            scp_cmd = f"scp -P {self.port} {self.username}@{self.host}:{remote_path} {local_path}"
            result = subprocess.run(scp_cmd, shell=True, capture_output=True)
            return result.returncode == 0

    # ========== 便捷方法 ==========

    def insmod(self, ko_path: str, params: dict = None) -> bool:
        """加载内核模块"""
        param_str = ""
        if params:
            param_str = " " + " ".join([f"{k}={v}" for k, v in params.items() if v is not None])
        cmd = f"insmod {ko_path}{param_str}"
        code, out, err = self.execute(cmd, sudo=True)
        if code != 0:
            print(f"insmod failed: {err}")
        return code == 0

    def rmmod(self, ko_name: str) -> bool:
        """卸载内核模块"""
        cmd = f"rmmod {ko_name}"
        code, out, err = self.execute(cmd, sudo=True)
        if code != 0:
            print(f"rmmod {ko_name} failed: {err}")
        return code == 0

    def lsmod_check(self, ko_name: str) -> bool:
        """检查内核模块是否已加载"""
        cmd = f"lsmod | grep {ko_name}"
        code, out, err = self.execute(cmd)
        return code == 0 and len(out.strip()) > 0

    def systemctl(self, action: str, service: str) -> bool:
        """控制服务"""
        cmd = f"systemctl {action} {service}"
        code, out, err = self.execute(cmd, sudo=True)
        return code == 0

    def get_numa_nodes(self) -> list:
        """获取系统NUMA节点列表"""
        cmd = "ls /sys/devices/system/node/ | grep node"
        code, out, err = self.execute(cmd)
        if code != 0:
            return []
        nodes = []
        for line in out.strip().split('\n'):
            if line.startswith('node'):
                try:
                    nid = int(line.replace('node', ''))
                    nodes.append(nid)
                except:
                    pass
        return sorted(nodes)

    def get_process_numa_memory(self, pid: int) -> dict:
        """获取进程在各NUMA节点的内存分布"""
        cmd = f"numastat -p {pid}"
        code, out, err = self.execute(cmd)
        if code != 0:
            return {}
        # 解析numastat输出
        result = {}
        lines = out.strip().split('\n')
        for line in lines:
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith('Node'):
                try:
                    nid = int(parts[0].replace('Node', ''))
                    # 取最后一列的内存值
                    mem_val = parts[-1]
                    # 转换为MB
                    if 'M' in mem_val:
                        result[nid] = float(mem_val.replace('M', '').strip())
                    elif 'G' in mem_val:
                        result[nid] = float(mem_val.replace('G', '').strip()) * 1024
                    else:
                        result[nid] = float(mem_val) / 1024  # KB to MB
                except:
                    pass
        return result

    def kill_process(self, pid: int) -> bool:
        """终止进程"""
        cmd = f"kill -9 {pid}"
        code, out, err = self.execute(cmd, sudo=True)
        return code == 0

    def process_exists(self, pid: int) -> bool:
        """检查进程是否存在"""
        cmd = f"ps -p {pid}"
        code, out, err = self.execute(cmd)
        return code == 0