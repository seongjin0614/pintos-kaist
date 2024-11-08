#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어 구조체.
   여러 스레드가 대기하고 있을 수 있으며, value 값을 통해 현재 세마포어의 상태를 추적 */
struct semaphore {
	unsigned value;             /* 현재 값. 0 이상이어야 함 */
	struct list waiters;        /* 대기 중인 스레드들의 리스트 */
};

/* 세마포어 관련 함수 선언 */
void sema_init (struct semaphore *, unsigned value);  /* 세마포어 초기화 */
void sema_down (struct semaphore *);                  /* 세마포어 P 연산 (잠금) */
bool sema_try_down (struct semaphore *);              /* 세마포어 P 연산 시도 (성공 여부 반환) */
void sema_up (struct semaphore *);                    /* 세마포어 V 연산 (해제) */
void sema_self_test (void);                           /* 세마포어 자체 테스트 */

/* 락 (Lock) 구조체.
   락을 보유한 스레드와 해당 락의 접근을 제어하는 세마포어를 포함 */
struct lock {
	struct thread *holder;      /* 락을 보유하고 있는 스레드 (디버깅용) */
	struct semaphore semaphore; /* 접근을 제어하는 이진 세마포어 */
};

/* 락 관련 함수 선언 */
void lock_init (struct lock *);                       /* 락 초기화 */
void lock_acquire (struct lock *);                    /* 락 획득 (필요 시 대기) */
bool lock_try_acquire (struct lock *);                /* 락 획득 시도 (성공 여부 반환) */
void lock_release (struct lock *);                    /* 락 해제 */
bool lock_held_by_current_thread (const struct lock *); /* 현재 스레드가 락을 보유 중인지 확인 */

/* 조건 변수 (Condition Variable) 구조체.
   특정 조건을 기다리는 스레드들의 리스트를 포함 */
struct condition {
	struct list waiters;        /* 조건을 기다리는 대기 중인 스레드들의 리스트 */
};

/* 조건 변수 관련 함수 선언 */
void cond_init (struct condition *);                  /* 조건 변수 초기화 */
void cond_wait (struct condition *, struct lock *);   /* 조건 대기 (락이 필요함) */
void cond_signal (struct condition *, struct lock *); /* 조건을 기다리는 한 스레드 깨우기 */
void cond_broadcast (struct condition *, struct lock *); /* 조건을 기다리는 모든 스레드 깨우기 */

/* 최적화 방벽(Optimization Barrier)
 *
 * 컴파일러가 최적화 중 방벽을 넘어서 명령을 재배치하지 않도록 함.
 * 최적화 방벽에 대한 자세한 내용은 참조 가이드의 "Optimization Barriers" 참조 */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */