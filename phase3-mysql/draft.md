## Client Draft

### 业务流程

启动
./client [.ini]

main.c:
1. 读取配置文件ini
2. 建立tcp连接
2. 打印横幅Cloud Drive
3. 打印菜单
   ```
    puts("1. Login");
    puts("2. Register");
    puts("3. Logout");
    puts("4. Exit");
   ```
4. 接收用户选择：
5. 逻辑分支switch：
6. 用户登录，发送用户登录请求
7. 用户注册，发送用户注册请求
8. 删除用户，发送用户删除请求
9.

## TODO

1. 协议使用流解析
2. mysql查询使用smat方法
