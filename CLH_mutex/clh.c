#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>

#define CACHE_LINE_SIZE 64

const int MAX_TIME_WAIT      = 1000;

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

    atomic_store(&newNode->isNextWait, waitStatus);

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

    PCLHNode newNode, prevNode = NULL;
    int ret = allocCLHNode(&newNode, TRUE); // next node will wait
    if (ret != SUCCESS)
    {
        fprintf(stderr, "[CLHLock] alloc new node error = %d\n", ret);
        return E_BADALLOC;
    }

    prevNode = atomic_exchange(&mutex->tail, newNode);

    while(atomic_load(&prevNode->isNextWait)){};

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

    atomic_store(&mutex->tail->isNextWait, FALSE);

    return SUCCESS;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Bad input format\n");
        exit(EXIT_FAILURE);
    }



    return 0;
}
