
同步/异步日志系统
===============
1、使用单例模式创建日志系统，对服务器运行状态、错误信息和访问数据进行记录
2、该系统可以实现按天分类，超行分类功能，可以根据实际情况分别使用同步和异步写入两种方式
3、异步写入方式：利用阻塞队列，实质为生产者-消费者模型，创建一个写线程，工作线程将要写的内容push进队列，写线程从队列中取出内容，写入日志文件。