#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* 스레드의 `magic` 멤버의 임의 값입니다. 
   스택 오버플로우를 감지하기 위해 사용됩니다. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 임의 값입니다.
   이 값은 변경하면 안 됩니다. */
#define THREAD_BASIC 0xd42df210

/* 실행 준비가 된 스레드들의 리스트입니다. 
   실행은 되지 않았지만 준비 상태에 있는 스레드들이 포함됩니다. */
static struct list ready_list;

/* 유휴 상태에서 실행되는 idle 스레드입니다. */
static struct thread *idle_thread;

/* init.c의 main()을 실행하는 초기 스레드입니다. */
static struct thread *initial_thread;

/* 고유 스레드 ID 할당에 사용되는 락입니다. */
static struct lock tid_lock;

/* 삭제가 요청된 스레드들을 담는 리스트입니다. */
static struct list destruction_req;

/* 통계 관련 */
static long long idle_ticks;    /* 유휴 상태에서 사용된 타이머 틱 수 */
static long long kernel_ticks;  /* 커널 스레드에서 사용된 타이머 틱 수 */
static long long user_ticks;    /* 사용자 프로그램에서 사용된 타이머 틱 수 */

/* 스케줄링 관련 */
#define TIME_SLICE 4            /* 각 스레드에 할당된 타이머 틱 수 */
static unsigned thread_ticks;   /* 마지막 CPU 전환 이후 경과된 타이머 틱 수 */

/* 라운드 로빈 스케줄러 사용 여부. false가 기본이고,
   true인 경우 다단계 피드백 큐 스케줄러를 사용합니다.
   커널 명령어 옵션 "-o mlfqs"로 제어됩니다. */
bool thread_mlfqs;

/* 함수 선언 */
static void kernel_thread (thread_func *, void *aux);  // 커널 스레드용 함수
static void idle (void *aux UNUSED);  				   // idle 스레드 함수
static struct thread *next_thread_to_run (void);  	   // 다음 실행할 스레드 선택
static void init_thread (struct thread *, const char *name, int priority);  // 스레드 초기화
static void do_schedule(int status);  				   // 스케줄링 수행
static void schedule (void);  						   // 현재 스레드를 스케줄링 큐에 추가하여 전환
static tid_t allocate_tid (void);  					   // 고유 스레드 ID 할당
static void thread_launch (struct thread *th);
void do_iret (struct intr_frame *tf);


/* 유효한 스레드인지 검사하는 매크로 */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드를 반환하는 매크로 */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

/* 스레드 시작을 위한 전역 디스크립터 테이블.
   thread_init 이후에 설정되므로 임시 gdt를 설정합니다. */
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* sleep 상태에 있는 스레드들을 담는 리스트입니다. */
static struct list sleep_list;

/* Pintos와 같은 스레드 기반 커널 시스템에서 스레드 시스템을 초기화하는 함수입니다.
이 함수는 현재 실행 중인 코드를 하나의 스레드로 설정하고,
스레드 시스템의 여러 전역 구조체들을 초기화하여 시스템의 기본적인 스레드 동작이 준비될 수 있도록 설정 */
void thread_init (void)
{
	ASSERT (intr_get_level () == INTR_OFF);

	// gdt를 임시로 로드하여 커널 사용 준비
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	// 전역 스레드 컨텍스트 초기화
	lock_init (&tid_lock);		 // 스레드 ID를 할당할 때 사용하는 tid_lock을 초기화
	list_init (&ready_list);	 // 준비 상태에 있는 스레드를 관리하는 ready_list를 초기화
	list_init (&destruction_req);// 제거 대기 중인 스레드를 관리하는 destruction_req 리스트를 초기화
	list_init (&sleep_list);  	 // sleep_list 초기화

	// 실행 중인 스레드 초기화
	initial_thread = running_thread ();     // 현재 실행 중인 스레드를 initial_thread에 저장 -> 이 스레드가 시스템의 첫 번째 스레드가 됩니다.
	init_thread (initial_thread, "main", PRI_DEFAULT); // initial_thread 스레드를 기본 설정으로 초기화
	initial_thread->status = THREAD_RUNNING;// 현재 스레드의 상태를 THREAD_RUNNING으로 설정-> 이 스레드가 실행 중임을 나타냅니다.
	initial_thread->tid = allocate_tid ();  // 현재 스레드에 고유 스레드 ID(tid)를 할당합니다.
}


/* 스레드 시스템을 시작하는 초기화 함수로, idle 스레드를 생성하고 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작
idle 스레드는 시스템이 실행할 다른 스레드가 없을 때 CPU를 점유하는 역할 */
void thread_start (void) {
	/* idle 스레드를 생성합니다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다. */
	intr_enable ();

	/* idle_thread 초기화를 대기 */
	sema_down (&idle_started);
}


/* 타이머 인터럽트 핸들러에서 매 타이머 틱마다 호출됩니다. */
void thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계 업데이트 */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점 강제 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}


/* 스레드 통계 정보를 출력 */
void thread_print_stats (void)
{
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}


/* 새로운 커널 스레드를 생성하여 ready 큐에 추가합니다.
   초기 우선순위를 가지며, 함수를 AUX 인자와 함께 실행합니다.
   생성 실패 시 TID_ERROR 반환. */
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드 메모리 할당 */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드 초기화 */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 커널 스레드 함수 호출 */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 준비 큐에 추가 */
	thread_unblock (t);
	preempt_priority();

	return tid;
}


