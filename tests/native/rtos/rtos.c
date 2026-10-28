#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

atomic_int test_active_tasks;
atomic_bool test_fail_task;

struct native_semaphore { pthread_mutex_t lock; pthread_cond_t changed; unsigned count; };
static SemaphoreHandle_t semaphore(unsigned count)
{
    SemaphoreHandle_t sem=calloc(1,sizeof(*sem));
    pthread_mutex_init(&sem->lock,NULL); pthread_cond_init(&sem->changed,NULL); sem->count=count; return sem;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer) { (void)buffer; return semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return semaphore(0); }
static struct timespec deadline(TickType_t ticks)
{
    struct timespec time; clock_gettime(CLOCK_REALTIME,&time);
    time.tv_sec+=ticks/1000; time.tv_nsec+=(ticks%1000)*1000000L;
    if(time.tv_nsec>=1000000000L) { ++time.tv_sec; time.tv_nsec-=1000000000L; }
    return time;
}
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    pthread_mutex_lock(&sem->lock); struct timespec end=deadline(ticks);
    while(!sem->count) {
        if(!ticks) { pthread_mutex_unlock(&sem->lock); return 0; }
        int result=ticks==portMAX_DELAY ? pthread_cond_wait(&sem->changed,&sem->lock) :
            pthread_cond_timedwait(&sem->changed,&sem->lock,&end);
        if(result==ETIMEDOUT) { pthread_mutex_unlock(&sem->lock); return 0; }
    }
    --sem->count; pthread_mutex_unlock(&sem->lock); return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t sem)
{
    pthread_mutex_lock(&sem->lock); assert(!sem->count); sem->count=1;
    pthread_cond_signal(&sem->changed); pthread_mutex_unlock(&sem->lock);
}
void vSemaphoreDelete(SemaphoreHandle_t sem)
{ pthread_mutex_destroy(&sem->lock); pthread_cond_destroy(&sem->changed); free(sem); }

struct native_task {
    pthread_mutex_t lock; pthread_cond_t wake; unsigned notifications;
    void (*function)(void *); void *arg;
};
static _Thread_local TaskHandle_t s_current;
static void *start_task(void *arg)
{
    s_current=arg; s_current->function(s_current->arg); abort();
}
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)priority;
    if(atomic_load(&test_fail_task)) return 0;
    TaskHandle_t task=calloc(1,sizeof(*task)); task->function=function; task->arg=arg;
    pthread_mutex_init(&task->lock,NULL); pthread_cond_init(&task->wake,NULL);
    *out=task; pthread_t thread; atomic_fetch_add(&test_active_tasks,1);
    assert(pthread_create(&thread,NULL,start_task,task)==0); pthread_detach(thread); return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task)
{
    pthread_mutex_lock(&task->lock); ++task->notifications;
    pthread_cond_signal(&task->wake); pthread_mutex_unlock(&task->lock);
}
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks)
{
    TaskHandle_t task=s_current; struct timespec end=deadline(ticks);
    pthread_mutex_lock(&task->lock);
    while(!task->notifications) {
        int result=ticks==portMAX_DELAY ? pthread_cond_wait(&task->wake,&task->lock) :
            pthread_cond_timedwait(&task->wake,&task->lock,&end);
        if(result==ETIMEDOUT) break;
    }
    unsigned count=task->notifications;
    if(clear) task->notifications=0; else if(count) --task->notifications;
    pthread_mutex_unlock(&task->lock); return count;
}
void vTaskDelete(TaskHandle_t task)
{
    assert(!task); task=s_current;
    pthread_mutex_destroy(&task->lock); pthread_cond_destroy(&task->wake); free(task);
    atomic_fetch_sub(&test_active_tasks,1); pthread_exit(NULL);
}
