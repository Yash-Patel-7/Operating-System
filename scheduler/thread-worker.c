#include "thread-worker.h"

// time quantum
size_t TIME_QUANTUM = TIME_QUANTUM_VALUE;

// scheduler time interval
size_t SCHEDULER_TIME_INTERVAL = SCHEDULER_TIME_INTERVAL_VALUE;

// number of priority levels
size_t PRIORITY_LEVELS = PRIORITY_LEVELS_VALUE || 1;

// time period S (used only in MLFQ) which is the number of time quantums that have elapsed across all threads
size_t TIME_PERIOD_S = TIME_PERIOD_S_VALUE;

// Global variable for the current thread
tcb *current_thread = NULL;

// global variable that keeps track of whether the current thread has yielded
size_t has_yielded = 0;

//Global counter for total context switches and 
//average turn around and response time in microseconds
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;

// lock variable to keep this worker library synchronized
size_t lock = 0;

// make variable to keep track of the total sum of the response time and turn around time
// in microseconds of time
size_t total_turn_time = 0;
size_t total_resp_time = 0;

// total number of threads that have been created
worker_t total_threads_created = 0;

// total number of threads that have been completed
worker_t total_threads_completed = 0;

// variable to indicate whether worker create has been called for the first time
size_t is_first_time = 1;

// run queues for each priority level
tcb *runqueues[PRIORITY_LEVELS_VALUE || 1];

// global variable for total elapsed quantums across all threads (used only in MLFQ)
double total_elapsed_quantums = 0;

// completed queue that will hold all threads that have completed
tcb *completed = NULL;

// all_threads is an array of pointers to all the threads that have been created
tcb **all_threads = NULL;
size_t total_threads = 0;

// function that obtains the global lock
void obtain_lock() {
	while (__sync_lock_test_and_set(&lock, 1) == 1) {
		worker_yield();
	}
}

// function that releases the global lock
void release_lock() {
	__sync_lock_release(&lock);
}

// function that stops the timer
void stop_timer() {
	struct itimerval stop = {{0, 0}, {0, 0}};
	setitimer(ITIMER_PROF, &stop, NULL);
}

// function that starts the timer
void start_timer() {
	struct itimerval timer;
	timer.it_interval.tv_usec = SCHEDULER_TIME_INTERVAL;
	timer.it_interval.tv_sec = 0;

	timer.it_value.tv_usec = SCHEDULER_TIME_INTERVAL;
	timer.it_value.tv_sec = 0;

	setitimer(ITIMER_PROF, &timer, NULL);
}

// function that adds a thread to the all threads array
void add_to_all_threads(tcb *thread) {
	// if all_threads is NULL, add thread as the first element in the array
	if (all_threads == NULL) {
		all_threads = malloc(sizeof(tcb *));
		all_threads[0] = thread;
		total_threads++;
		return;
	}
	// if all_threads is not NULL, add thread to the end of the array using realloc
	total_threads++;
	all_threads = realloc(all_threads, sizeof(tcb *) * total_threads);
	all_threads[total_threads - 1] = thread;
}

// function that gets a thread from the all threads array given a tid
tcb* get_tid(worker_t tid) {
	// if all_threads is NULL, return NULL
	if (all_threads == NULL) {
		return NULL;
	}
	// if all_threads is not NULL, iterate over the array to find the thread with the given tid
	for (size_t i = 0; i < total_threads; i++) {
		if (all_threads[i]->tid == tid) {
			return all_threads[i];
		}
	}
	// if the thread with the given tid is not found, return NULL
	return NULL;
}

// function that enqueues a node to the end of the list
// example list: A->B->C->D, enqueue will add E to the end of the list A->B->C->D->E
void enqueue(tcb **head, tcb *thread) {
	// if the list is empty, add the thread to the list
	if (head == NULL) {
		return;
	}
	if (*head == NULL) {
		*head = thread;
		thread->next = thread;
		thread->prev = thread;
	}
	// if the list is not empty, add the thread to the end of the list
	else {
		// get the last node in the list
		tcb *last = (*head)->prev;
		// add the thread to the end of the list
		last->next = thread;
		thread->prev = last;
		thread->next = *head;
		(*head)->prev = thread;
	}
}

