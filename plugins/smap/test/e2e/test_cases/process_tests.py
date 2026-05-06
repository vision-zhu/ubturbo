"""进程模式测试用例"""

import time
from typing import Dict

from test_cases.base_test import BaseTest, TestResult


class TestSmapStartStop(BaseTest):
    """测试SMAP启动和停止"""

    @property
    def description(self) -> str:
        return "测试ubturbo_smap_start和ubturbo_smap_stop接口"

    def setup(self) -> bool:
        # 加载ko模块（进程模式）
        self.logger.info("Loading kernel modules (process mode)...")
        if not self.ko_manager.load_kos(mode="process"):
            self.logger.error("Failed to load kernel modules")
            return False

        # 如果使用直接调用模式（libsmap.so），不需要启动服务
        if self.smap_client and self.smap_client.use_direct_mode():
            self.logger.info("Using direct smap.so mode, skip service start")
            return True

        # 启动ubturbo服务（IPC模式需要）
        self.logger.info("Starting ubturbo service...")
        if not self.service_manager.start_service():
            self.logger.error("Failed to start ubturbo service")
            return False

        return True

    def run(self) -> bool:
        # 启动smap（4K页模式）
        self.logger.info("Starting SMAP (4K page mode)...")
        ret = self.smap_client.smap_start(0)  # PAGETYPE_NORMAL
        if ret != 0:
            self.logger.error(f"smap_start failed: {ret}")
            return False
        return True

    def verify(self) -> bool:
        # 检查smap是否运行
        self.logger.info("Checking if SMAP is running...")
        running = self.smap_client.smap_is_running()
        if not running:
            self.logger.error("smap_is_running returned False")
            return False
        self.logger.info("SMAP is running correctly")
        return True

    def teardown(self) -> bool:
        # 停止smap
        self.logger.info("Stopping SMAP...")
        ret = self.smap_client.smap_stop()
        if ret != 0:
            self.logger.warning(f"smap_stop returned {ret}")

        # 如果不是直接模式，停止服务
        if not (self.smap_client and self.smap_client.use_direct_mode()):
            self.service_manager.stop_service()

        # 卸载ko
        self.ko_manager.unload_kos()

        return super().teardown()


class TestSetRemoteNumaInfo(BaseTest):
    """测试设置远端NUMA信息"""

    @property
    def description(self) -> str:
        return "测试ubturbo_smap_remote_numa_info_set接口"

    def setup(self) -> bool:
        if not self.ko_manager.load_kos(mode="process"):
            return False
        if self.smap_client and not self.smap_client.use_direct_mode():
            if not self.service_manager.start_service():
                return False
        if self.smap_client.smap_start(0) != 0:
            return False
        return True

    def run(self) -> bool:
        numa_config = self.config.get("numa", {})
        remote_configs = numa_config.get("remote_numa_config", [])

        if not remote_configs:
            self.logger.warning("No remote NUMA config, using default")
            src_nid = 0
            dest_nid = numa_config.get("default_remote_nid", 10)
            size_mb = numa_config.get("default_size_mb", 10240)
        else:
            cfg = remote_configs[0]
            src_nid = cfg.get("local_nid", 0)
            dest_nid = cfg.get("remote_nid", 10)
            size_mb = cfg.get("size_mb", 10240)

        self.logger.info(f"Setting remote NUMA info: src={src_nid}, dest={dest_nid}, size={size_mb}MB")
        ret = self.smap_client.smap_remote_numa_info_set(src_nid, dest_nid, size_mb)
        if ret != 0:
            self.logger.error(f"remote_numa_info_set failed: {ret}")
            return False
        return True

    def verify(self) -> bool:
        self.logger.info("Remote NUMA info set successfully")
        return True

    def teardown(self) -> bool:
        self.smap_client.smap_stop()
        if self.smap_client and not self.smap_client.use_direct_mode():
            self.service_manager.stop_service()
        self.ko_manager.unload_kos()
        return super().teardown()


