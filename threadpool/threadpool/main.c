﻿#include "threadpool.h"

void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n", pthread_self(), num);
    sleep(1);
}

int main()
{
    //创建出一个线程池
    ThreadPool* pool = threadPoolCreate(3, 10, 100);
    for (int i = 0; i < 100; i++)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAdd(pool,taskFunc, num);
    }

    //让主线程睡眠一段时间，保证工作的线程处理完毕再退出
    sleep(30);

    threadPoolDestroy(pool);
    return 0;
}