# NetWork_CourseDesign



- Ping命令框架
- 基于TCP的类FTP客户端/服务器用于二进制传输-
- 数据包头定义和解析辅助工具
- Datalink停止等待/GBN模拟骨架

## Build

```powershell
cmake -S . -B build
cmake --build build
```

## Run

```powershell
.\build\network_course_design.exe help
.\build\network_course_design.exe ftp-server 2121
.\build\network_course_design.exe ftp-client 127.0.0.1 2121
.\build\network_course_design.exe datalink-demo
```

“ping”和数据包捕获入口点保留在命令框架中，并可在当前接口之上扩展。