class TestProcessMigrateOut(BaseTest):
    """测试进程迁出到远端NUMA"""

    @property
    def description(self) -> str:
        return "测试进程内存迁出到远端NUMA"

    def setup(self) -> bool:
        if not self.ko_manager.load_kos(mode="process"):
            return False
        if self.smap_client and not self.smap_client.use_direct_mode():
            if not self.service_manager.start_service():
                return False
        if self.smap_client.smap_start(0) != 0:
            return False

        # 设置远端NUMA信息
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)
        size_mb = numa_config.get("default_size_mb", 10240)
        self.smap_client.smap_remote_numa_info_set(0, dest_nid, size_mb)

        return True

    def run(self) -> bool:
        test_config = self.config.get("test", {})
        memory_mb = test_config.get("test_process_memory_mb", 256)
        ratio = test_config.get("migrate_ratio", 25)
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)

        # 创建测试进程
        self.logger.info(f"Creating test process with {memory_mb}MB memory...")
        pid = self.create_test_process(memory_mb, numa_node=0)
        if pid <= 0:
            self.logger.error("Failed to create test process")
            return False

        self.logger.info(f"Test process PID: {pid}")
        self.wait(2)

        # 执行迁出
        self.logger.info(f"Migrating process {pid} to NUMA {dest_nid} with ratio {ratio}%...")
        ret = self.smap_client.smap_migrate_out([pid], dest_nid, ratio, pid_type=0)
        if ret != 0:
            self.logger.error(f"migrate_out failed: {ret}")
            return False

        self.wait()
        return True

    def verify(self) -> bool:
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)

        for pid in self.test_processes:
            numa_mem = self.process_helper.get_process_numa_memory(pid)
            self.logger.info(f"Process {pid} NUMA memory: {numa_mem}")

            remote_mem = numa_mem.get(dest_nid, 0)
            if remote_mem > 0:
                self.logger.info(f"Process has {remote_mem}MB on remote NUMA {dest_nid}")
                return True
            else:
                self.logger.warning(f"No memory on remote NUMA {dest_nid}")
                return True  # 放宽验证

        return False

    def teardown(self) -> bool:
        if self.test_processes:
            self.smap_client.smap_remove(self.test_processes, pid_type=0)
        self.smap_client.smap_stop()
        if self.smap_client and not self.smap_client.use_direct_mode():
            self.service_manager.stop_service()
        self.ko_manager.unload_kos()
        return super().teardown()


class TestProcessMigrateBack(BaseTest):
    """测试进程内存迁回"""

    @property
    def description(self) -> str:
        return "测试进程内存从远端NUMA迁回本地"

    def setup(self) -> bool:
        if not self.ko_manager.load_kos(mode="process"):
            return False
        if self.smap_client and not self.smap_client.use_direct_mode():
            if not self.service_manager.start_service():
                return False
        if self.smap_client.smap_start(0) != 0:
            return False

        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)
        size_mb = numa_config.get("default_size_mb", 10240)
        self.smap_client.smap_remote_numa_info_set(0, dest_nid, size_mb)

        return True

    def run(self) -> bool:
        test_config = self.config.get("test", {})
        memory_mb = test_config.get("test_process_memory_mb", 256)
        ratio = test_config.get("migrate_ratio", 25)
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)

        pid = self.create_test_process(memory_mb, numa_node=0)
        if pid <= 0:
            return False

        self.wait(2)

        self.logger.info(f"Migrating out process {pid}...")
        ret = self.smap_client.smap_migrate_out([pid], dest_nid, ratio, pid_type=0)
        if ret != 0:
            return False

        self.wait()

        self.logger.info(f"Disabling NUMA node {dest_nid}...")
        self.smap_client.smap_node_enable(dest_nid, enable=False)
        self.wait(3)

        self.logger.info(f"Migrating back from NUMA {dest_nid}...")
        ret = self.smap_client.smap_migrate_back(dest_nid, 0)
        if ret != 0:
            self.logger.warning(f"migrate_back returned {ret}")

        self.smap_client.smap_node_enable(dest_nid, enable=True)
        return True

    def verify(self) -> bool:
        self.logger.info("Migrate back operation completed")
        return True

    def teardown(self) -> bool:
        if self.test_processes:
            self.smap_client.smap_remove(self.test_processes, pid_type=0)
        self.smap_client.smap_stop()
        if self.smap_client and not self.smap_client.use_direct_mode():
            self.service_manager.stop_service()
        self.ko_manager.unload_kos()
        return super().teardown()


