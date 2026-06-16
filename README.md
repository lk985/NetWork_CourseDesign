# NetWork_CourseDesign

- Ping 命令实现
- 基于 TCP 的类 FTP 客户端/服务器，用于二进制文件传输
- 数据包捕获与 IPv4/TCP/UDP/ICMP 协议解析展示
- 数据链路层停止等待 / GBN 模拟骨架

## Build

```powershell
cmake -S . -B build-gcc -G Ninja -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe
cmake --build build-gcc
```

## Run

```powershell
.\build-gcc\network_course_design.exe help
.\build-gcc\network_course_design.exe ping 127.0.0.1
.\build-gcc\network_course_design.exe ftp-server
.\build-gcc\network_course_design.exe ftp-client 127.0.0.1
.\build-gcc\network_course_design.exe capture demo
.\build-gcc\network_course_design.exe datalink-demo
```

## 对于课题2
默认端口约定为2493


服务端：

```powershell
.\build-gcc\network_course_design.exe ftp-server
```

客户端：

```powershell
.\build-gcc\network_course_design.exe ftp-client 127.0.0.1
```


## 对于课题3

离线演示：

```powershell
.\build-gcc\network_course_design.exe capture demo
.\build-gcc\network_course_design.exe capture demo tcp
.\build-gcc\network_course_design.exe capture demo udp
.\build-gcc\network_course_design.exe capture demo icmp
```

实时抓包：

```powershell
.\build-gcc\network_course_design.exe capture 192.168.1.194 tcp
```

## 对于课题4

数据链路层模拟演示：

```powershell
.\build-gcc\network_course_design.exe datalink-demo
.\build-gcc\network_course_design.exe datalink-demo gbn 0.35 5 300
.\build-gcc\network_course_design.exe datalink-demo stopwait 0.00 1 200
```

说明：

- `datalink-demo` 会依次演示停止等待和回退 N 帧（GBN）两种模式。
- 也可以手动指定模式和参数：`datalink-demo [stopwait|gbn|all] [loss_rate] [window] [timeout_ms]`。
- 运行时会额外打印自定义链路层帧头摘要、CRC 校验结果和载荷预览。
- 模拟器会输出发送、ACK、丢帧、ACK 丢失、超时重传等日志。
- 统计信息中会展示 `sent`、`resent`、`acked`、`delivered`、`frame_drop`、`ack_drop`、`timeout_event` 等字段。
