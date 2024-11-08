#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


/* 주어진 세마포어 구조체를 초기화하고 사용할 수 있는 상태로 설정 */
void
sema_init (struct semaphore *sema, unsigned value)	// 매개변수: 세마포어 구조체 포인터 sema, value 초기값
{
	ASSERT (sema != NULL);		// sema 포인터가 NULL이 아닌지 확인

	sema->value = value;		// sema 구조체의 value 필드에 전달받은 value 값을 설정

	list_init (&sema->waiters); // list_init 함수를 사용해 sema->waiters 리스트를 초기화
								// waiters 리스트는 세마포어를 기다리는 스레드들의 대기열
								// 초기화함으로써 현재 대기하고 있는 스레드가 없음
}


/* 세마포어의 값을 감소시키며, 자원을 사용하려는 스레드가 자원이 사용 가능해질 때까지 기다리게 합니다
자원이 부족할 때 호출한 스레드를 대기열에 추가하고 대기 상태로 전환 */
void
sema_down (struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT (sema != NULL);		// 세마포어 포인터 sema가 NULL이 아닌지 확인하여, 유효한 포인터임을 보장
	ASSERT (!intr_context ());

	old_level = intr_disable ();// 인터럽트를 비활성화하고 현재 인터럽트 레벨을 저장

	while (sema->value == 0) {  // sema->value가 0이면, 현재 세마포어의 자원이 부족한 상태 -> 현재 스레드는 대기열에 추가되고 차단

		// 우선순위에 따라 대기 중인 스레드를 삽입
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_thread_priority, NULL);
									// thread_current()->elem은 현재 스레드의 리스트 요소, cmp_thread_priority 함수는 스레드의 우선순위를 비교하는 함수
		thread_block (); // 현재 스레드를 차단(blocked) 상태로 전환하여, 스케줄러가 실행 중인 스레드를 다른 스레드로 전환
	}
	sema->value--;				// 세마포어 값을 감소시켜, 자원을 하나 사용하고 있음을 나타냅니다.
	intr_set_level (old_level); // 기존의 인터럽트 레벨로 복원하여 인터럽트를 다시 활성화
}


/* 세마포어의 값을 조건부로 감소시키는 함수입니다.
자원이 사용 가능한 상태일 때만 세마포어 값을 감소시키며, 자원이 부족한 경우에는 차단 없이 즉시 실패를 반환
 세마포어의 P 연산과 비슷하지만, 차단 없이 시도만 해본다는 점에서 차이가 있습니다. */
bool										// 세마포어를 성공적으로 획득하면 true, 그렇지 않으면 false를 반환
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();		// 인터럽트를 비활성화하고 현재 인터럽트 레벨을 old_level에 저장

	if (sema->value > 0) {				// sema->value가 0보다 큰지 확인 -> 즉, 자원이 하나 이상 남아있는지 확인하는 조건문
		sema->value--;					// 세마포어 값이 0보다 크면 값을 감소
		success = true;					// success를 true로 설정 -> 자원을 성공적으로 획득한 경우
	} else {
		success = false;				// 세마포어 값이 0이면, success를 false로 설정 -> 자원이 부족하여 세마포어 획득에 실패
	}
	intr_set_level (old_level);			// 기존의 인터럽트 레벨로 복원하여, 인터럽트를 다시 활성화

	return success;
}

/* 세마포어의 값을 증가시키고, 자원을 기다리던 스레드 중 우선순위가 가장 높은 스레드를 깨워 실행 가능한 상태로 전환
세마포어의 V 연산으로, 자원을 해제할 때 사용 */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();			// 인터럽트를 비활성화하고 현재 인터럽트 레벨을 저장

	if (!list_empty (&sema->waiters)) {		// sema->waiters 리스트가 비어 있지 않은지 확인하여, 대기 중인 스레드가 있는지 검사
											// 대기 중인 스레드가 있으면 우선순위가 가장 높은 스레드를 선택하여 실행 상태로 전환
		list_sort(&sema->waiters, cmp_thread_priority, NULL);
		// waiters 리스트를 cmp_thread_priority 함수를 사용해 우선순위 순서로 정렬 -> 가장 높은 우선순위의 스레드가 리스트의 맨 앞에 위치

		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
		// 정렬된 waiters 리스트의 맨 앞에서 스레드를 꺼내 thread_unblock을 호출하여 실행 가능한 상태로 만듭니다. -> 우선순위가 가장 높은 스레드가 깨어나 스케줄링에 참여
	}
	sema->value++;		// 세마포어 값을 증가시켜 자원이 해제되었음을 나타냅니다. -> 자원이 하나 추가되었으므로, 자원을 기다리던 다른 스레드가 접근할 수 있는 상태가 됩니다.
	preempt_priority(); // 스케줄링 우선순위를 재조정하는 함수 -> 현재 실행 중인 스레드의 우선순위보다 높은 스레드가 있으면 문맥 전환을 유도

	intr_set_level (old_level); // 이전의 인터럽트 레벨로 복원하여 인터럽트를 다시 활성화
}


