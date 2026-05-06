/*
 * mem_demo.c - 内存申请测试demo程序
 * 用于smap端到端测试，申请指定大小的内存并持续运行
 *
 * 编译: gcc -o mem_demo mem_demo.c
 * 使用: ./mem_demo <memory_mb> 或 numactl --membind=N ./mem_demo <memory_mb>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

static volatile int keep_running = 1;

void signal_handler(int sig)
{
    keep_running = 0;
    printf("\nReceived signal %d, exiting...\n", sig);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <memory_mb>\n", argv[0]);
        printf("Example: %s 512    # 申请512MB内存\n", argv[0]);
        printf("         numactl --membind=0 %s 512  # 绑定NUMA0\n", argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int mem_mb = atoi(argv[1]);
    if (mem_mb <= 0) {
        printf("Error: memory size must be positive\n");
        return 1;
    }

    size_t size = (size_t)mem_mb * 1024 * 1024;
    printf("Allocating %d MB (%zu bytes) memory...\n", mem_mb, size);

    char *buffer = (char *)malloc(size);
    if (!buffer) {
        perror("malloc failed");
        return 1;
    }

    /* 实际写入数据确保物理内存分配 */
    printf("Initializing memory...\n");
    for (size_t i = 0; i < size; i += 4096) {
        buffer[i] = 0x55;
    }

    printf("Memory allocated at %p\n", buffer);
    printf("PID: %d\n", getpid());
    printf("Running... (accessing memory every second)\n");
    printf("Press Ctrl+C to exit\n");
    fflush(stdout);

    while (keep_running) {
        sleep(1);
        /* 每秒访问部分内存保持"热"状态 */
        for (size_t i = 0; i < size; i += 4096) {
            buffer[i] = (char)((buffer[i] + 1) & 0xFF);
        }
    }

    free(buffer);
    printf("Memory released, exiting\n");
    return 0;
}