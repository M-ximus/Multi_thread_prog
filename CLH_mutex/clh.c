#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define CACHE_LINE_SIZE 64

const int MAX_TIME_WAIT = 10 * 10e6; // 10 milli
const int64_t NUM_OPERATIONS = 1000000;

const char FALSE = 0;
const char TRUE  = 1;

enum errCodeCLH
{
    SUCCESS    = 0,
    E_BADARGS  = -2,
    E_BADALLOC = -3,
};


typedef struct CLHNode
{
    _Atomic char isNextWait;
} CLHNode_t;
typedef CLHNode_t* PCLHNode;


typedef struct CLHMutex
{
    PCLHNode currNode;
    char alignment[CACHE_LINE_SIZE];
    _Atomic (PCLHNode) tail;
} CLHMutex_t;
typedef CLHMutex_t* PCLHMutex;


int allocCLHNode(PCLHNode* retNode, char waitStatus)
{
    errno = 0;
    PCLHNode newNode = (PCLHNode)calloc(1, sizeof(*newNode));
    if (newNode == NULL)
    {
        perror("[allocCLHNode] allocation newNode return error\n");
        return E_BADALLOC;
    }

    atomic_store_explicit(&newNode->isNextWait, waitStatus, memory_order_relaxed);

    *retNode = newNode;

    return SUCCESS;
}


int CLHConstr(PCLHMutex mutex)
{
    if (mutex == NULL)
    {
        fprintf(stderr, "[CLHConstr] init mutex is NULL\n");
        return E_BADARGS;
    }

    PCLHNode newNode = NULL;
    int ret = allocCLHNode(&newNode, FALSE);
    if (ret != SUCCESS)
    {
        fprintf(stderr, "[CLHConstr] alloc new node with false key error = %d\n", ret);
        return E_BADALLOC;
    }

    mutex->currNode = newNode;
    atomic_store(&mutex->tail, newNode);

    return SUCCESS;
}


int CLHDestr(PCLHMutex mutex)
{
    if (mutex == NULL)
        return SUCCESS;

    free(atomic_load(&mutex->tail));
}


int CLHLock(PCLHMutex mutex)
{
    if (mutex == NULL)
    {
        fprintf(stderr, "[CLHLock] mutex ptr is NULL\n");
        return E_BADARGS;
    }

    PCLHNode newNode = NULL;
    int ret = allocCLHNode(&newNode, TRUE); // next node will wait
    if (ret != SUCCESS)
    {
        fprintf(stderr, "[CLHLock] alloc new node error = %d\n", ret);
        return E_BADALLOC;
    }

    PCLHNode prevNode = atomic_exchange(&mutex->tail, newNode);
    //while(atomic_load(&prevNode->isNextWait));


    char is_locked = atomic_load_explicit(&prevNode->isNextWait, memory_order_relaxed);
    if (is_locked)
    {
        int sleepStage = 1;

        struct timespec timeToSleep;
        timeToSleep.tv_sec = 0;
        timeToSleep.tv_nsec = 0;

        sched_yield();
        is_locked = atomic_load(&prevNode->isNextWait);

        while(is_locked)
        {
            sched_yield();

            /*
            int random_part = rand() % 10;
            timeToSleep.tv_nsec = (sleepStage * 10 + random_part) * 1000;
            if (timeToSleep.tv_nsec > MAX_TIME_WAIT)
                timeToSleep.tv_nsec = MAX_TIME_WAIT;
            else
                sleepStage *= 2;

            //printf("I want to sleep %ld\n", timeToSleep.tv_nsec);
            nanosleep(&timeToSleep, NULL);
            */

            is_locked = atomic_load(&prevNode->isNextWait);
        }
    }

    free(prevNode);

    mutex->currNode = newNode;

    return SUCCESS;
}


int CLHUnlock(PCLHMutex mutex)
{
    if (mutex == NULL)
    {
        fprintf(stderr, "[CLHUnlock] mutex ptr is NULL\n");
        return E_BADARGS;
    }

    if (mutex->currNode == NULL)
    {
        printf("Dolboeb\n");
        return SUCCESS;
    }

    atomic_store(&mutex->currNode->isNextWait, FALSE);

    return SUCCESS;
}