/* 세마포어 자체 테스트를 위한 헬퍼 함수 */
static void sema_test_helper (void *sema_);


/* 세마포어에 대한 자체 테스트를 수행.
   두 스레드 간에 "ping-pong"을 만들며 세마포어 기능을 확인 */
void
sema_self_test (void) {
	struct semaphore sema[2];			// 두 개의 세마포어 배열 sema를 선언합니다. 이 배열은 두 세마포어를 관리하는 데 사용
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);			// sema[0]과 sema[1] 두 세마포어를 각각 초기화하고, 초기 값은 0으로 설정
	sema_init (&sema[1], 0);			// 초기 값이 0이므로, 이 세마포어는 대기 상태로 시작 -> 세마포어의 값이 증가하기 전까지는 획득할 수 없는 상태
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++) {
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}


/* 세마포어 테스트에서 사용되는 스레드 함수 */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++) {
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}


/* lock 객체를 초기화하는 함수로, 이 객체를 사용할 수 있는 상태로 설정
lock은 세마포어를 이용해 구현되며, 이를 통해 스레드들이 자원을 안전하게 관리
   LOCK은 한 번에 하나의 스레드만이 보유할 수 있습니다. */
void
lock_init (struct lock *lock)
{
	ASSERT (lock != NULL);

	lock->holder = NULL;			// holder는 현재 lock을 소유하고 있는 스레드를 가리키는 포인터
	sema_init (&lock->semaphore, 1);// 세마포어(semaphore)를 초기화하여 lock의 접근 제어를 담당
									// 세마포어의 초기값을 1로 설정하여, 처음에는 lock이 사용 가능하다는 의미를 부여
}


/* 스레드가 lock을 획득할 때 사용하는 함수로,
만약 다른 스레드가 lock을 이미 보유하고 있는 경우 우선순위 기부(Priority Donation)를 수행하여 우선순위 역전을 방지.
sema_down을 사용해 lock의 접근을 제어하며, lock을 획득한 후에는 lock의 소유자를 현재 스레드로 설정 */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *curr = thread_current();				// 현재 실행 중인 스레드를 curr 변수에 저장
	
	// LOCK을 이미 다른 스레드가 보유하고 있는 경우 --> 우선순위 기부(Priority Donation)를 수행
	if (lock->holder != NULL) {
		curr->wait_on_lock = lock;			// 현재 스레드의 wait_on_lock 필드를 lock으로 설정 -> lock을 기다리고 있음을 표시
		list_insert_ordered(&lock->holder->donations, &curr->donation_elem, cmp_donation_priority, NULL);
		donate_priority();					// 선순위 기부를 수행하여 lock을 보유하고 있는 스레드의 우선순위를 대기 중인 스레드의 우선순위로 일시적으로 상승
	}

	sema_down (&lock->semaphore);  // LOCK 획득
	curr->wait_on_lock = NULL;     // LOCK 획득 후 대기 상태 해제
	lock->holder = thread_current (); // lock의 holder 필드를 현재 스레드로 설정하여, 이 lock이 현재 스레드에 의해 소유되고 있음을 나타냅니다.
}


/* lock을 비차단(non-blocking) 방식으로 획득하려고 시도하는 함수
lock을 즉시 획득할 수 있으면 성공하고, 그렇지 않으면 차단 없이 false를 반환
lock_acquire와 달리, 대기 상태에 들어가지 않고 즉시 결과를 반환한다는 점에서 차이가 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);		// 세마포어의 try_down 연산을 호출하여 lock을 비차단 방식으로 획득하려고 시도
	if (success)
		lock->holder = thread_current ();			// lock의 holder 필드를 현재 스레드로 설정
	return success;
}


/* lock을 해제하고, 우선순위 기부(Priority Donation)로 인해 영향을 받은 우선순위를 정리하는 함수
현재 스레드가 보유하고 있는 lock을 해제하고, 대기 중인 다른 스레드가 이 lock을 획득할 수 있도록 합니다.
또한 우선순위 기부로 인해 변경된 우선순위를 되돌려 놓습니다. */
void
lock_release (struct lock *lock)
{
	ASSERT (lock != NULL);							// 유효한 lock 객체가 사용되고 있는지 보장
	ASSERT (lock_held_by_current_thread (lock));	// lock을 소유하지 않은 스레드가 해제하려는 시도를 방지

	remove_donor(lock);                	// LOCK을 보유한 기부자 제거
	update_priority_for_donations();    // 기부 목록에 따라 우선순위 업데이트

	lock->holder = NULL;				// lock의 소유자를 NULL로 설정하여 현재 lock이 더 이상 소유되지 않았음을 나타냅니다.
	sema_up (&lock->semaphore);         // LOCK 해제
}


