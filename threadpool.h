#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;
//创建线程池初始化
ThreadPool* threadPoolCreate(int min,int max,int queueSize);
//销毁线程池
int threadPoolDestroy(ThreadPool* pool);
//给线程池添加任务
void threadPoolAdd(ThreadPool* pool,void(*func)(void*),void* arg);
//获取线程池中工作的线程个数
int threadPoolBusyNum(ThreadPool* pool);
//获取线程池中活着的线程个数
int threadPoolAliveNum(ThreadPool* pool);
/////////////////////////
//消费者线程
void * worker(void * arg);
//管理者线程
void * manager(void * arg);

void threadExit(ThreadPool* pool);

#endif