// function that dequeues a node from the front of the list
// example list: A->B->C->D, dequeue will remove A from the list and return it
// remaining list will be B->C->D
tcb* dequeue(tcb **head) {
	// if the list is empty, return NULL
	if (head == NULL) {
		return NULL;
	}
	if (*head == NULL) {
		return NULL;
	}
	// if the list is not empty, remove the first node from the list and return it
	else {
		// get the first node in the list
		tcb *first = *head;
		// if the list only has one node, remove the node and set the list to NULL
		if (first->next == first) {
			*head = NULL;
		}
		// if the list has more than one node, remove the first node and set the head to the next node
		else {
			*head = first->next;
			(*head)->prev = first->prev;
			(first->prev)->next = *head;
		}
		// return the first node after setting its next and prev to NULL
		first->next = NULL;
		first->prev = NULL;
		return first;
	}
}

// function that searches the linked list for a thread with the minimum elapsed quantums
// remove and return the thread with the minimum elapsed quantums
tcb* get_shortest_job(tcb **head) {
	// if head is NULL, return NULL
	if (head == NULL) {
		return NULL;
	}
	// if the list is empty, return NULL
	if (*head == NULL) {
		return NULL;
	}
	// iterate over the list to find the thread with the minimum elapsed quantums
	tcb *current = *head;
	tcb *shortest_job = current;
	double min_elapsed_quantums = current->elapsed_quantums;
	current = current->next;
	while (current != *head) {
		if (current->elapsed_quantums < min_elapsed_quantums) {
			min_elapsed_quantums = current->elapsed_quantums;
			shortest_job = current;
		}
		current = current->next;
	}
	// remove the thread with the minimum elapsed quantums from the list and return it
	if (shortest_job == *head) {
		return dequeue(head);
	}
	else {
		(shortest_job->prev)->next = shortest_job->next;
		(shortest_job->next)->prev = shortest_job->prev;
		shortest_job->next = NULL;
		shortest_job->prev = NULL;
		return shortest_job;
	}
}

// function that gets the next job from the topmost non-empty run queue
tcb* get_next_job() {
	// iterate over the run queues to find the topmost non-empty run queue
	for (size_t i = 0; i < PRIORITY_LEVELS; i++) {
		if (runqueues[i] != NULL) {
			return dequeue(&(runqueues[i]));
		}
	}

	// if all run queues are empty, return NULL
	return NULL;
}

// function that moves all threads to the top priority run queue
void move_to_top() {
	for (size_t i = 1; i < PRIORITY_LEVELS; i++) {
		// iterate over the run queue at the current priority level
		// dequeue each thread and enqueue it into the top priority run queue
		while (runqueues[i] != NULL) {
			// dequeue the thread from the run queue
			tcb *thread = dequeue(&(runqueues[i]));

			// update the priority of the thread to 0
			thread->priority = 0;

			// enqueue the thread into the top priority run queue
			enqueue(&(runqueues[0]), thread);
		}
	}
}

// function will find and return the thread with the given tid from a linked list
tcb* get_thread(tcb **head, worker_t tid) {
	// if head is NULL, return NULL
	if (head == NULL) {
		return NULL;
	}
	// if the list is empty, return NULL
	if (*head == NULL) {
		return NULL;
	}
	// iterate over the list to find the thread with the given tid
	// if the tid is not found, return NULL
	tcb *current = *head;
	while (current->tid != tid) {
		current = current->next;
		if (current == *head) {
			return NULL;
		}
	}
	// return the thread with the given tid
	return current;
}

// function that moves all threads from the blocked queue into their respective priority level run queues
void unblock_threads(tcb **blocked) {
	// if blocked is NULL, return
	if (blocked == NULL) {
		return;
	}
	// if the blocked queue is empty, return
	if (*blocked == NULL) {
		return;
	}
	// iterate over the blocked queue
	// so while there is still a thread in the blocked queue, dequeue it and enqueue it into its respective priority level run queue
	while (*blocked != NULL) {
		// dequeue the thread from the blocked queue
		tcb *thread = dequeue(blocked);

		// set the thread status to READY
		thread->status = READY;

		// enqueue the thread into its respective priority level run queue
		enqueue(&(runqueues[thread->priority]), thread);
	}
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// call the scheduler
	schedule(-1);

	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread

	// obtain the lock
	obtain_lock();

	// enqueue the current thread into the completed queue
	enqueue(&completed, current_thread);

	// update the total number of threads completed
	total_threads_completed++;

	// update status to completed
	current_thread->status = COMPLETED;

	// set the return value to the value_ptr
	current_thread->return_value = value_ptr;

	// move all threads in the blocked queue of the current thread to the run queue
	// so that they can join with the current thread and get its return value
	unblock_threads(&(current_thread->blocked));
	
	// update the total turn around time by adding the current time minus the start time
	struct timeval tv;
	gettimeofday(&tv, NULL);
	size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);
	total_turn_time += (current_time - current_thread->start_time);

	// update the average turn around time and then convert to milliseconds from microseconds
	avg_turn_time = (double)total_turn_time / (double)total_threads_completed;
	avg_turn_time /= 1000;


	// update the average response time
	// we have to add 1 to the total threads completed because the main thread is not included in the completed queue
	avg_resp_time = (double)total_resp_time / (double)(total_threads_created + 1);
	avg_resp_time /= 1000;
	
	// release the lock
	release_lock();

	// yield to scheduler
	worker_yield();
};


/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread

	// obtain the lock
	obtain_lock();
  
	tcb *thread_to_join = NULL;
	while (1) {
		thread_to_join = get_thread(&completed, thread);
		if (thread_to_join != NULL) {
			break;
		}

		// put the current thread into blocked queue of the thread to join
		enqueue(&(get_tid(thread)->blocked), current_thread);

		// set the status of the current thread to blocked
		current_thread->status = BLOCKED;
		
		// release the lock
		release_lock();

		// yield to the scheduler
		worker_yield();

		// obtain the lock
		obtain_lock();
	}

	// set the return value to the value_ptr
	if (value_ptr != NULL) {
		*value_ptr = thread_to_join->return_value;
	}

	// release the lock
	release_lock();

	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
	
	//- initialize data structures for this mutex

	// obtain the lock
	obtain_lock();

	// set the status to unlocked
	__sync_lock_release(&(mutex->status));

	// initialize the blocked queue to NULL
	mutex->blocked = NULL;

	// release the lock
	release_lock();

	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {
	// - use the built-in test-and-set atomic function to test the mutex
	// - if the mutex is acquired successfully, enter the critical section
	// - if acquiring mutex fails, push current thread into block list and
	// context switch to the scheduler thread

	// obtain the lock
	obtain_lock();

	// while this mutex status is LOCKED, put current thread in the mutex's blocked queue
	while (__sync_lock_test_and_set(&(mutex->status), 1) == 1) {
		// enqueue the current thread into the block queue for this mutex
		enqueue(&(mutex->blocked), current_thread);

		// set the status of the current thread to blocked
		current_thread->status = BLOCKED;
		
		// release the lock
		release_lock();

		// yield to the scheduler
		worker_yield();

		// obtain the lock
		obtain_lock();
	}

	// release the lock
	release_lock();

	return 0;
};


