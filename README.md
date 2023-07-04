## websocket client

### 1. 如何编译
1. mkdir build
2. cd build && cmake ..
3. make

### 2. 运行示例
```c
tzy@LAPTOP-595ER4EN:~/websocket/build$ ./websocket 
connect websocket server success!!!
onmessage recv:hello server
hello world!!!!
write [hello world!!!!] success!!!!
onmessage recv:hello world!!!!
hello websocket!!!!!!
write [hello websocket!!!!!!] success!!!!
onmessage recv:hello websocket!!!!!!
^Cbyby!!!
```

### 3. 待完善项
1. 服务层 read/write 接口实现
2. 实现异步机制，将服务层接口全部让异步线程去处理
3. ....想起再说