/* 현재 스레드가 특정 lock을 소유하고 있는지를 확인하는 함수 */
bool
lock_held_by_current_thread (const struct lock *lock)
{
	ASSERT (lock != NULL);

	return lock->holder == thread_current (); // lock의 holder 필드가 현재 스레드(thread_current())와 같은지 비교
}


/* 세마포어 요소 구조체 정의 */
struct semaphore_elem
{
	struct list_elem elem;              /* 리스트 요소 */
	struct semaphore semaphore;         /* 세마포어 */
};


/* 조건 변수를 초기화.
조건 변수는 특정 조건을 대기하는 스레드 간의 신호 전달을 돕는 역할
이를 사용해 여러 스레드 간의 동기화를 구현 */
void
cond_init (struct condition *cond)
{
	ASSERT (cond != NULL);

	list_init (&cond->waiters);	// 조건 변수의 waiters 리스트를 초기화
								// waiters 리스트는 조건 변수를 기다리고 있는 스레드들이 들어갈 대기열을 의미
}


/* 조건 변수를 이용해 스레드가 특정 조건이 충족될 때까지 대기하도록 하는 함수
함수는 조건 변수를 기다리는 스레드를 대기열에 추가하고, 대기하는 동안 lock을 해제하여 다른 스레드가 lock을 사용할 수 있도록 합니다.
조건이 충족되면 다시 lock을 획득하여 이후 작업을 진행할 수 있게 합니다.*/
void
cond_wait (struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);						// 유효한 조건 변수 객체를 대상으로 동작하는지 보장
	ASSERT (lock != NULL);						// 유효한 lock 객체가 사용되고 있는지 보장
	ASSERT (!intr_context ());					// 인터럽트 컨텍스트에서 호출되지 않도록 보장
	ASSERT (lock_held_by_current_thread (lock));// 현재 스레드가 lock을 소유하고 있는지 확인

	sema_init (&waiter.semaphore, 0);	// waiter의 semaphore 필드를 0으로 초기화 -> 스레드가 조건 변수를 기다리는 동안 sema_down이 호출되면 바로 대기 상태로 들어갈 수 있도록 설정
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sema_priority, NULL);
	// 조건 변수의 waiters 리스트에 waiter를 우선순위에 따라 삽입, cmp_sema_priority 함수를 사용하여 우선순위가 높은 스레드가 앞에 위치하도록 정렬하여, 우선순위 역전 문제가 발생하지 않도록 한다.

	lock_release (lock);	// 스레드가 wait 상태에 들어가기 전, lock을 해제 -> 다른 스레드가 lock을 사용할 수 있도록 하여 자원의 접근을 허용
	sema_down (&waiter.semaphore); // sema_down을 호출하여 이 스레드가 대기 상태로 들어가도록 합니다.
								   // 다른 스레드가 cond_signal 또는 cond_broadcast를 호출해 이 세마포어를 up할 때까지 대기
	lock_acquire (lock);
	// 조건이 충족되어 대기에서 깨어난 후 lock을 다시 획득
}


