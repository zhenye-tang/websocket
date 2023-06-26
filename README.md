## websocket client

### 1. 如何编译
1. mkdir build
2. cd build && cmake ..
3. make

### 2. 运行示例
```c
tzy@LAPTOP-595ER4EN:~/websocket_client$ /home/tzy/websocket_client/build/websocket
hello world!!!
write [hello world!!!] success!!!!
recv server message, message length = 14, content is: hello world!!!
hello websocket!!!!!!!!!!!!!!
write [hello websocket!!!!!!!!!!!!!!] success!!!!
recv server message, message length = 29, content is: hello websocket!!!!!!!!!!!!!!
^Cbyby!!!
```

### 3. 待完善项
1. websocket + tls 实现安全传输，主要是将 mbedtls port 出一套接口
2. 增加一层服务层，使用多线程或者单线程+ select、poll、epoll 支持多个 client 共存
3. 在服务层的基础上实现 callback 机制、分片帧组包、控制帧处理
3. ....想起再说