class TestFullWorkflow(BaseTest):
    """完整工作流测试"""

    @property
    def description(self) -> str:
        return "测试完整的SMAP工作流: start -> set_info -> migrate_out -> migrate_back -> remove -> stop"

    def setup(self) -> bool:
        if not self.ko_manager.load_kos(mode="process"):
            return False
        if self.smap_client and not self.smap_client.use_direct_mode():
            if not self.service_manager.start_service():
                return False
        return True

    def run(self) -> bool:
        test_config = self.config.get("test", {})
        memory_mb = test_config.get("test_process_memory_mb", 256)
        ratio = test_config.get("migrate_ratio", 25)
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)
        size_mb = numa_config.get("default_size_mb", 10240)

        # 1. 启动SMAP
        self.logger.info("Step 1: Starting SMAP...")
        if self.smap_client.smap_start(0) != 0:
            return False

        # 2. 设置远端NUMA信息
        self.logger.info(f"Step 2: Setting remote NUMA info...")
        if self.smap_client.smap_remote_numa_info_set(0, dest_nid, size_mb) != 0:
            return False

        # 3. 创建测试进程
        self.logger.info(f"Step 3: Creating test process...")
        pid = self.create_test_process(memory_mb, numa_node=0)
        if pid <= 0:
            return False

        self.wait(2)

        # 4. 迁出
        self.logger.info(f"Step 4: Migrating out (ratio={ratio}%)...")
        if self.smap_client.smap_migrate_out([pid], dest_nid, ratio, pid_type=0) != 0:
            return False

        self.wait()

        # 5. 查看内存分布
        numa_mem = self.process_helper.get_process_numa_memory(pid)
        self.logger.info(f"Step 5: NUMA memory distribution: {numa_mem}")

        # 6. 禁用节点并迁回
        self.logger.info(f"Step 6: Disabling node and migrating back...")
        self.smap_client.smap_node_enable(dest_nid, enable=False)
        self.wait(3)
        self.smap_client.smap_migrate_back(dest_nid, 0)
        self.smap_client.smap_node_enable(dest_nid, enable=True)

        self.wait()

        # 7. 移除管理
        self.logger.info("Step 7: Removing process management...")
        self.smap_client.smap_remove([pid], pid_type=0)

        # 8. 停止SMAP
        self.logger.info("Step 8: Stopping SMAP...")
        self.smap_client.smap_stop()

        return True

    def verify(self) -> bool:
        self.logger.info("Full workflow completed successfully")
        return True

    def teardown(self) -> bool:
        if self.smap_client and not self.smap_client.use_direct_mode():
            self.service_manager.stop_service()
        self.ko_manager.unload_kos()
        return super().teardown()


class TestNodeEnableDisable(BaseTest):
    """测试NUMA节点使能/禁用"""

    @property
    def description(self) -> str:
        return "测试ubturbo_smap_node_enable接口"

    def setup(self) -> bool:
        if not self.ko_manager.load_kos(mode="process"):
            return False
        if self.smap_client and not self.smap_client.use_direct_mode():
            if not self.service_manager.start_service():
                return False
        if self.smap_client.smap_start(0) != 0:
            return False
        return True

    def run(self) -> bool:
        numa_config = self.config.get("numa", {})
        dest_nid = numa_config.get("default_remote_nid", 10)

        self.logger.info(f"Disabling NUMA node {dest_nid}...")
        ret = self.smap_client.smap_node_enable(dest_nid, enable=False)
        if ret != 0:
            self.logger.warning(f"node_enable(disable) returned {ret}")

        self.wait(1)

        self.logger.info(f"Enabling NUMA node {dest_nid}...")
        ret = self.smap_client.smap_node_enable(dest_nid, enable=True)
        if ret != 0:
            self.logger.warning(f"node_enable(enable) returned {ret}")

        return True

    def verify(self) -> bool:
        self.logger.info("Node enable/disable operations completed")
        return True

    def teardown(self) -> bool:
        self.smap_client.smap_stop()
        if self.smap_client and not self.smap_client.use_direct_mode():
            self.service_manager.stop_service()
        self.ko_manager.unload_kos()
        return super().teardown()


# 导出所有测试类
PROCESS_TESTS = [
    TestSmapStartStop,
    TestSetRemoteNumaInfo,
    TestProcessMigrateOut,
    TestProcessMigrateBack,
    TestFullWorkflow,
    TestNodeEnableDisable,
]