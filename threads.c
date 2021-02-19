#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

#define THREAD_STATE_READY 0
#define THREAD_STATE_EXIT 1
#define THREAD_STATE_SLEEP 2

typedef struct thread {
    Tid id;
    int state;
    ucontext_t ctx;
    void * sp;

    struct wait_queue* t_wq;
    struct thread * next;
} thread;
//ready queue
struct thread *rq;
//exit queue
struct thread *eq;
//multiple wait queues needed
struct wait_queue {
    struct thread *wq_node;
};

struct thread *t_running;
struct thread *threads[THREAD_MAX_THREADS];
struct wait_queue *queue_wq;

void thread_stub(void (*thread_main)(void *), void *arg) {
    interrupts_set(1);
    //calls the main thread function with arg
    thread_main(arg);
    thread_exit();
}
// push element to the last of queue 
void add_last(struct thread * head, struct thread * t) {
    
    thread* tmp = head->next;
    
    if(head == NULL || tmp == NULL){
        head->next = t;
        t->next = NULL;
        return;
    }
    
    while(tmp->next != NULL){
        tmp = tmp->next;
    }

    tmp->next = t;
    t->next = NULL;
      
}
// removing the first thread in the queue 
struct thread *remove_first(struct thread * t) {
    struct thread * tmp = t->next;

    if (tmp == NULL){
        return tmp;
    }
    
    t->next = tmp->next;
    tmp->next = NULL;
    return tmp;

}
// search in the queue for a particular thread id 
struct thread *get_by_id(struct thread * t, Tid id, int is_remove) {
    if(t == NULL) {
        return NULL;
    }
    struct thread *rt = NULL;
    struct thread * tmp = t->next;
    struct thread * prev_tmp = tmp;
    
    
    while(tmp != NULL) {
        if(tmp->id == id) {
                rt = tmp;
                break;
        }
        prev_tmp = tmp;
        tmp = tmp->next;
    }
    
    if(rt != NULL){
        if (is_remove) {

            t->next = rt->next;
            prev_tmp->next = rt->next;
            rt->next = NULL;
            return rt;
        }
    }

    return NULL;
};
// intialize first thread inside the global thread array declared above
void thread_init(void) {

    rq = malloc(sizeof(struct thread));
    rq->next = NULL;

    eq = malloc(sizeof(struct thread));
    eq->next = NULL;

    t_running = malloc(sizeof(struct thread));
    t_running->id = 0;
    t_running->state = THREAD_STATE_READY; // Because its a user thread already running
    t_running->t_wq = wait_queue_create();
    getcontext(&t_running->ctx);

    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        threads[i] = NULL;
    }
    threads[0] = t_running;
}

Tid thread_id() {

    Tid id = t_running->id;

    return id;
}

Tid thread_create(void (*fn) (void *), void *parg) {
    int enabled = interrupts_set(0);

    int id = -1;
    for(int i = 0; i < THREAD_MAX_THREADS; i++) {
        if(threads[i] == NULL) {
                id = i;
                break;
        }
    }

    // if the thread array is full then no thread id can be created 
    if(id == -1) {
            interrupts_set(enabled);
            return THREAD_NOMORE;
    }
    
    struct thread *new = (struct thread *) malloc (sizeof(struct thread));
    if(new == NULL) {
            interrupts_set(enabled);
            return THREAD_NOMEMORY;
    }
    
    new->id = id;
    new->state = THREAD_STATE_READY;   
    new->sp = (void *)malloc(THREAD_MIN_STACK);
    if (new->sp == NULL) {
        interrupts_set(enabled);
        return THREAD_NOMEMORY;
    }
    new->next = NULL;
    new->t_wq = wait_queue_create();
    
    getcontext(&new->ctx);
    
    new->ctx.uc_mcontext.gregs[REG_RIP] = (unsigned long)&thread_stub;
    new->ctx.uc_mcontext.gregs[REG_RSP] = (unsigned long)(new->sp + THREAD_MIN_STACK-8);
    new->ctx.uc_mcontext.gregs[REG_RDI] = (long long int)(fn);
    new->ctx.uc_mcontext.gregs[REG_RSI] = (long long int)(parg);
    
    threads[id] = new;
    add_last(rq, new);

    interrupts_set(enabled);
    return id;
}

void thread_free() {
    int enabled = interrupts_set(0);

    struct thread * tmp = eq->next;

    while(tmp != NULL) {
        struct thread * tmp_next = tmp->next;
        
        free(tmp->sp);
        free(tmp);

        tmp = tmp_next;
    }
    
    eq->next = NULL;

    interrupts_set(enabled);
}

Tid thread_yield(Tid want_tid) {
    int enabled = interrupts_set(0);
    Tid current_running = t_running->id;
    if (current_running == want_tid || want_tid == THREAD_SELF) {
    	volatile int is_set_ctx = 0;
    	getcontext(&t_running->ctx);
    	if (is_set_ctx == 0) {
            is_set_ctx = 1;
            setcontext(&t_running->ctx);
        }
        
    	interrupts_set(enabled);
    	return t_running->id;

    } else if (want_tid == THREAD_ANY) {
        if (rq->next == NULL) {

            interrupts_set(enabled);
            return THREAD_NONE;
            
        } else {
            struct thread * target = remove_first(rq);
            assert(target->state == THREAD_STATE_READY);
            Tid id = target->id;
            add_last(rq, t_running);

            volatile int is_set_ctx = 0;
            getcontext(&t_running->ctx);
            if (is_set_ctx == 0){
                is_set_ctx = 1;
                t_running = target;
                setcontext(&t_running->ctx);
            }

            thread_free();
            interrupts_set(enabled);
            return id;
        }

    } else if (want_tid > THREAD_MAX_THREADS || want_tid < 0) {

        interrupts_set(enabled);
        return THREAD_INVALID;

    } else if(threads[want_tid] == NULL) {

            interrupts_set(enabled);
            return THREAD_INVALID;

    } else {
        struct thread * tmp = get_by_id(rq, want_tid, 1);
        assert(tmp != NULL);
        add_last(rq, t_running);
        
        volatile int is_set_ctx = 0;
        getcontext(&t_running->ctx);
        if (is_set_ctx == 0){
            is_set_ctx = 1;
            t_running = tmp;
            setcontext(&t_running->ctx);
        }

        thread_free();
        interrupts_set(enabled);
        return want_tid;
    }

    interrupts_set(enabled);
    return THREAD_FAILED;
}

void thread_exit() {
    interrupts_set(0);

    add_last(eq, t_running);
    threads[t_running->id]= NULL;
    thread_wakeup(t_running->t_wq, 1);
    wait_queue_destroy(t_running->t_wq);
    
    struct thread * tmp = remove_first(rq);
    if (tmp == NULL){
        exit(0);
    }
    t_running = tmp;

    setcontext(&t_running->ctx);
}

Tid thread_kill(Tid tid) {
    int enabled = interrupts_set(0);

    if (tid < 0 || tid > THREAD_MAX_THREADS || tid == t_running->id || threads[tid] == NULL) {
        interrupts_set(enabled);
    	return THREAD_INVALID;
    }
    
    threads[tid]->ctx.uc_mcontext.gregs[REG_RIP] = (long long int)&thread_exit;

    interrupts_set(enabled);
    return tid;/*
    int enabled = interrupts_set(0);
    if(tid < 0 || tid >= THREAD_MAX_THREADS || 
        tid == thread_id() || threads[tid] == NULL) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    threads[tid]->ctx.uc_mcontext.gregs[REG_RIP] = (long long int)&thread_exit;
    interrupts_set(enabled);
    return tid;*/
    
}