struct CacheFriendlyThreadInfo
{
    int64_t   dataEnd;
    int64_t   counter;
    PCLHMutex mutex;
    double    execTime;
    int       thread_id;
    char      alignment[64];
} __attribute__((__aligned__(64)));
typedef struct CacheFriendlyThreadInfo threadInfo_t;
typedef threadInfo_t* PthreadInfo;


void* threadRoutine(void* inputData)
{
    if (inputData == NULL)
    {
        fprintf(stderr, "[threadRoutine] Invalid inputData\n");
        exit(EXIT_FAILURE);
    }

    threadInfo_t* threadLocalData = (threadInfo_t*)inputData;
    int64_t stopValue = threadLocalData->dataEnd;
    PCLHMutex mutex = threadLocalData->mutex;
    int id = threadLocalData->thread_id;

    //sleep(1);

    errno = 0;
    struct timespec start, end;
    int ret_start = clock_gettime(CLOCK_REALTIME, &start);

    int64_t counter = 0;
    while (threadLocalData->counter < stopValue)
    {
        //printf("[%d] tried to lock\n", id);
        CLHLock(mutex);
        (threadLocalData->counter)++;
        //printf("[%d] tried to unlock\n", id);
        CLHUnlock(mutex);
        //printf("%ld counter\n", threadLocalData->counter);
    }

    int ret_end = clock_gettime(CLOCK_REALTIME, &end);
    if (ret_start < 0 || ret_end < 0)
    {
        perror("[threadRoutine] Invalid time measurement\n");
        exit(EXIT_FAILURE);
    }

    long int sec  = end.tv_sec - start.tv_sec;
    long int nano = end.tv_nsec - start.tv_nsec;
    threadLocalData->execTime = sec + nano * 1e-9;

    //printf("I finished\n");

    return NULL;
}


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Bad input format\n");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));

    errno = 0;
    long int numThreads = strtol(argv[1], NULL, 10);
    if (errno < 0)
    {
        perror("Transform input srgv[1] to number of threads return error\n");
        exit(EXIT_FAILURE);
    }

    CLHMutex_t mutex;
    int errCode = CLHConstr(&mutex);
    if (errCode)
    {
        fprintf(stderr, "CLH constructor return error %d\n", errCode);
        exit(EXIT_FAILURE);
    }

    errno = 0;
    pthread_t* threadPool = (pthread_t*)calloc(numThreads, sizeof(*threadPool));
    if (threadPool == NULL)
    {
        perror("Alloc memory for threadPool return error\n");
        exit(EXIT_FAILURE);
    }
    threadInfo_t* localVarArr = (threadInfo_t*)calloc(numThreads, sizeof(*localVarArr));
    if (localVarArr == NULL)
    {
        perror("Alloc memory for localVarArr return error\n");
        free(threadPool);
        exit(EXIT_FAILURE);
    }

    int64_t stopCounterValue = NUM_OPERATIONS / numThreads;
    errno = 0;
    for (long int i = 0; i < numThreads; ++i)
    {
        localVarArr[i].dataEnd   = stopCounterValue;
        localVarArr[i].mutex     = &mutex;
        localVarArr[i].thread_id = i;
        localVarArr[i].counter   = 0;
        int err = pthread_create(&(threadPool[i]), NULL, threadRoutine, &(localVarArr[i]));
        if (err < 0)
        {
            perror("thread creation return error\n");
            exit(EXIT_FAILURE);
        }
        //printf("I started %ld with stop value = %lu\n", i, stopCounterValue);
    }

    errno = 0;
    double average = 0.0;
    double max     = 0.0;
    double min     = NUM_OPERATIONS;
    for (long int i = 0; i < numThreads; ++i)
    {
        int ret = pthread_join(threadPool[i], NULL);
        if (ret < 0)
        {
            perror("join thread return error\n");
            exit(EXIT_FAILURE);
        }
        average += localVarArr[i].execTime;
        if (localVarArr[i].execTime > max)
            max = localVarArr[i].execTime;
        if (localVarArr[i].execTime < min)
            min = localVarArr[i].execTime;
    }
    //average /= numThreads;

    printf("sum = %lg\nmax = %lg\nmin = %lg\n", average, max, min);

    free(threadPool);
    free(localVarArr);
    CLHDestr(&mutex);

    return 0;
}
