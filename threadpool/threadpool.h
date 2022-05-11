#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

#define DEFAULT_TIME 10                 /*10s检测一次*/
#define MIN_WAIT_TASK_NUM 10            /*如果queue_size > MIN_WAIT_TASK_NUM 添加新的线程到线程池*/ 
#define DEFAULT_THREAD_VARY 10          /*每次创建和销毁线程的个数*/

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);//为什么要用静态成员函数呢-----class specific
    void run();
    void* adjust_thread(void* threadpool);

private:
    int m_thread_number;        //线程池中的线程数
    //线程数量选择：CPU密集型任务：CPU核数；IO密集型任务：>CPU核数
    //最佳线程数 = CPU当前可使用核数 * 当前CPU利用率 * （1 + CPU等待时间 / CPU响应时间）
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    pthread_t adjust_tid;       //管理线程，用来动态调整线程数
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //请求队列的信号量（能够看出要处理的任务数）
    //mutex用于互斥，保证任意时刻只有一个线程读写队列
    //semaphore用于同步，保证执行顺序（队列为空时不要读，队列满了不要写）。
    connection_pool *m_connPool;  //数据库
    //int m_actor_model;          //模型切换（这个切换是指Reactor/Proactor）
    //int live_thr_num;                   /* 当前存活线程个数 */
    //int busy_thr_num;                   /* 忙状态线程个数 */
    //int wait_exit_thr_num;              /* 要销毁的线程个数 */
};

template <typename T>
//线程池构造函数
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)    //不合理的线程数量和请求队列数量
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];     //pthread_t是长整型
    if (!m_threads)
        throw std::exception();
    //创造thread_number个线程而且存储起来
    for (int i = 0; i < thread_number; ++i)
    {
        //函数原型中的第三个参数，为函数指针，指向处理线程函数的地址。
        //若线程函数为类成员函数，
        //则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配，不能通过编译。
        //静态成员函数就没有这个问题，因为里面没有this指针。
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads; //失败
            throw std::exception();
        }
        //主要是将线程属性更改为unjoinable，使得主线程分离,便于资源的回收（由系统回收）
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads; //失败
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

template <typename T>
//reactor模式下的请求入队
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    //读写事件
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//将“待办工作”加入到请求队列
//传入的是fd
template <typename T>
//proactor模式下的请求入队
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//线程回调函数/工作函数，arg其实是this
//工作线程就是不断地等任务队列有新任务，而后就加锁取任务->取到任务解锁->执行任务
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    //调用时 *arg是this！
    //所以该操作其实是获取threadpool对象地址
    //将参数强转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;
    //线程池中每一个线程创建时都会调用run()，睡眠在队列中
    pool->run();
    return pool;
}

//线程池中的所有线程都睡眠，等待请求队列中新增任务
//回调函数会调用这个函数工作
//工作线程就是不断地等任务队列有新任务，然后就加锁取任务->取到任务解锁->执行任务
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        //信号量等待，请求队列长度-1
        m_queuestat.wait();
        //被唤醒后先加互斥锁
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务
        //将任务从请求队列删除
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        //Reactor
        if (1 == m_actor_model)
        {
            //IO事件类型：0为读
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //default:Proactor，线程池不需要进行数据读取，而是直接开始业务处理
        //之前的操作已经将数据读取到http的read和write的buffer中了
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

/* 管理线程 */
void threadpool<T>::adjust_thread(void* threadpool)
{
    int i;
    threadpool_t* pool = (threadpool_t*)threadpool;
    while (!pool->shutdown)
    {

        sleep(DEFAULT_TIME);                                    /*定时 对线程池管理*/

        pthread_mutex_lock(&(pool->lock));
        int queue_size = pool->queue_size;                      /* 关注 任务数 */
        int live_thr_num = pool->live_thr_num;                  /* 存活 线程数 */
        pthread_mutex_unlock(&(pool->lock));

        pthread_mutex_lock(&(pool->thread_counter));
        int busy_thr_num = pool->busy_thr_num;                  /* 忙着的线程数 */
        pthread_mutex_unlock(&(pool->thread_counter));

        /* 创建新线程 算法： 任务数大于最小线程池个数, 且存活的线程数少于最大线程个数时 如：30>=10 && 40<100*/
        if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num)
        {
            pthread_mutex_lock(&(pool->lock));
            int add = 0;

            /*一次增加 DEFAULT_THREAD 个线程*/
            for (i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY
                && pool->live_thr_num < pool->max_thr_num; i++)
            {
                if (pool->threads[i] == 0 || !is_thread_alive(pool->threads[i]))
                {
                    pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void*)pool);
                    add++;
                    pool->live_thr_num++;
                }
            }

            pthread_mutex_unlock(&(pool->lock));
        }

        /* 销毁多余的空闲线程 算法：忙线程X2 小于 存活的线程数 且 存活的线程数 大于 最小线程数时*/
        if ((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num)
        {
            /* 一次销毁DEFAULT_THREAD个线程, 隨機10個即可 */
            pthread_mutex_lock(&(pool->lock));
            pool->wait_exit_thr_num = DEFAULT_THREAD_VARY;      /* 要销毁的线程数 设置为10 */
            pthread_mutex_unlock(&(pool->lock));

            for (i = 0; i < DEFAULT_THREAD_VARY; i++)
            {
                /* 通知处在空闲状态的线程, 他们会自行终止*/
                pthread_cond_signal(&(pool->queue_not_empty));
            }
        }
    }

    return NULL;
}

#endif