/* 조건 변수를 기다리고 있는 스레드 중 하나를 깨워, 대기 상태에서 빠져나와 실행을 재개하도록 합니다.
특정 조건이 충족되었을 때 대기 중인 스레드를 선택적으로 깨우는 역할 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);			// 유효한 조건 변수 객체가 사용되고 있는지 보장
	ASSERT (lock != NULL);			// 유효한 lock 객체가 사용되고 있는지 보장
	ASSERT (!intr_context ());		// 인터럽트 컨텍스트에서 호출되지 않도록 보장
	ASSERT (lock_held_by_current_thread (lock));// 현재 스레드가 lock을 보유하고 있는지 확인

	if (!list_empty (&cond->waiters)) {						  // 조건 변수의 대기열(waiters)이 비어있지 않은 경우에만, 대기 중인 스레드를 깨우는 동작을 수행
		list_sort(&cond->waiters, cmp_sema_priority, NULL);	  // waiters 리스트를 cmp_sema_priority 함수를 사용해 우선순위에 따라 정렬
		sema_up (&list_entry (list_pop_front (&cond->waiters),// truct semaphore_elem 타입으로 변환
					struct semaphore_elem, elem)->semaphore); // -->> 해당 스레드의 세마포어(semaphore)를 sema_up으로 증가시켜 깨어나게 합니다.
	}
}


/* 조건 변수를 신호하여 대기 중인 모든 스레드를 깨웁니다.
대기열에 있는 모든 스레드에게 조건이 충족되었음을 알립니다.
즉, 조건 변수를 기다리는 스레드가 여러 개일 때, 모든 스레드를 한꺼번에 깨워 실행할 수 있도록 합니다.*/
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters)) // 조건 변수의 waiters 리스트가 비어 있지 않은 동안 반복하여 모든 대기 중인 스레드를 깨웁니다.
		cond_signal (cond, lock);		 // cond_signal 함수를 호출하여 waiters 리스트의 가장 높은 우선순위 스레드를 깨웁니다.
}


/* 두 semaphore_elem의 우선순위를 비교하는 함수로, 조건 변수 대기열에서 우선순위가 높은 순서대로 정렬하기 위해 사용
cond_signal이나 cond_wait 함수에서 조건 변수의 대기열을 우선순위에 따라 정렬 */
bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem); // 각각의 list_elem 포인터 a와 b를 semaphore_elem 구조체 포인터로 변환
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem); // list_entry 매크로를 사용하여 a와 b가 가리키는 리스트 요소를 semaphore_elem 구조체로 해석

	struct list *waiters_a = &(sema_a->semaphore.waiters); // 각 semaphore_elem의 semaphore 필드 안에 있는 waiters 리스트를 waiters_a와 waiters_b에 저장
	struct list *waiters_b = &(sema_b->semaphore.waiters); // 이 리스트는 세마포어를 기다리는 스레드들이 들어가 있는 대기열

	struct thread *root_a = list_entry(list_begin(waiters_a), struct thread, elem); // 각각의 waiters 리스트의 첫 번째 스레드를 가져옵니다.
	struct thread *root_b = list_entry(list_begin(waiters_b), struct thread, elem); // list_begin으로 리스트의 첫 번째 요소를 가져오고, 이를 struct thread 타입으로 변환하여 우선순위에 접근
																					// root_a와 root_b는 각각 sema_a와 sema_b의 대기열에서 가장 높은 우선순위의 스레드
	return root_a->priority > root_b->priority;
	// root_a와 root_b는 각각 sema_a와 sema_b의 대기열에서 가장 높은 우선순위의 스레드
}


/* 우선순위 기부(Priority Donation) 리스트에서 스레드 간의 우선순위를 비교하는 함수
donation_elem 리스트 요소에 대한 우선순위를 비교하여, 우선순위가 높은 스레드가 리스트에서 앞쪽에 위치하도록 정렬하는 데 사용 */
bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *st_a = list_entry(a, struct thread, donation_elem); // 각각의 list_elem 포인터 a와 b를 struct thread 포인터로 변환
	struct thread *st_b = list_entry(b, struct thread, donation_elem); // donation_elem 필드가 포함된 리스트 요소를 struct thread 타입으로 해석하여 st_a와 st_b에 저장
	// st_a와 st_b는 각각 a와 b에 해당하는 스레드를 가리킵니다.

	return st_a->priority > st_b->priority; // 이 비교 함수는 리스트에서 우선순위가 높은 스레드가 앞에 오도록 정렬할 때 사용
}


