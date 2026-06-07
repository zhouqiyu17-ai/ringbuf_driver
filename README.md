# Linux 内核环形缓冲区字符设备驱动实验

本项目用于《操作系统原理》实验二，实现一个虚拟字符设备 `/dev/ringbuf`。

## 文件说明

- `ringbuf_driver.c`：Linux 内核字符设备驱动源码，内部实现 4KB 环形缓冲区。
- `Makefile`：用于编译内核模块和用户态测试程序。
- `writer.c`：用户态写测试程序，向 `/dev/ringbuf` 写入字符串。
- `reader.c`：用户态读测试程序，从 `/dev/ringbuf` 读取字符串。
- `test.c`：用户态综合测试程序，在同一个进程中先写后读。
- `README.md`：实验说明和运行命令。

## 快速运行

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) gcc make

make
sudo insmod ringbuf_driver.ko
ls -l /dev/ringbuf
sudo chmod 666 /dev/ringbuf

make user
./writer
./reader

dmesg | tail
sudo rmmod ringbuf_driver
dmesg | tail
make clean
```

如果 `class_create` 编译报参数数量错误，请检查当前内核版本。Linux 6.4 及以后使用 `class_create(CLASS_NAME)`，较老版本使用 `class_create(THIS_MODULE, CLASS_NAME)`。本项目已经用版本宏做了兼容处理。
