#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* 스레드의 생명 주기 상태를 나타내는 열거형 */
enum thread_status {
	THREAD_RUNNING,     /* 현재 실행 중인 상태 */
	THREAD_READY,       /* 실행 준비가 되었으나 실행되지 않은 상태 */
	THREAD_BLOCKED,     /* 특정 이벤트가 발생하기를 기다리는 상태 */
	THREAD_DYING        /* 파괴될 예정인 상태 */
};

/* 스레드 식별자 타입 */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* TID 오류 값 */

/* 스레드 우선순위 관련 상수 */
#define PRI_MIN 0                       /* 최소 우선순위 */
#define PRI_DEFAULT 31                  /* 기본 우선순위 */
#define PRI_MAX 63                      /* 최대 우선순위 */

/* 커널 스레드 또는 사용자 프로세스를 나타내는 구조체
 *
 * 각 스레드 구조체는 개별적인 4KB 페이지에 저장됩니다.
 * 스레드 구조체는 페이지의 맨 아래에 위치하며, 페이지의 나머지는
 * 스레드의 커널 스택으로 예약됩니다.
 *
 * 페이지 구조:
 *      4 kB +---------------------------------+
 *           |          커널 스택               |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |        아래쪽으로 성장            |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 스택 오버플로우가 발생할 경우 `magic` 멤버가 `THREAD_MAGIC`이 아닌 값으로 변경되어 
 * 에러를 감지합니다.
 */
struct thread {
	/* thread.c에서 사용 */
	tid_t tid;                          /* 스레드 식별자 */
	enum thread_status status;          /* 스레드 상태 */
	char name[16];                      /* 스레드 이름(디버깅용) */
	int priority;                       /* 스레드 우선순위 */

	/* 깨어날 시간을 나타내는 wakeup_ticks */
	int64_t wakeup_ticks;

	/* thread.c와 synch.c에서 공유 */
	struct list_elem elem;              /* 리스트 요소 */

	/* 우선순위 기부 (Priority Donation) 관련 필드 */
	int init_priority;                  /* 초기 우선순위 */
	struct lock *wait_on_lock;          /* 기다리고 있는 락 */
	struct list donations;              /* 기부된 우선순위 리스트 */
	struct list_elem donation_elem;     /* 기부 리스트 요소 */

#ifdef USERPROG
	/* userprog/process.c에서 사용 */
	uint64_t *pml4;                     /* 4단계 페이지 맵 */
#endif
#ifdef VM
	/* 스레드가 소유하는 전체 가상 메모리 테이블 */
	struct supplemental_page_table spt;
#endif

	/* thread.c에서 사용 */
	struct intr_frame tf;               /* 스레드 전환에 필요한 정보 */
	unsigned magic;                     /* 스택 오버플로우 감지용 */
};

/* 스케줄링 모드 설정:
   false일 경우 기본적으로 라운드 로빈 스케줄러 사용.
   true일 경우 다단계 피드백 큐 스케줄러 사용 */
extern bool thread_mlfqs;

/* 함수 선언 */
void thread_init (void);
void thread_start (void);
void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* 스레드가 깨어날 시간을 설정하여 sleep 상태로 만듭니다. */
void thread_sleep(int64_t ticks);

/* 두 스레드의 wakeup_ticks를 비교하여 정렬에 사용 */
bool cmp_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux);

/* 현재 시간을 기준으로 깨워야 하는 스레드들을 ready 상태로 전환 */
void thread_wakeup(int64_t global_ticks);

/* 스레드의 우선순위를 비교하여 정렬에 사용 */
bool cmp_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* 높은 우선순위의 스레드가 존재할 경우 현재 스레드를 양보하도록 만듭니다. */
void preempt_priority(void);

/* 우선순위 비교를 위한 세마포어 리스트 요소 비교 함수 */
bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

/* 기부된 우선순위 비교 함수 */
bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

/* 우선순위 기부 기능을 수행 */
void donate_priority(void);

/* 특정 락에 의해 기부된 우선순위를 제거 */
void remove_donor(struct lock *lock);

/* 기부된 우선순위를 기반으로 우선순위를 업데이트 */
void update_priority_for_donations(void);

/* 스레드 전환에 필요한 iret 명령을 수행 */
void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */