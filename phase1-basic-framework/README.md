# Cloud Drive Demo - Phase 1 Basic Framework

一个简单的客户端-服务器云存储演示项目，实现基于TCP套接字的基本文件系统操作和文件传输。

## 功能特性

- **命令行界面** 类似于典型的Unix shell (`pwd`, `cd`, `ls`, `ll`, `mkdir`, `rmdir`, `rm`, `tree`, `cat`)
- **文件上传/下载** (`puts`, `gets`) 支持可靠的流式传输
- **多客户端并发** 使用线程池处理连接
- **会话管理** 为每个客户端维护独立的当前工作目录
- **自定义数据包协议** 包含状态码和内容长度验证
- **基于Epoll的事件循环** 实现高效的I/O多路复用

## 构建

### 服务端
```bash
cd server
make
```

### 客户端
```bash
cd client
make
```

清理构建：
```bash
make clean      # 删除目标文件和可执行文件
make rebuild    # 清理后重新构建
```

## 使用方法

### 1. 启动服务端
```bash
cd server
./server
```
服务端会从 `server.conf` 读取配置（默认：`127.0.0.1:8888`，4个工作线程）。  
你也可以指定自定义配置文件：
```bash
./server /path/to/config.conf
```
或者直接提供参数：
```bash
./server <server_ip> <server_port> <thread_count>
```

### 2. 运行客户端
```bash
cd client
./client <server_ip> <server_port>
```
示例：
```bash
./client 127.0.0.1 8888
```

客户端连接成功后，将显示如下提示符：

```
neozhuang@cloud-drive-demo:cloud-drive$
```

此提示符模拟实际的终端提示符，`:` 前面的字符串可自定义，`:` 后面的为当前工作目录的最后一个目录名，使用 `cd` 可切换。

### 3. 可用命令
| 命令 | 描述 |
|---------|-------------|
| `pwd`   | 显示当前工作目录 |
| `cd <目录>` | 切换目录 |
| `ls [路径]` | 列出目录内容（简短格式） |
| `ll [路径]` | 列出目录内容并显示详细信息（权限、大小、时间） |
| `mkdir <目录>` | 创建目录 |
| `rmdir <目录>` | 删除空目录 |
| `rm <文件>` | 删除文件 |
| `puts <本地文件>` | 上传文件到服务端 |
| `gets <远程文件>` | 从服务端下载文件到客户端的 `Downloads/` 目录 |
| `tree [路径]` | 显示目录树结构 |
| `cat <文件>` | 输出文件内容到标准输出 |

所有路径都是相对于服务端的会话路径（默认为 `cloud-drive/`）。

## 配置

### 服务端配置文件 (`server/server.conf`)
```ini
server_ip=127.0.0.1
port=8888
thread_count=4
```

### 客户端常量 (`client/include/deal_command.h`)
```c
#define USER_NAME "neozhuang"
#define SERVICE_NAME "cloud-drive-demo"
#define DOWNLOAD_PATH "Downloads"
```

## 协议细节

### 数据包结构
定义在 `include/packet.h`（客户端和服务端共用）：
```c
#define CONTENT_MAX 1024

typedef enum {
    PROMPT, PWD, CD, LS, LL, MKDIR, RMDIR, RM,
    PUTS, GETS, TREE, CAT, NOTCMD
} cmd_type_t;

typedef struct {
    cmd_type_t type;    // 命令类型
    int status;         // 0 = 成功, -1 = 错误
    int length;         // 内容长度（0..CONTENT_MAX）
    char content[CONTENT_MAX];
} packet_t;
```

### 数据包传输
- `send_packet()` / `recv_packet()` 按照字段顺序处理结构体的序列化。
- `sendn()` / `recvn()` 确保TCP上的二进制数据完整传输。

### 文件传输 (PUTS/GETS)
#### 上传 (`puts <文件名>`)
1. 客户端打开本地文件，发送第一个数据包，其中 `content` 字段包含文件大小。
2. 服务端接收大小，在会话目录下创建目标文件。
3. 客户端分多个数据包发送文件内容（每个包最大 `CONTENT_MAX`）。
4. 最后一个包的 `length = 0` 表示文件结束。
5. 服务端写入每个数据块，并验证接收的总大小是否匹配。
6. 服务端发送成功/错误响应。

#### 下载 (`gets <文件名>`)
1. 服务端检查文件是否存在，在第一个包中发送文件大小。
2. 客户端在 `Downloads/` 目录下创建本地文件。
3. 服务端通过数据包流式传输文件内容。
4. 最后一个包的 `length = 0`。
5. 客户端写入数据块，验证接收的总大小。

### 会话管理
- 每个客户端连接都有一个 `session_t` 结构存储其当前路径。
- 路径相对于 `cloud-drive/` 根目录解析。
- `session_init()` / `session_get()` / `session_update_path()` 管理每个客户端的状态。

## 项目结构

```
phase1-basic-framework/
├── README.md               （本文档）
├── client/
│   ├── Makefile
│   ├── include/
│   ├── src/
│   │   ├── command_handlers.c  （处理服务端响应）
│   │   ├── deal_command.c      （解析用户输入，发送请求）
│   │   ├── main.c
│   │   ├── network.c           （TCP连接）
│   │   ├── transmit_data.c     （发送/接收数据包，文件传输）
│   │   └── utils.c
│   └── Downloads/              （默认下载目录）
└── server/
    ├── Makefile
    ├── server.conf         （默认配置）
    ├── cloud-drive/        （服务端文件存储根目录）
    ├── include/
    └── src/
        ├── block_queue.c   （线程安全队列）
        ├── config.c        （读取配置文件/参数）
        ├── do_task.c       （每个 cmd_type_t 的命令处理器）
        ├── main.c          （服务端入口，epoll，线程池初始化）
        ├── network.c       （TCP监听/接受连接）
        ├── session.c       （会话管理）
        ├── stack.c         （路径规范化）
        ├── task_helper.c   （send_text_response，get_short_path 等）
        ├── threadpool.c    （线程池实现）
        └── transmit_data.c （服务端文件传输）
```

## 已知限制

- 未实现单用户认证（所有客户端共享同一个 `cloud-drive/` 根目录）。
- 不支持断点续传或部分传输。
- 服务端未实现日志记录功能。