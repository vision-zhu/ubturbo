"""SMAP接口封装模块 - 通过ctypes调用libsmap.so或libubturbo_client.so"""

import os
import ctypes
from ctypes import Structure, c_int, c_uint32, c_uint64, c_uint16, c_uint8, c_bool, c_char_p, c_void_p, POINTER, CFUNCTYPE
from typing import Optional

# 常量定义（参考smap_interface.h）
MAX_NR_MIGOUT = 40
MAX_NR_MIGBACK = 50
MAX_NR_REMOVE = MAX_NR_MIGOUT
REMOTE_NUMA_NUM = 4

# 页面类型
PAGETYPE_NORMAL = 0  # 4K页
PAGETYPE_HUGE = 1    # 2M页

# 迁移模式
MIG_RATIO_MODE = 0   # 按比例迁移
MIG_MEMSIZE_MODE = 1 # 按内存大小迁移

# NUMA使能
DISABLE_NUMA_MIG = 0
ENABLE_NUMA_MIG = 1

# 数据源
NORMAL_DATA_SOURCE = 0
STATISTIC_DATA_SOURCE = 1


# ========== 结构体定义 ==========

class MigrateOutPayloadInner(Structure):
    """迁出内部参数"""
    _fields_ = [
        ("destNid", c_int),
        ("ratio", c_int),
        ("memSize", c_uint64),
        ("migrateMode", c_int),
    ]


class MigrateOutPayload(Structure):
    """迁出参数"""
    _fields_ = [
        ("srcNid", c_int),
        ("pid", c_int),
        ("count", c_int),
        ("inner", MigrateOutPayloadInner * REMOTE_NUMA_NUM),
    ]


class MigrateOutMsg(Structure):
    """迁出消息"""
    _fields_ = [
        ("count", c_int),
        ("payload", MigrateOutPayload * MAX_NR_MIGOUT),
    ]


class MigrateBackPayload(Structure):
    """迁回参数"""
    _fields_ = [
        ("srcNid", c_int),
        ("destNid", c_int),
        ("memid", c_uint64),
    ]


class MigrateBackMsg(Structure):
    """迁回消息"""
    _fields_ = [
        ("taskID", c_uint64),
        ("count", c_int),
        ("payload", MigrateBackPayload * MAX_NR_MIGBACK),
    ]


class RemovePayload(Structure):
    """移除参数"""
    _fields_ = [
        ("pid", c_int),
        ("count", c_int),
        ("nid", c_int * REMOTE_NUMA_NUM),
    ]


class RemoveMsg(Structure):
    """移除消息"""
    _fields_ = [
        ("count", c_int),
        ("payload", RemovePayload * MAX_NR_REMOVE),
    ]


class EnableNodeMsg(Structure):
    """NUMA使能消息"""
    _fields_ = [
        ("enable", c_int),
        ("nid", c_int),
    ]


class SetRemoteNumaInfoMsg(Structure):
    """设置远端NUMA信息"""
    _fields_ = [
        ("srcNid", c_int),
        ("destNid", c_int),
        ("size", c_uint64),
    ]


class MigrateNumaMsg(Structure):
    """NUMA间迁移消息"""
    _fields_ = [
        ("srcNid", c_int),
        ("destNid", c_int),
        ("count", c_int),
        ("memids", c_uint64 * MAX_NR_MIGOUT),
    ]


class MigrateEscapePayload(Structure):
    """进程NUMA间迁移参数"""
    _fields_ = [
        ("pid", c_int),
        ("srcNid", c_int),
        ("destNid", c_int),
        ("ratio", c_int),
        ("memSize", c_uint64),
        ("migrateMode", c_int),
    ]


class MigrateEscapeMsg(Structure):
    """进程NUMA间迁移消息"""
    _fields_ = [
        ("count", c_int),
        ("payload", MigrateEscapePayload * 40),  # MAX_NR_MIGRATE_ESCAPE
    ]


# 回调函数类型
LOGFUNC = CFUNCTYPE(None, c_int, c_char_p, c_char_p)


