# 基于 C++ 的 Linux 阻塞式 TCP Client/Server

这是一个使用 C++17 和 Linux/POSIX Socket API 逐阶段实现的 TCP Client/Server 学习项目。当前代码为 Stage 1：最小单客户端阻塞式 TCP Echo；后续 Stage 2、Stage 3 将继续在同一仓库中演进。

## 核心模型

```text
Client Process                         Server Process

socket()                              socket()
   ↓                                     ↓
client_fd                            listen_fd
   ↓                                     ↓
connect(127.0.0.1, 8080)             bind(0.0.0.0, 8080)
   │                                     ↓
   │                                  listen()
   │                                     ↓
   └────── TCP connection setup ─────→ accept()
                                         ↓
                                      conn_fd

client_fd  ←──── TCP connection ────→ conn_fd
```

`listen_fd` 只负责监听新连接，业务数据在 `client_fd` 与 `conn_fd` 之间传输。`accept()` 不会替换 `listen_fd`，而是返回一个新的 connected socket 文件描述符 `conn_fd`。

## 项目结构

```text
cpp-linux-tcp-client-server/
├── .gitignore
├── CMakeLists.txt
├── README.md
└── src/
    ├── client.cpp
    └── server.cpp
```

`build/` 是本地生成的构建目录，已通过 `.gitignore` 排除。

## 环境

已在 Fedora Linux 44（WSL）、GCC 16.1.1、CMake 4.3.0 环境验证。其他具有 C++17 编译器和 POSIX Socket API 的 Linux 环境也应能够构建。

## 构建

```bash
cmake -S . -B build
cmake --build build
```

构建后生成：

```text
build/server
build/client
```

## 运行

先在终端 A 启动服务端：

```bash
./build/server
```

再在终端 B 启动客户端：

```bash
./build/client
```

服务端完成一次连接和 Echo 后退出：

```text
server socket created, fd = 3
server bound to 0.0.0.0:8080
server listening on 0.0.0.0:8080
waiting for client...

new client connected
client connected from 127.0.0.1:53214
listen_fd = 3
conn_fd   = 4

received: Hello from client
echo sent

connection closed
```

客户端示例输出：

```text
client socket created, fd = 3
connected to 127.0.0.1:8080
client local endpoint: 127.0.0.1:53214

sent: Hello from client
echo from server: Hello from client

connection closed
```

实际 fd 和临时端口由内核分配，可能与示例不同。

## 调用职责

- `socket()`：创建内核 socket 对象，返回引用它的进程文件描述符。
- `bind()`：为服务端 socket 指定本地 IP 和端口。
- `listen()`：把服务端 socket 转换为监听 socket。
- `accept()`：阻塞等待连接，连接到达后返回新的 `conn_fd`。
- `connect()`：客户端主动发起 TCP 连接。
- `send()` / `recv()`：在已连接的 socket 上发送和接收字节。
- `getsockname()`：查询 socket 实际使用的本地 IP 和端口。
- `close()`：释放文件描述符对内核 socket 对象的引用。

服务端设置了 `SO_REUSEADDR`，方便程序结束后立即重新绑定测试端口。它不改变本项目的阻塞式、单客户端通信模型。

## TCP 四元组

客户端没有显式调用 `bind()`。执行 `connect()` 时，内核会自动选择合适的本地 IP 和临时端口。例如：

```text
client socket: 127.0.0.1:53214
server socket: 127.0.0.1:8080
```

该 TCP 连接由四元组标识：

```text
(source IP, source port, destination IP, destination port)
```

对于上面的客户端方向连接，即：

```text
(127.0.0.1, 53214, 127.0.0.1, 8080)
```

## 实验

### 1. 服务端未启动

确保 8080 端口没有服务端监听，然后执行：

```bash
./build/client
```

程序会记录 `connect()` 的返回值、`errno` 数值和错误文本。典型输出为：

```text
connect() returned -1, errno = 111 (Connection refused)
```

错误值以实际系统结果为准。

### 2. 验证 accept() 阻塞

只启动服务端：

```bash
./build/server
```

程序输出 `waiting for client...` 后停留，表示当前线程阻塞在 `accept()`。

### 3. 观察监听端口

服务端等待连接时，在另一终端执行：

```bash
ss -lntp | grep ':8080'
```

预期看到 `0.0.0.0:8080` 处于 `LISTEN` 状态。

### 4. 观察 TCP 连接

本示例通信很快完成。若要稳定观察 `ESTABLISHED`，可在调试器中让服务端暂停在 `accept()` 返回之后，再执行：

```bash
ss -ntp | grep ':8080'
```

客户端临时端口应与客户端的 `getsockname()` 输出以及服务端打印的远端端口一致。

### 5. 观察文件描述符

服务端等待连接时取得 PID：

```bash
server_pid=$(pgrep -n server)
ls -l /proc/$server_pid/fd
```

连接前能看到 `listen_fd` 对应的 `socket:[...]`。连接建立后、关闭前再次查看，会同时看到：

```text
listen_fd -> socket:[...]
conn_fd   -> socket:[...]
```

两者是不同的 fd，并指向职责不同的内核 socket 对象。由于 Echo 很快结束，可使用调试器在关闭 fd 前设置断点进行观察。

## 错误处理和资源释放

- 所有关键 socket 系统调用均检查返回值并打印错误信息。
- `send()` 使用循环处理部分发送，并在被信号中断时重试。
- 客户端根据已知消息长度循环 `recv()`，不假设一次调用必然收到完整 Echo。
- 字符串根据 `recv()` 返回的实际字节数构造，不依赖缓冲区中的 `\0`。
- 服务端正常退出前依次关闭 `conn_fd` 和 `listen_fd`。
- 客户端正常退出前关闭 `client_fd`。
- 失败分支关闭此前已经成功创建的文件描述符。

## 当前限制

- 一次只接受一个客户端；
- 只处理一次固定消息并回显；
- 使用阻塞式 `accept()` 和 `recv()`；
- 没有应用层消息协议；
- 没有多线程或 I/O 多路复用。

这些限制是第一阶段的设计边界。

## 演进路线

- Stage 1：单客户端、阻塞式 TCP Echo（当前版本）。
- Stage 2：在保持底层 POSIX Socket API 学习目标的前提下继续扩展。
- Stage 3：基于前两个阶段的提交历史继续演进。

每个阶段都在同一 Git 仓库中开发，并通过独立提交或版本标签保留可回溯的阶段基线。

## Git

仓库已经初始化，建议使用小步提交继续后续实验：

```bash
git status
git log --oneline
```