/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// obtain the lock
	obtain_lock();

	// set the mutex status to UNLOCKED
	__sync_lock_release(&(mutex->status));

	// unblock all threads in the blocked queue for this mutex
	unblock_threads(&(mutex->blocked));

	// release the lock
	release_lock();

	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
	// there is no dynamic memory created in worker_mutex_init
	// so do nothing
	return 0;
};

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)
	// YOUR CODE HERE

	// get current time in microseconds
	struct timeval tv;

	// if this is the first time that the scheduler is called, then run the next shortest job in the run queue
	if (current_thread->scheduled_time == 0 || current_thread->status != SCHEDULED) {
		// get the next shortest job from the run queue by using the get_shortest_job() function
		tcb *shortest_job = get_shortest_job(&(runqueues[0]));

		// update job's status to SCHEDULED
		shortest_job->status = SCHEDULED;

		// update the elapsed quantums of the current thread
		if (TIME_QUANTUM == 0) {
			current_thread->elapsed_quantums += (double)(current_thread->current_runtime) / 1;
		} else {
			current_thread->elapsed_quantums += (double)(current_thread->current_runtime) / TIME_QUANTUM;
		}

		// set the current thread to the shortest job
		current_thread = shortest_job;

		// get the current time
		gettimeofday(&tv, NULL);
		size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

		// if the shortest job's current scheduled time is 0, then that means that it has not been scheduled before
		// so we need to update the total response time by adding the scheduled time minus the start time
		if (shortest_job->scheduled_time == 0) {
			total_resp_time += (current_time - shortest_job->start_time);
		}

		// set the scheduled time of the shortest job to the current time
		shortest_job->scheduled_time = current_time;

		// use setcontext() to run the shortest job
		setcontext(&(shortest_job->context));
		return;
	}

	// if the current thread has not yielded and it has ran for less than time quantum, then continue running the current thread
	if (has_yielded == 0 && current_thread->current_runtime < TIME_QUANTUM) {
		// use setcontext() to continue running the current thread
		setcontext(&(current_thread->context));
		return;
	}

	// in all other cases, we enqueue the current thread into the run queue and pick the next shortest job to schedule to run
	// enqueue the current thread into the run queue
	enqueue(&(runqueues[0]), current_thread);

	// update status of current thread to READY
	current_thread->status = READY;

	// get the next shortest job from the run queue by using the get_shortest_job() function
	tcb *shortest_job = get_shortest_job(&(runqueues[0]));

	// update job's status to SCHEDULED
	shortest_job->status = SCHEDULED;

	// get the current time
	gettimeofday(&tv, NULL);
	size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

	// if the shortest job's current scheduled time is 0, then that means that it has not been scheduled before
	// so we need to update the total response time by adding the scheduled time minus the start time
	if (shortest_job->scheduled_time == 0) {
		total_resp_time += (current_time - shortest_job->start_time);
	}

	// set the scheduled time of the shortest job to the current time if it is different from the current thread
	if (shortest_job != current_thread) {
		shortest_job->scheduled_time = current_time;

		// update the elapsed quantums of the current thread
		if (TIME_QUANTUM == 0) {
			current_thread->elapsed_quantums += (double)(current_thread->current_runtime) / 1;
		} else {
			current_thread->elapsed_quantums += (double)(current_thread->current_runtime) / TIME_QUANTUM;
		}
	}

	// set the current thread to the shortest job
	current_thread = shortest_job;
	
	// use setcontext() to run the shortest job
	setcontext(&(shortest_job->context));
}