class SmapClient:
    """SMAP接口封装 - 支持libsmap.so直接调用"""

    def __init__(self, lib_path: str = "/opt/ubturbo/lib/libubturbo_client.so",
                 smap_lib_path: str = None):
        self.lib_path = lib_path
        self.smap_lib_path = smap_lib_path  # libsmap.so路径（直接调用，不需要ubturbo服务）
        self.lib = None
        self._use_direct_smap = False  # 是否使用libsmap.so直接调用
        self._load_library()

    def _load_library(self):
        """加载动态库"""
        # 优先尝试加载libsmap.so（直接调用模式）
        if self.smap_lib_path and os.path.exists(self.smap_lib_path):
            try:
                self.lib = ctypes.CDLL(self.smap_lib_path)
                self._use_direct_smap = True
                self._setup_functions()
                print(f"Loaded {self.smap_lib_path} (direct mode)")
                return
            except Exception as e:
                print(f"Failed to load {self.smap_lib_path}: {e}")

        # 尝试加载libubturbo_client.so（IPC模式）
        if os.path.exists(self.lib_path):
            try:
                lib_dir = os.path.dirname(self.lib_path)
                if lib_dir:
                    os.environ['LD_LIBRARY_PATH'] = lib_dir + ':' + os.environ.get('LD_LIBRARY_PATH', '')
                self.lib = ctypes.CDLL(self.lib_path)
                self._setup_functions()
                print(f"Loaded {self.lib_path}")
                return
            except Exception as e:
                print(f"Failed to load {self.lib_path}: {e}")

        print("Warning: No smap library loaded, tests will fail")

    def _setup_functions(self):
        """设置函数签名"""
        # ubturbo_smap_start - 第二个参数是回调函数，可为NULL，用c_void_p代替LOGFUNC
        self.lib.ubturbo_smap_start.argtypes = [c_uint32, c_void_p]
        self.lib.ubturbo_smap_start.restype = c_int

        # ubturbo_smap_stop
        self.lib.ubturbo_smap_stop.argtypes = []
        self.lib.ubturbo_smap_stop.restype = c_int

        # ubturbo_smap_is_running
        self.lib.ubturbo_smap_is_running.argtypes = []
        self.lib.ubturbo_smap_is_running.restype = c_bool

        # ubturbo_smap_migrate_out
        self.lib.ubturbo_smap_migrate_out.argtypes = [POINTER(MigrateOutMsg), c_int]
        self.lib.ubturbo_smap_migrate_out.restype = c_int

        # ubturbo_smap_migrate_back
        self.lib.ubturbo_smap_migrate_back.argtypes = [POINTER(MigrateBackMsg)]
        self.lib.ubturbo_smap_migrate_back.restype = c_int

        # ubturbo_smap_remove
        self.lib.ubturbo_smap_remove.argtypes = [POINTER(RemoveMsg), c_int]
        self.lib.ubturbo_smap_remove.restype = c_int

        # ubturbo_smap_node_enable
        self.lib.ubturbo_smap_node_enable.argtypes = [POINTER(EnableNodeMsg)]
        self.lib.ubturbo_smap_node_enable.restype = c_int

        # ubturbo_smap_remote_numa_info_set
        self.lib.ubturbo_smap_remote_numa_info_set.argtypes = [POINTER(SetRemoteNumaInfoMsg)]
        self.lib.ubturbo_smap_remote_numa_info_set.restype = c_int

        # ubturbo_smap_remote_numa_migrate
        self.lib.ubturbo_smap_remote_numa_migrate.argtypes = [POINTER(MigrateNumaMsg)]
        self.lib.ubturbo_smap_remote_numa_migrate.restype = c_int

    def is_library_loaded(self) -> bool:
        """检查库是否已加载"""
        return self.lib is not None

    def use_direct_mode(self) -> bool:
        """是否使用直接调用模式（libsmap.so）"""
        return self._use_direct_smap

    # ========== 接口方法 ==========

    def smap_start(self, page_type: int = PAGETYPE_NORMAL) -> int:
        """
        启动SMAP
        page_type: 0=4K页(进程模式), 1=2M页(虚机模式)
        返回: 0成功，非0失败
        """
        if not self.is_library_loaded():
            return -1
        return self.lib.ubturbo_smap_start(c_uint32(page_type), None)

    def smap_stop(self) -> int:
        """停止SMAP"""
        if not self.is_library_loaded():
            return -1
        return self.lib.ubturbo_smap_stop()

    def smap_is_running(self) -> bool:
        """查询SMAP是否运行"""
        if not self.is_library_loaded():
            return False
        return self.lib.ubturbo_smap_is_running()

    def smap_migrate_out(self, pids: list, dest_nid: int, ratio: int = 25,
                         src_nid: int = -1, pid_type: int = PAGETYPE_NORMAL,
                         mem_size: int = 0, migrate_mode: int = MIG_RATIO_MODE) -> int:
        """
        进程迁出到远端NUMA
        """
        if not self.is_library_loaded():
            return -1

        msg = MigrateOutMsg()
        msg.count = len(pids)

        for i, pid in enumerate(pids):
            msg.payload[i].pid = pid
            msg.payload[i].srcNid = src_nid
            msg.payload[i].count = 1
            msg.payload[i].inner[0].destNid = dest_nid
            msg.payload[i].inner[0].ratio = ratio
            msg.payload[i].inner[0].memSize = mem_size
            msg.payload[i].inner[0].migrateMode = migrate_mode

        return self.lib.ubturbo_smap_migrate_out(ctypes.byref(msg), c_int(pid_type))

    def smap_migrate_back(self, src_nid: int, dest_nid: int, task_id: int = 0) -> int:
        """从远端NUMA迁回内存"""
        if not self.is_library_loaded():
            return -1

        msg = MigrateBackMsg()
        msg.taskID = task_id
        msg.count = 1
        msg.payload[0].srcNid = src_nid
        msg.payload[0].destNid = dest_nid
        msg.payload[0].memid = 0

        return self.lib.ubturbo_smap_migrate_back(ctypes.byref(msg))

    def smap_remove(self, pids: list, pid_type: int = PAGETYPE_NORMAL,
                    numa_nodes: list = None) -> int:
        """移除进程管理"""
        if not self.is_library_loaded():
            return -1

        msg = RemoveMsg()
        msg.count = len(pids)

        for i, pid in enumerate(pids):
            msg.payload[i].pid = pid
            if numa_nodes:
                msg.payload[i].count = len(numa_nodes)
                for j, nid in enumerate(numa_nodes):
                    msg.payload[i].nid[j] = nid
            else:
                msg.payload[i].count = 0

        return self.lib.ubturbo_smap_remove(ctypes.byref(msg), c_int(pid_type))

    def smap_node_enable(self, nid: int, enable: bool = True) -> int:
        """使能/禁用NUMA节点"""
        if not self.is_library_loaded():
            return -1

        msg = EnableNodeMsg()
        msg.nid = nid
        msg.enable = ENABLE_NUMA_MIG if enable else DISABLE_NUMA_MIG

        return self.lib.ubturbo_smap_node_enable(ctypes.byref(msg))

    def smap_remote_numa_info_set(self, src_nid: int, dest_nid: int, size_mb: int) -> int:
        """设置远端NUMA可用内存量"""
        if not self.is_library_loaded():
            return -1

        msg = SetRemoteNumaInfoMsg()
        msg.srcNid = src_nid
        msg.destNid = dest_nid
        msg.size = size_mb

        return self.lib.ubturbo_smap_remote_numa_info_set(ctypes.byref(msg))

    def smap_remote_numa_migrate(self, src_nid: int, dest_nid: int,
                                  memids: list = None) -> int:
        """远端NUMA间迁移"""
        if not self.is_library_loaded():
            return -1

        msg = MigrateNumaMsg()
        msg.srcNid = src_nid
        msg.destNid = dest_nid
        if memids:
            msg.count = len(memids)
            for i, mid in enumerate(memids):
                msg.memids[i] = mid
        else:
            msg.count = 0

        return self.lib.ubturbo_smap_remote_numa_migrate(ctypes.byref(msg))