/* 현재 스레드를 sleep 상태로 전환합니다. 
   thread_unblock()이 호출되기 전까지 재스케줄되지 않습니다.
   이 함수는 인터럽트가 비활성화된 상태에서만 호출 가능합니다. */
void thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* BLOCKED 상태의 스레드를 READY 상태로 전환하여 실행 준비 상태로 만듭니다. */
void thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, cmp_thread_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 실행 중인 스레드의 이름을 반환합니다. */
const char * thread_name (void) {
	return thread_current ()->name;
}

/* 실행 중인 스레드를 반환합니다. */
struct thread * thread_current (void) {
	struct thread *t = running_thread ();

	/* 유효한 스레드인지 확인 */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 스레드의 TID를 반환 */
tid_t thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 종료하고, 상태를 THREAD_DYING으로 변경하여 스케줄링 */
void thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* THREAD_DYING 상태로 설정하고 스케줄링을 수행 */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* 현재 스레드가 CPU를 양보하게 만듭니다. */
void thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, cmp_thread_priority, NULL);

	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정 */
void thread_set_priority (int new_priority) {
	thread_current ()->init_priority = new_priority;
	update_priority_for_donations();
	preempt_priority();
}

/* 현재 스레드의 우선순위를 반환 */
int thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 현재 스레드를 sleep 상태로 전환하고 wakeup_ticks에 따라 정렬된 sleep_list에 추가 */
void thread_sleep(int64_t ticks) {
	struct thread *curr;
	enum intr_level old_level;
	old_level = intr_disable();  // 인터럽트 비활성

	curr = thread_current();  // 현재 스레드
	ASSERT(curr != idle_thread);  // idle 스레드는 슬립 상태로 만들지 않음

	curr->wakeup_ticks = ticks;  // 깨워야 할 시각 지정
	list_insert_ordered(&sleep_list, &curr->elem, cmp_thread_ticks, NULL);
	thread_block();  // 현재 스레드 슬립 상태로 전환

	intr_set_level(old_level);  // 인터럽트 원상 복귀
}

/* 두 스레드의 wakeup_ticks를 비교하여 정렬에 사용 */
bool cmp_thread_ticks(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, elem);
	struct thread *st_b = list_entry(b, struct thread, elem);
	return st_a->wakeup_ticks < st_b->wakeup_ticks;
}


/* 현재 시간을 기준으로 깨워야 하는 스레드들을 찾아 ready 상태로 전환 */
void thread_wakeup(int64_t current_ticks)
{
	enum intr_level old_level;
	old_level = intr_disable();  // 인터럽트 비활성화하여 안전한 상태 전환

	struct list_elem *curr_elem = list_begin(&sleep_list);
	while (curr_elem != list_end(&sleep_list)) {
		struct thread *curr_thread = list_entry(curr_elem, struct thread, elem);

		// wakeup_ticks가 현재 시간을 초과했는지 확인
		if(current_ticks >= curr_thread->wakeup_ticks) {
			curr_elem = list_remove(curr_elem);  // sleep_list에서 제거
			thread_unblock(curr_thread);  		 // ready 상태로 변경하여 실행 가능하도록 설정
			preempt_priority(); 				 // 더 높은 우선순위의 스레드가 있다면 현재 스레드 양보
		} else {
			// sleep_list는 정렬되어 있으므로 더 이상 깨울 스레드가 없으면 중지
			break;
		}
	}
	intr_set_level(old_level);  // 원래의 인터럽트 상태로 복구
}


/* 두 스레드의 우선순위를 비교하여, 준비 리스트에서 높은 우선순위의 스레드가 앞에 오도록 합니다.
   - 우선순위가 높은 스레드일수록 리스트의 앞부분에 배치되도록 true 또는 false를 반환합니다. */
bool cmp_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, elem);  // 리스트 요소 a를 스레드 포인터로 변환
	struct thread *st_b = list_entry(b, struct thread, elem);  // 리스트 요소 b를 스레드 포인터로 변환
	return st_a->priority > st_b->priority;  // a가 b보다 우선순위가 높으면 true 반환 ->  준비 리스트에서 우선순위가 높은 스레드가 앞으로 오게 됩니다.
}


/* 높은 우선순위의 스레드가 존재할 경우 현재 스레드를 양보하도록 만듭니다.
   - Idle 스레드가 아닐 때 실행되며, 준비 리스트에 더 높은 우선순위 스레드가 있다면 현재 스레드를 양보합니다. */
void preempt_priority(void)
{
    // 현재 스레드가 Idle 스레드일 경우 함수 종료
	if (thread_current() == idle_thread) return;

	// 준비 리스트가 비어 있을 경우 함수 종료
	if (list_empty(&ready_list)) return;

	// 준비 리스트의 가장 높은 우선순위 스레드와 비교하여 양보할지 결정
	struct thread *curr = thread_current();
	struct thread *ready = list_entry(list_front(&ready_list), struct thread, elem);
						   // 준비 리스트에서 가장 높은 우선순위를 가진 스레드를 가져옵니다.
	
	if (curr->priority < ready->priority)// 만약 현재 스레드의 우선순위가 준비 리스트의 가장 높은 우선순위보다 낮다면
		thread_yield();					 // thread_yield()를 호출하여 현재 스레드가 CPU를 양보하고, 준비 리스트에서 더 높은 우선순위의 스레드가 실행되도록 합니다.
}


static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	//priority donation
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&(t->donations));

}

static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}

static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}


static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}


static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}


static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}


/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}
