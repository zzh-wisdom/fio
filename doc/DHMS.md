# 测试fio DHMS指南

1. 编译fio，输入命令 `./configure && make`

2. 编辑工作目录下的 dhms.h 头文件和 dhms.c 源文件。两个文件的接口已经准备好，仅需要在上面实现功能即可。（其他函数和类型定义使用 `static` 防止命名冲突）

    > 注：其中 `dhms_create()` 需要传入的 `conf_file` 字符串参数，是运行客户端所需要的配置文件，在运行命令中传入。

3. 编译 dhms.c 生成共享链接库，输入命令`gcc -shared -fPIC -o libdhms.so dhms.c`。

4. libdhms.so 在 /usr/local/lib 中建立软链接，dhms.h 在 /usr/local/include 中建立软链接。

5. 在 examples 目录下有 DHMS 示例文件，有 dhms-write.fio 和 dhms-read.fio，输入 `./fio examples/dhms-write.fio` 运行顺序写示例。

    > 注：numjob 是客户端数量，bs 是负载大小，rw 是负载类型（顺序读 read、顺序写 write、随机读 randread、随机写 randwrite、混合读写 rw），size 是测试集大小，conf 是客户端配置文件，runtime 是fio**运行总时间** （单位：秒）

    如果出现链接失败错误，键入命令 `export LD_LIBRARY_PATH=/usr/local/lib` 再尝试运行。

## 运行环境

先运行master和DN，然后启动fio测试，内部会启动numjob个客户端，在conf中获取必要配置项与master建立链接。

客户端独占一个线程，共享同一个pool。

测试先预写4字节，然后压测。

`./fio --enghelp=dhms` 命令查看 dhms 的配置项信息
