#include "threadpool.h"

const int NUMBER = 2;
//����ṹ��
//���Ͻṹ����������ָ��Ͳ���
typedef struct Task
{
	void (*function)(void* arg);
	void* arg;
}Task;

//�̳߳ؽṹ��
//����������У�������ID�͹������߳�����ָ��
struct ThreadPool
{
	//�������
	Task* taskQ;
	int queueCapacity;		//����
	int queueSize;			//��ǰ�������
	int queueFront;			//��ͷ->ȡ����
	int queueRear;			//��β->������

	pthread_t managerID;	//�������߳�ID
	pthread_t* threadIDs;	//�������߳�ID

	int minNum;				//��С�̸߳���
	int maxNum;				//����̸߳���
	int busyNum;			//æ�̸߳���
	int liveNum;			//����̸߳���
	int exitNum;			//Ҫ���ٵ��̸߳���

	pthread_mutex_t mutexPool;		//���������̳߳أ���ֹ���ݻ��ң������Ƿ��ڴ棬������������м���ͬ��
	pthread_mutex_t mutexBusy;		//��busyNum����
	pthread_cond_t notFull;			//��������ǲ�������
	pthread_cond_t notEmpty;		//��������ǲ��ǿ���

	int shutdown;			//�ǲ���Ҫ�����̳߳أ�����Ϊ1��������Ϊ0
};

//�����̳߳ز���ʼ��
ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	//ΪʲôҪ��һ��do whileѭ������Ϊ����������̳߳سɹ������ڴ浫�ǹ����߳��������������п����ڴ�ʧ�ܵ������
	//��Ҫ�ͷŵ�ǰһ�����ٵ��ڴ�ռ�,�������жϹ����߳������Ƿ񿪱ٳɹ���ʱ����Ҫ�ͷŵ�pool�������ǳ��鷳��
	//���Լ���һ��do whileѭ�����������ʧ��ֱ��break���ڽ�����ʱ���ж��Ƿ���Ҫ�ͷ��ڴ�
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

		//��threadIDs�����ʼ��
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;		//����С�������
		pool->exitNum = 0;

		//��ʼ�����ͻ�������
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0)
		{
			printf("mutex or condition init failed...\n");
			break;
		}

		//�������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		//�����߳�
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; i++)
		{
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;
	} while (0);

	//�ͷ���Դ
	if (pool->threadIDs) free(pool->threadIDs);
	if (pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

//�����̳߳غ���
int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}

	//�ر��̳߳�
	pool->shutdown = 1;
	//�������չ������߳�
	pthread_join(pool->managerID, NULL);
	//�����������������̣߳���Ϊ����֮�����ɱ��
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//�ͷŶ��ڴ�
	//1.�������
	if (pool->taskQ)
	{
		free(pool->taskQ);
	}
	//2.�����߳�ID
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}
	//3.�������ͻ�������
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
	//�ж��ǲ�������
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		//�����������߳�
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);	
	}
	//�ж��̳߳��ǲ��Ǳ��ر���
	if (pool->shutdown)
	{
		//�Ƚ������˳�����ֹ����
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//�������
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;

	//�������������ά��һ�����ζ��� 
	//�ƶ�β���
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	//�����������������߳�
	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);
}

//��ȡ�̳߳��й������̵߳ĸ���
int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

//��ȡ�̳߳��л��ŵ��̵߳ĸ���
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
		//��Ϊ���̳߳���һ��������Դ��ʹ��֮ǰ��Ҫ����
		pthread_mutex_lock(&pool->mutexPool);
		//��ǰ��������Ƿ�Ϊ��
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//���������߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//�ж��ǲ���Ҫ�����߳�
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;
					//ֻҪ�Ǵ��������������߳�����ִ�У����Ѿ���mutexPool����������������̣߳����Ѿ�����ѻ�����������
					//���������˳�֮ǰ��Ҫ��������ֹ����
					pthread_mutex_unlock(&pool->mutexPool);
					//��ɱ
					threadExit(pool);
				}
			}
		}

		//�ж��̳߳��Ƿ񱻹ر���
		if (pool->shutdown)
		{
			//�Ƚ��������˳�����ֹ����
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		//�����������ȡ��һ������
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;

		//�������������ά��һ�����ζ��� 
		//�ƶ�ͷ���
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;

		//����
		//�����������������߳�
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

//�����߳���Ҫ����������
//ÿ�������Ӽ��һ�Σ��ж��ǲ���Ҫ����̻߳��������߳�
void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)
	{
		//ÿ��3s���һ��
		sleep(3);

		//ȡ���̳߳�������������͵�ǰ�̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//ȡ��æ�߳�����
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//����߳�
		//����ĸ��� > �����̸߳��� && �����߳��� < �����߳���
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

		//�����߳�
		//æ���߳� * 2 < �����߳� && �����߳� > ��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//�ù������߳���ɱ
			//û����������Ĺ����̻߳�������������Ҫ�Ȼ����ⲿ���߳�
			//����֮�������̻߳���������µ�ifģ�飬�����߳���ɱ
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

//���߳��˳�֮�󽫶�������λ����0
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
