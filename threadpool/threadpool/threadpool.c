#include "threadpool.h"

const int NUMBER = 2;
//任务结构体
//符合结构，包括函数指针和参数
typedef struct Task
{
	void (*function)(void* arg);
	void* arg;
}Task;

//线程池结构体
//包括任务队列，管理者ID和工作的线程数组指针
struct ThreadPool
{
	//任务队列
	Task* taskQ;
	int queueCapacity;		//容量
	int queueSize;			//当前任务个数
	int queueFront;			//队头->取数据
	int queueRear;			//队尾->放数据

	pthread_t managerID;	//管理者线程ID
	pthread_t* threadIDs;	//工作的线程ID

	int minNum;				//最小线程个数
	int maxNum;				//最大线程个数
	int busyNum;			//忙线程个数
	int liveNum;			//存活线程个数
	int exitNum;			//要销毁的线程个数

	pthread_mutex_t mutexPool;		//锁整个的线程池，防止数据混乱，操作非法内存，对整个任务队列加入同步
	pthread_mutex_t mutexBusy;		//锁busyNum变量
	pthread_cond_t notFull;			//任务队列是不是满了
	pthread_cond_t notEmpty;		//任务队列是不是空了

	int shutdown;			//是不是要销毁线程池，销毁为1，不销毁为0
};

//创建线程池并初始化
ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	//为什么要用一个do while循环，因为如果出现了线程池成功开辟内存但是工作线程数组或者任务队列开辟内存失败的情况，
	//需要释放掉前一个开辟的内存空间,例如我判断工作线程数组是否开辟成功的时候还需要释放掉pool，这样非常麻烦，
	//所以加了一个do while循环，如果开辟失败直接break，在结束的时候判断是否需要释放内存
	do
	{
		if (pool == NULL)
		{
			printf("malloc threadpool failed...\n");
			break;
		}

		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL)
		{
			printf("malloc threadIDs failed...\n");
			break;
		}

		//将threadIDs数组初始化
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;		//和最小个数相等
		pool->exitNum = 0;

		//初始化锁和环境变量
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0)
		{
			printf("mutex or condition init failed...\n");
			break;
		}

		//任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		//创建线程
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; i++)
		{
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;
	} while (0);

	//释放资源
	if (pool->threadIDs) free(pool->threadIDs);
	if (pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

//销毁线程池函数
int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}

	//关闭线程池
	pool->shutdown = 1;
	//阻塞回收管理者线程
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的消费者线程（因为唤醒之后会自杀）
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//释放堆内存
	//1.任务队列
	if (pool->taskQ)
	{
		free(pool->taskQ);
	}
	//2.工作线程ID
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}
	//3.互斥锁和环境变量
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);
	//4.pool
	free(pool);
	pool = NULL;

	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	//判断是不是满了
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		//阻塞生产者线程
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);	
	}
	//判断线程池是不是被关闭了
	if (pool->shutdown)
	{
		//先解锁再退出，防止死锁
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//添加任务
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;

	//把任务队列数组维护一个环形队列 
	//移动尾结点
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	//唤醒阻塞的消费者线程
	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);
}

//获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

//获取线程池中活着的线程的个数
int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}

void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (1)
	{
		//因为是线程池是一个共享资源，使用之前需要加锁
		pthread_mutex_lock(&pool->mutexPool);
		//当前任务队列是否为空
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//阻塞工作线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//判断是不是要销毁线程
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;
					//只要是从上面解除阻塞的线程向下执行，就已经将mutexPool互斥锁给到了这个线程，就已经将这把互斥锁锁上了
					//所以我们退出之前需要解锁，防止死锁
					pthread_mutex_unlock(&pool->mutexPool);
					//自杀
					threadExit(pool);
				}
			}
		}

		//判断线程池是否被关闭了
		if (pool->shutdown)
		{
			//先解锁，再退出，防止死锁
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		//从任务队列中取出一个任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;

		//把任务队列数组维护一个环形队列 
		//移动头结点
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;

		//解锁
		//唤醒阻塞的生产者线程
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		printf("thread %ld start working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.function(task.arg);
		free(task.arg);
		task.arg = NULL;

		printf("thread %ld end working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
	return NULL;
}

//管理线程主要是做两件事
//每隔三秒钟检测一次，判断是不是要添加线程或者销毁线程
void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)
	{
		//每隔3s检测一次
		sleep(3);

		//取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//取出忙线程数量
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//添加线程
		//任务的个数 > 存活的线程个数 && 存活的线程数 < 最大的线程数
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER
				&& pool->liveNum < pool->maxNum; ++i)
			{
				if (pool->threadIDs[i] == 0)
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程
		//忙的线程 * 2 < 存活的线程 && 存活的线程 > 最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//让工作的线程自杀
			//没有事情可做的工作线程会阻塞，我们需要先唤醒这部分线程
			//唤醒之后工作的线程会进入阻塞下的if模块，会让线程自杀
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

//当线程退出之后将对于数组位置清0
void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; i++)
	{
		if (pool->threadIDs[i] == tid)
		{
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
