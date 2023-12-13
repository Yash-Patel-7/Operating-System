#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

// global variable that is the maximum amount of time that a thread can run before it is switched out
#define TIME_QUANTUM_VALUE 10000 // microseconds

// global variable that determines the time interval after which the scheduler is invoked
#define SCHEDULER_TIME_INTERVAL_VALUE 10000 // microseconds

// global variable that is the number of priority levels used in the scheduler
#define PRIORITY_LEVELS_VALUE 4

// global variable that defines the time period S after which all threads are moved to the highest priority level
#define TIME_PERIOD_S_VALUE 10 // number of time quantums that have elapsed across all threads

// states of a thread
#define READY 0
#define SCHEDULED 1
#define BLOCKED 2
#define COMPLETED 3

// define stack size 
#define STACK_SIZE 8192

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>

typedef unsigned int worker_t;

typedef struct TCB {
	/* add important states in a thread control block */
	// thread Id
	worker_t tid;
	// thread context
	ucontext_t context;
	// thread status
	size_t status;
	// the time at which the thread was added to the run queue for the first time
	size_t start_time;
	// the time at which the thread was scheduled to run
	size_t scheduled_time;
	// the time that the thread has been running consecutively (without being switched out)
	size_t current_runtime;
	// priority level of the thread
	size_t priority;
	// variable to indicate whether this thread got restored
	size_t is_restored;
	// the number of time quantums that have elapsed while the thread was running
	double elapsed_quantums;
	// variable that stores the return value of the thread
	void *return_value;
	// blocked queue for threads that are waiting to join this thread
	struct TCB *blocked;
	// next thread in the queue
	struct TCB *next;
	// previous thread in the queue
	struct TCB *prev;

} tcb; 

/* mutex struct definition */
typedef struct worker_mutex_t {
	/* add something here */
	// lock status
	size_t status;
	// blocked queue for threads that are waiting to obtain this mutex lock
	tcb *blocked;
} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// we are going to make a CIRCULAR DOUBLY LINKED LIST
// for this linked list, enqueue will add a new node to the end of the list
// dequeue will remove the first node of the list and return it

// function that enqueues a node to the end of the list
// example list: A->B->C->D, enqueue will add E to the end of the list A->B->C->D->E
void enqueue(tcb **head, tcb *thread);

// function that dequeues a node from the front of the list
// example list: A->B->C->D, dequeue will remove A from the list and return it
// remaining list will be B->C->D
tcb* dequeue(tcb **head);

// function that searches the linked list for a thread with the minimum elapsed quantums
// remove and return the thread with the minimum elapsed quantums
tcb* get_shortest_job(tcb **head);

// function will find and return the thread with the given tid
tcb* get_thread(tcb **head, worker_t tid);

// function that moves all threads from the blocked queue into their respective priority level run queues
void unblock_threads(tcb **blocked);

// function that adds a thread to the all threads array
void add_to_all_threads(tcb *thread);

// function that gets a thread from the all threads array given a tid
tcb* get_tid(worker_t tid);

/* Function Declarations: */

// scheduler
static void schedule(int signo);

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif


