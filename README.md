
   	 基于Linux的轻量级Web服务器
	（主要开发语言：C++）

===============

一、特点如下：
------------
* 使用epoll和动态线程池实现半同步/半反应堆的高并发模型；
* 使用状态机解析HTTP请求报文，支持解析GET和POST请求；
* 基于小根堆和管道信号实现定时器，及时关闭超时的非活动连接；
* 使用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态；
* 使用RAII机制实现数据库连接池，同时实现用户注册登录功能；

二、启动准备
------------
1、服务器测试环境
	* Ubuntu版本16.04
	* MySQL版本5.7.29

2、浏览器测试环境
	* Windows、Linux均可
	* FireFox

3、测试前确认已安装MySQL数据库并建表

   （1） 建立mydb库
    create database mydb;

   （2）创建user表
    USE mydb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

   （3）添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'passwd');

  （4）修改main.cpp中的数据库初始化信息
    //数据库登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "mydb";

三、启动过程
------------
1、build 编译项目
    sh ./build.sh

2、启动server服务器项目
    ./server

3、浏览器端打开指定网页
    http://127.0.0.1:9006

4、个性化启动
     ./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]

      指令如下：

（1）-p，自定义端口号
	* 默认9006

（2）-l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入

（3）-m，listenfd和connfd的模式组合，默认使用LT + LT
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    	* 2，表示使用ET + LT
    	* 3，表示使用ET + ET

（4）-o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用

（5）-s，数据库连接数量
	* 默认为8

（6）-t，线程数量
	* 默认为8

（7）-c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志

（8）-a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型


项目参考：
------------
[1] Linux高性能服务器编程，游双著.
[2] https://github.com/qinguoyi/TinyWebServer