/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)
	// YOUR CODE HERE

	// if the total elapsed quantums across all threads is at least TIME PERIOD S, then move all threads to the top priority run queue
	if (total_elapsed_quantums >= TIME_PERIOD_S) {
		total_elapsed_quantums = 0;
		move_to_top();
	}

	// get current time in microseconds
	struct timeval tv;

	// if this is the first time that the scheduler is called, then run the next job in the run queues
	if (current_thread->scheduled_time == 0 || current_thread->status != SCHEDULED) {
		// get the next job from the run queues
		tcb *next_job = get_next_job();

		// update the next job's status to SCHEDULED
		next_job->status = SCHEDULED;

		// update the total elapsed quantums
		if (TIME_QUANTUM == 0) {
			total_elapsed_quantums += (double)(current_thread->current_runtime) / 1;
		} else {
			total_elapsed_quantums += (double)(current_thread->current_runtime) / TIME_QUANTUM;
		}

		// set the current thread to the next job
		current_thread = next_job;

		// get the current time
		gettimeofday(&tv, NULL);
		size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

		// if the next job's current scheduled time is 0, then that means that it has not been scheduled before
		// so we need to update the total response time by adding the scheduled time minus the start time
		if (next_job->scheduled_time == 0) {
			total_resp_time += (current_time - next_job->start_time);
		}

		// set the scheduled time of the next job to the current time
		next_job->scheduled_time = current_time;

		// use setcontext() to run the next job
		setcontext(&(next_job->context));
		return;
	}

	// if the current thread has not yielded and it has ran for less than time quantum, then continue running the current thread
	if (has_yielded == 0 && current_thread->current_runtime < TIME_QUANTUM) {
		// use setcontext() to continue running the current thread
		setcontext(&(current_thread->context));
		return;
	}

	// if the current thread has ran for at least time quantum, then we need to demote the current thread to a lower priority level
	if (current_thread->current_runtime >= TIME_QUANTUM) {
		// if the current thread's priority level is not the lowest, then demote the current thread
		if (current_thread->priority < PRIORITY_LEVELS - 1) {
			current_thread->priority++;
		}
	}

	// in all other cases, we enqueue the current thread into the run queue and pick the next job to schedule to run
	// enqueue the current thread into the run queue at its respective priority level
	enqueue(&(runqueues[current_thread->priority]), current_thread);

	// update status of current thread to READY
	current_thread->status = READY;

	// get the next job from the run queues
	tcb *next_job = get_next_job();

	// update the next job's status to SCHEDULED
	next_job->status = SCHEDULED;

	// get the current time
	gettimeofday(&tv, NULL);
	size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

	// if the next job's current scheduled time is 0, then that means that it has not been scheduled before
	// so we need to update the total response time by adding the scheduled time minus the start time
	if (next_job->scheduled_time == 0) {
		total_resp_time += (current_time - next_job->start_time);
	}

	// set the scheduled time of the next job to the current time if it is different from the current thread
	if (next_job != current_thread) {
		next_job->scheduled_time = current_time;

		// update the total elapsed quantums
		if (TIME_QUANTUM == 0) {
			total_elapsed_quantums += (double)(current_thread->current_runtime) / 1;
		} else {
			total_elapsed_quantums += (double)(current_thread->current_runtime) / TIME_QUANTUM;
		}
	}

	// set the current thread to the next job
	current_thread = next_job;
	
	// use setcontext() to run the next job
	setcontext(&(next_job->context));
}

/* scheduler */
static void schedule(int signo) {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)

	// if (sched == PSJF)
	//		sched_psjf();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// stop the timer
	stop_timer();

	// increment the total number of context switches by 2
	// because current thread -> scheduler -> next thread
	tot_cntx_switches += 2;

	// try to obtain the lock
	// if you fail to obtain the lock, then return
	if (__sync_lock_test_and_set(&lock, 1) == 1) {
		// start the timer
		start_timer();

		return;
	}	

	// if signo is negative, set has_yielded to 1
	if (signo == -1) {
		has_yielded = 1;
	} else {
		has_yielded = 0;
	}

	// save the context of the current thread
	current_thread->is_restored = 0;
	getcontext(&(current_thread->context));
	if (current_thread->is_restored == 1) {
		// release the lock
		release_lock();

		// start the timer
		start_timer();

		return;
	}
	current_thread->is_restored = 1;

	// update the current runtime
	if (current_thread->scheduled_time != 0) {
		// get the current time
		struct timeval tv;
		gettimeofday(&tv, NULL);

		// get the current time in microseconds
		size_t current_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

		// update current_runtime of the current thread
		current_thread->current_runtime = current_time - current_thread->scheduled_time;
	}


// - schedule policy
#ifndef MLFQ
	// Choose PSJF
	sched_psjf();
#else 
	// Choose MLFQ
	sched_mlfq();
#endif

}

