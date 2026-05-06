"""测试进程辅助工具"""

import os
import subprocess
import time
from typing import Optional

from .ssh_executor import SSHExecutor


class ProcessHelper:
    """创建和管理测试进程（基于mem_demo程序）"""

    def __init__(self, executor: SSHExecutor, demo_path: str = None):
        self.executor = executor
        # 默认demo路径
        if demo_path is None:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            self.demo_path = os.path.join(base_dir, "mem_demo")
        else:
            self.demo_path = demo_path
        self.test_processes = []  # 记录创建的测试进程PID

    def compile_demo(self) -> bool:
        """编译mem_demo.c"""
        source_path = os.path.join(os.path.dirname(self.demo_path), "mem_demo.c")
        if self.executor.is_local():
            # 本地编译
            if not os.path.exists(source_path):
                print(f"Error: {source_path} not found")
                return False
            cmd = f"gcc -o {self.demo_path} {source_path}"
            result = subprocess.run(cmd, shell=True, capture_output=True)
            if result.returncode != 0:
                print(f"Compile failed: {result.stderr.decode()}")
                return False
            print(f"Compiled {self.demo_path}")
            return True
        else:
            # 远程编译
            cmd = f"gcc -o {self.demo_path} {source_path}"
            code, out, err = self.executor.execute(cmd)
            if code != 0:
                print(f"Compile failed: {err}")
                return False
            return True

    def create_memory_process(self, size_mb: int, numa_node: Optional[int] = None) -> int:
        """
        创建占用指定内存的测试进程
        返回进程PID
        """
        # 确保demo已编译
        if not self.executor.is_local():
            # 远程检查
            code, out, err = self.executor.execute(f"test -f {self.demo_path}")
            if code != 0:
                print("mem_demo not found, compiling...")
                self.compile_demo()

        cmd_parts = []
        if numa_node is not None:
            cmd_parts.extend(["numactl", "--membind", str(numa_node)])
        cmd_parts.extend([self.demo_path, str(size_mb)])

        cmd = " ".join(cmd_parts)

        if self.executor.is_local():
            # 本地启动进程
            proc = subprocess.Popen(
                cmd, shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            pid = proc.pid
            # 等待进程初始化
            time.sleep(0.5)
            if proc.poll() is not None:
                print(f"Process exited immediately: {proc.stderr.read().decode()}")
                return -1
        else:
            # 远程启动进程（后台运行）
            cmd = f"nohup {cmd} > /dev/null 2>&1 & echo $!"
            code, out, err = self.executor.execute(cmd)
            if code != 0:
                print(f"Failed to start process: {err}")
                return -1
            try:
                pid = int(out.strip().split('\n')[-1])
            except:
                print(f"Failed to parse PID: {out}")
                return -1

        print(f"Created test process PID={pid}, memory={size_mb}MB, numa_node={numa_node}")
        self.test_processes.append(pid)
        return pid

    def kill_process(self, pid: int) -> bool:
        """终止进程"""
        if pid <= 0:
            return False
        print(f"Killing process {pid}")
        return self.executor.kill_process(pid)

    def kill_all_test_processes(self) -> bool:
        """终止所有测试进程"""
        for pid in self.test_processes:
            self.kill_process(pid)
        self.test_processes = []
        return True

    def process_exists(self, pid: int) -> bool:
        """检查进程是否存在"""
        return self.executor.process_exists(pid)

    def get_process_numa_memory(self, pid: int) -> dict:
        """获取进程NUMA内存分布"""
        return self.executor.get_process_numa_memory(pid)

    def get_total_memory_on_numa(self, pid: int, numa_node: int) -> float:
        """获取进程在指定NUMA节点的内存占用(MB)"""
        numa_mem = self.get_process_numa_memory(pid)
        return numa_mem.get(numa_node, 0.0)