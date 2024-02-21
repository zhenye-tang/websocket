## websocket client

### 1. 如何编译
1. mkdir build
2. cd build && cmake ..
3. cmake --build . --target hello-server

### 2. 运行示例
```c
tzy@LAPTOP-595ER4EN:~/websocket/build/bin$ ./hello-server 
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