// wrapper function that will call the user's function when starting a new thread
void new_thread_wrapper(void *(*function)(void*), void *arg) {
	// set is_restored to 1
	current_thread->is_restored = 1;

	// release the lock
	release_lock();

	// start the timer
	start_timer();

	// call the user's function passing in the argument
	function(arg);
}


/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {
	// obtain the lock
	obtain_lock();

	// if this is the first time worker_create is called, register the signal handler for the timer
	if (is_first_time == 1) {
		// register a signal handler for SIGPROF
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = &schedule;
		sigaction(SIGPROF, &sa, NULL);

		// initialize the run queues to NULL
		memset(runqueues, 0, sizeof(tcb*) * PRIORITY_LEVELS);
	}

	// create a new thread and initialize its context
	tcb *new_thread = (tcb *)malloc(sizeof(tcb));

	// initialize the context of the new thread
	getcontext(&(new_thread->context));

	// set uc_link to NULL
	new_thread->context.uc_link = NULL;

	// set stack pointer
	new_thread->context.uc_stack.ss_sp = (char *)malloc(STACK_SIZE);

	// set stack size
	new_thread->context.uc_stack.ss_size = STACK_SIZE;

	// set flags to 0
	new_thread->context.uc_stack.ss_flags = 0;

	// call makecontext to call the function being passed in with the argument being passed in
	makecontext(&(new_thread->context), (void (*)(void))&new_thread_wrapper, 2, function, arg);

	// update total number of threads created
	total_threads_created++;

	// set the thread id to the total number of threads created
	new_thread->tid = total_threads_created;

	// set the thread pointer to the thread id
	*thread = total_threads_created;

	// set the thread state to ready
	new_thread->status = READY;

	// add the new thread into all threads array
	add_to_all_threads(new_thread);

	// enqueue the new thread into the run queue
	enqueue(&(runqueues[0]), new_thread);

	// get time of day
	struct timeval tv;
	gettimeofday(&tv, NULL);

	// set the start time of the new thread to the current time in microseconds
	new_thread->start_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

	// set the scheduled time of the new thread to 0
	new_thread->scheduled_time = 0;

	// set current_runtime to 0
	new_thread->current_runtime = 0;

	// set the priority of the new thread to 0
	new_thread->priority = 0;

	// set is_restored to 0
	new_thread->is_restored = 0;

	// set elapsed quantums to 0
	new_thread->elapsed_quantums = 0;

	// set return value to NULL
	new_thread->return_value = NULL;

	// set blocked queue to NULL
	new_thread->blocked = NULL;

	// if this is the first time worker_create is called, initialize the scheduler and main context
	if (is_first_time == 1) {
		// set is_first_time to 0 so that this if statement is not called again
		is_first_time = 0;

		// create the main thread
		tcb *main = (tcb *)malloc(sizeof(tcb));

		// set the tid of the main thread to 0
		main->tid = 0;

		// set the status of the main thread to READY
		main->status = READY;

		// add the main thread into all threads array
		add_to_all_threads(main);

		// enqueue the main thread into the run queue
		enqueue(&(runqueues[0]), main);

		// get time of day
		struct timeval tv;
		gettimeofday(&tv, NULL);

		// set the start time of the main thread to the current time in microseconds
		main->start_time = (tv.tv_sec) * 1000000 + (tv.tv_usec);

		// set scheduled time to 0
		main->scheduled_time = 0;

		// set current runtime to 0
		main->current_runtime = 0;

		// set priority to 0
		main->priority = 0;

		// set is_restored to 0
		main->is_restored = 0;

		// set elapsed quantums to 0
		main->elapsed_quantums = 0;

		// set return value to NULL
		main->return_value = NULL;

		// set blocked queue to NULL
		main->blocked = NULL;

		// set the current thread to the main thread
		current_thread = main;

		// release the lock
		release_lock();

		// yield to the scheduler
		worker_yield();

		return 0;
	}

	// release the lock
	release_lock();

    return 0;
};

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

	fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
	fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
	fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}

