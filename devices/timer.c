#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]에서 확인할 수 있습니다. */
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif


/* 운영 체제가 부팅된 이후의 타이머 틱 수를 나타냅니다. */
static int64_t ticks;

/* 타이머 틱당 반복 횟수.
   timer_calibrate()에 의해 초기화됩니다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 프로그래머블 인터벌 타이머(PIT)를 TIMER_FREQ 횟수로 설정하고
   해당 인터럽트를 등록합니다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값입니다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: 카운터 0, LSB 그 다음 MSB, 모드 2, 이진수 */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* loops_per_tick을 보정하여 짧은 대기 시간을 구현할 때 사용됩니다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* timer 틱보다 작은 가장 큰 2의 제곱수를 loops_per_tick으로 초기화합니다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick을 더 세밀하게 조정합니다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* 운영 체제 부팅 이후의 타이머 틱 수를 반환합니다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN으로부터 경과한 타이머 틱 수를 반환합니다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 대략 TICKS 틱 동안 실행을 중지합니다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks();			// 현재 시각을 저장

	ASSERT(intr_get_level () == INTR_ON);
	/* 기존 코드: CPU 점유율을 낮추기 위해 thread_yield() 사용
	while (timer_elapsed (start) < ticks)
		thread_yield ();
	 */
	thread_sleep(start + ticks);			// 현재 시각 + 대기 시간 = 일어날 시각
}

/* 대략 MS 밀리초 동안 실행을 중지합니다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 대략 US 마이크로초 동안 실행을 중지합니다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 대략 NS 나노초 동안 실행을 중지합니다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계 정보를 출력합니다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}


/* 타이머 인터럽트 핸들러 함수입니다. */
/* 주기적으로 발생하는 타이머 인터럽트를 처리하는 함수로, 시스템의 시간 추적과 스레드 스케줄링의 기반이 되는 역할 */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
	ticks++;
	thread_tick ();
	//+ 추가: 타이머 틱 증가 시 thread_wakeup 함수 호출
	thread_wakeup(ticks);	// 일정 시간 동안 Sleep 상태로 기다리고 있던 스레드를 깨우기 위한 함수
							// ticks 값을 넘겨주어, 현재 시각이 지정된 wakeup_ticks 이상인 스레드가 다시 Ready 상태로 전환되어 실행 대기열에 추가
}


/* LOOPS 횟수의 반복이 타이머 틱을 넘었는지 확인하는 함수 */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 틱을 대기합니다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS 반복을 실행합니다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 수가 변경되었다면 반복 시간이 초과된 것입니다. */
	barrier ();
	return start != ticks;
}

/* LOOPS 만큼 단순 반복하여 짧은 대기 시간을 구현합니다.
   NO_INLINE 속성을 설정하여 다른 곳에서 이 함수가 다르게 인라인되지 않도록 합니다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 약 NUM/DENOM 초 동안 실행을 중지합니다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM 초를 타이머 틱으로 변환하여 하향 반올림합니다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 적어도 한 개의 전체 타이머 틱을 대기 중인 경우
		   timer_sleep()을 사용하여 다른 프로세스에 CPU를 양보합니다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면 더 정확한 서브 틱 타이밍을 위해
		   busy-wait 루프를 사용합니다. 오버플로를 방지하기 위해
		   분자와 분모를 1000으로 줄입니다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}