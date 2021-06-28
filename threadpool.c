#include "threadpool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const int NUMBER=2;
//任务队列
typedef struct Task
{
    void (*function) (void * arg);
    void *arg;
}Task;

//线程池结构体
typedef struct ThreadPool
{
    Task* taskQ;
    int queueCapacity; //容量
    int queueSize;   //当前任务
    int queueFront; //队头
    int queueRear; //队尾

    pthread_t managerID; //管理者ID
    pthread_t* threadIDs; //工作的线程ID
    int minNum; //最小线程数量
    int maxNum; //最大线程数量
    int busyNum; //忙线程个数
    int liveNum; //存活线程个数
    int exitNum; //要销毁的程序个数
    pthread_mutex_t mutexpool; //锁整个线程池
    pthread_mutex_t mutexBusy; //锁busyNum；

    int shutdown; //销毁线程池
    pthread_cond_t notFull;  //任务队列是否满
    pthread_cond_t notEmpty; //任务队列是否空
}ThreadPool;

//创建线程并初始化
ThreadPool* threadPoolCreate(int min,int max,int queueSize)
{
    ThreadPool* pool=(ThreadPool *)malloc(sizeof(ThreadPool));
    do
    {
    if(pool==NULL){
        printf("malloc error\n");
        break;
    }
    pool->threadIDs=(pthread_t *)malloc(sizeof(pthread_t)*max);
    if(pool->threadIDs==NULL){
        printf("threads ID error\n");
        break;
    }
    memset(pool->threadIDs,0,sizeof(pthread_t)*max);
    pool->minNum=min;
    pool->maxNum=max;
    pool->busyNum=0;
    pool->liveNum=min;
    pool->exitNum=0;
    if(pthread_mutex_init(&pool->mutexpool,NULL)!=0||
        pthread_mutex_init(&pool->mutexBusy,NULL)!=0||
        pthread_cond_init(&pool->notEmpty,NULL)!=0||
        pthread_cond_init(&pool->notFull,NULL)!=0){
            printf("mutex or condition init fails\n");
            break;
        }

        //任务队列
        pool->taskQ=(Task*)malloc(sizeof(Task)*queueSize);
        pool->queueCapacity=queueSize;
        pool->queueSize=0;
        pool->queueFront=0;
        pool->queueRear=0;
        pool->shutdown=0;

        //创建线程
        pthread_create(&pool->managerID,NULL,manager,pool);
        for(int i=0;i<min;i++){
            pthread_create(&pool->threadIDs[i],NULL,worker,pool);
        }
        return pool;
    } while (0);
    //释放资源
    if(pool&&pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    if(pool&&pool->taskQ)
    {
        free(pool->taskQ);
    }
    if(pool){
        free(pool);
    }
    return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
    if(pool==NULL)
    {
        return -1;
    }
    //关闭线程池
    pool->shutdown=1;
    //阻塞管理者线程
    pthread_join(pool->managerID,NULL);
    //唤醒还在阻塞的消费者线程
    for(int i=0;i<pool->liveNum;i++)
    {
        pthread_cond_signal(&pool->notEmpty);
    }
    //释放堆内存
    if(pool->taskQ)
    {
        free(pool->taskQ);
    }
    if(pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_mutex_destroy(&pool->mutexpool);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);    
    free(pool);
    pool=NULL;
    return 0;
}

void threadPoolAdd(ThreadPool* pool,void(*func)(void*),void* arg)
{
    pthread_mutex_lock(&pool->mutexpool);
    while(pool->queueSize==pool->queueCapacity&&!pool->shutdown)
    {
        //判断生产队列是否已经满了，阻塞生产者线程
        pthread_cond_wait(&pool->notFull,&pool->mutexpool);
    }
    if(pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexpool);
        return;        
    }
    //添加任务
    pool->taskQ[pool->queueRear].function=func;
    pool->taskQ[pool->queueRear].arg=arg;
    pool->queueRear=(pool->queueRear+1)%pool->queueCapacity;
    pool->queueSize++;
    pthread_cond_signal(&pool->notEmpty); 
    pthread_mutex_unlock(&pool->mutexpool);
}

int threadPoolBusyNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int BusyNum=pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return BusyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexpool);
    int AliveNum=pool->liveNum;
    pthread_mutex_unlock(&pool->mutexpool);
    return AliveNum;
}
//worker实现
void * worker(void * arg)
{
    //arg是pthread_create传过来的pool,为动态创建的
    ThreadPool* pool=(ThreadPool *)arg;

    while(1){
        pthread_mutex_lock(&pool->mutexpool);
        //当前任务队列是否为空
        while(pool->queueSize==0&&!pool->shutdown)
        {
            //阻塞线程
            pthread_cond_wait(&pool->notEmpty,&pool->mutexpool);
            //判断线程是否需要被销毁
            if(pool->exitNum>0)
            {
                pool->exitNum--;
                if(pool->liveNum>pool->minNum)
                {
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexpool);
                    threadExit(pool);
                }
            }
        }
        //判断线程池是否关闭
        if(pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutexpool);
            threadExit(pool);
        }
        //从任务中取出一个任务（任务队列为临界资源）
        Task task;
        task.function=pool->taskQ[pool->queueFront].function;
        task.arg=pool->taskQ[pool->queueFront].arg;
        //移动头结点
        pool->queueFront=(pool->queueFront+1)%pool->queueCapacity;
        pool->queueSize--;
        //消费一个产品唤醒生产者
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexpool);
        printf("thread %ld start working\n",pthread_self());

        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        //执行选取任务的要求,表示线程忙，所以在此之前加锁

        task.function(task.arg);
        //函数指针两种调用方式
        //(*task.function)(task.arg);
        //执行完了之后再次调整busyNum
        printf("thread %ld end working\n",pthread_self());

        //释放arg堆内存
        free(task.arg);
        task.arg=NULL;
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
    }
    return NULL;
}

//manager实现
void * manager(void * arg)
{
    ThreadPool * pool=(ThreadPool *)arg;
    while(!pool->shutdown)
    {
        //每隔3s检测一次
        sleep(3);

        //取出线程池中的数量和当前线程数量
        pthread_mutex_lock(&pool->mutexpool);
        int queueSize=pool->queueSize;
        int liveNum=pool->liveNum;
        pthread_mutex_unlock(&pool->mutexpool);        

        //取出忙的线程数量

        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum=pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusy);        
        
        //添加线程
        //任务个数>存活个数&&存活线程<最大线程数
        if(queueSize>liveNum&&liveNum<pool->maxNum)
        {
            pthread_mutex_lock(&pool->mutexpool);
            int counter=0;
            for(int i=0;i<pool->maxNum
            &&counter<NUMBER&&
            pool->liveNum<pool->maxNum;i++)
            {
                if(pool->threadIDs[i]==0)
                {
                    pthread_create(&pool->threadIDs[i],NULL,worker,pool);
                    counter++;
                    pool->liveNum++;
                }
            }
            pthread_mutex_unlock(&pool->mutexpool);
        }
        //删除线程
        //忙的线程数*2<存活线程数&&存活线程数>最小线程数
        if(busyNum*2<liveNum&&liveNum>pool->minNum)
        {
            pthread_mutex_lock(&pool->mutexpool);
            pool->exitNum=NUMBER;
            pthread_mutex_unlock(&pool->mutexpool);
            //让工作线程自杀
            for(int i=0;i<NUMBER;++i)
            {
                pthread_cond_signal(&pool->notEmpty);
            }
        }
    }
    return NULL;
}



void threadExit(ThreadPool* pool)
{
    pthread_t tid=pthread_self();
    for(int i=0;i<pool->maxNum;++i)
    {
        if(pool->threadIDs[i]==tid){
            pool->threadIDs[i]=0;
            printf("threadExit called,%ld exiting\n",tid);
            pthread_join(pool->threadIDs[i],NULL);
            break;
        }
    }

    pthread_exit(NULL);
}