/* 우선순위 기부(Priority Donation)를 수행하여, 대기 중인 스레드의 높은 우선순위가 lock을 보유한 스레드에게 전달되도록 합니다.
우선순위 역전 문제를 해결하기 위해 사용되며, lock을 기다리는 여러 스레드가 있을 때 lock을 보유한 스레드가 높은 우선순위로 실행되도록 합니다.
또한 우선순위 기부의 깊이 제한을 설정하여, 무한히 깊어지지 않도록 방지합니다. */
void donate_priority(void)
{
	struct thread *curr = thread_current(); // 현재 스레드를 curr에 저장합니다. 이 스레드는 현재 lock을 기다리고 있는 스레드
	struct thread *holder;

	int priority = curr->priority;			// 현재 스레드의 우선순위를 priority 변수에 저장 -->> 이 우선순위가 lock을 소유한 스레드들에게 기부

	for(int i = 0; i < 8; i++) { // 우선순위 상속 깊이 설정
		if (curr->wait_on_lock == NULL)		// 현재 스레드가 더 이상 대기 중이 아니라는 의미 -> 추가적인 기부가 필요하지 않습니다.
			return;
		holder = curr->wait_on_lock->holder;// 현재 스레드가 기다리고 있는 lock의 소유자를 holder로 설정 -> holder는 현재 스레드가 기다리고 있는 lock을 소유하고 있는 스레드
		holder->priority = priority;		// holder의 priority를 현재 스레드(curr)의 우선순위로 설정하여 우선순위를 기부
		curr = holder;						// curr를 holder로 갱신하여 우선순위 기부의 다음 단계로 이동
	}
}


/* 우선순위 기부에서 특정 lock에 대해 대기 중인 기부 항목을 삭제하는 함수
lock이 해제될 때 해당 lock을 기다리며 기부한 스레드의 기부 항목을 현재 스레드의 기부 목록(donations)에서 제거하여, 불필요한 기부 항목이 남아 있지 않도록 합니다. */
void remove_donor(struct lock *lock)
{
	struct list *donations = &(thread_current()->donations); // 현재 스레드의 donations 리스트를 가리키는 포인터 donations를 선언
	struct list_elem *donor_elem;
	struct thread *donor_thread;

	if (list_empty(donations))			// donations 리스트가 비어 있다면, 제거할 기부 항목이 없는 것이므로 함수를 바로 종료
		return;

	donor_elem = list_front(donations); // donations 리스트의 첫 번째 요소를 가져와 donor_elem에 저장
										// donor_elem은 기부자 리스트에서 현재 검사할 기부 항목을 가리키는 포인터

	while (1)	// 기부자 리스트의 모든 요소를 순회하는 while 루프
	{
		donor_thread = list_entry(donor_elem, struct thread, donation_elem);
		// 현재 기부 항목 donor_elem을 struct thread 타입으로 변환하여 donor_thread에 저장
		// donor_thread는 현재 기부 항목에 해당하는 스레드로, 이 스레드가 특정 lock을 기다리고 있는지 확인

		if (donor_thread->wait_on_lock == lock)			// donor_thread의 wait_on_lock이 lock과 일치하는지 검사
		{
			list_remove(&donor_thread->donation_elem);  // donor_thread의 기부 항목(donation_elem)을 donations 리스트에서 제거
		}

		donor_elem = list_next(donor_elem);
		if (donor_elem == list_end(donations))			// donor_elem이 donations 리스트의 끝에 도달하면 순회를 종료
			return;
	}
}


/* 현재 스레드의 우선순위를 기부받은 우선순위 정보에 따라 업데이트하는 함수
기부받은 우선순위가 있으면, 현재 스레드의 우선순위를 가장 높은 기부 우선순위로 설정하고,
기부받은 우선순위가 없으면 초기 우선순위로 되돌립니다. */
void update_priority_for_donations(void)
{
	struct thread *curr = thread_current();					// 현재 스레드를 curr에 저장합니다. 이 스레드의 우선순위를 업데이트할 대상
	struct list *donations = &(thread_current()->donations);// 현재 스레드의 donations 리스트 포인터를 donations에 저장
															// 이 리스트에는 다른 스레드들이 현재 스레드에게 기부한 우선순위 정보가 포함
	struct thread *donations_root;

	if (list_empty(donations))					// donations 리스트가 비어 있다면,
	{
		curr->priority = curr->init_priority;	// 현재 스레드가 기부받은 우선순위가 없는 상태이므로, 우선순위를 초기 우선순위(init_priority)로 설정
		return;
	}

	donations_root = list_entry(list_front(donations), struct thread, donation_elem);
							// donations 리스트에서 가장 높은 우선순위를 가진 기부 항목을 donations_root에 저장
							// list_front로 리스트의 첫 번째 요소를 가져오고, list_entry를 통해 struct thread 타입으로 변환하여 donations_root에 저장
							// 기부받은 우선순위 리스트가 우선순위 순서로 정렬되어 있기 때문에, 가장 높은 우선순위 항목을 list_front로 가져올 수 있습니다.

	curr->priority = donations_root->priority;  // 현재 스레드의 우선순위를 donations_root의 우선순위로 설정 -> 가장 높은 기부 우선순위를 반영
}