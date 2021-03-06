#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

// #define DEBUG

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* Timer clock lock */
static struct clock clock;

/* Initializes clock CLOCK. Set its hand to 0, and initialize its waiting list */
void clock_init (struct clock *clock) {
    clock->hand = 0;
    for (int i=0; i< CLOCK_SIZE ; i++) {
        list_init(&clock->waiters[i]);
    }
}

/* Function used in list_insert_ordered, returns true if a's round is less than
    b's round, and false otherwise */
bool less_time_left(const struct list_elem *a,
                        const struct list_elem *b,
                        void *aux UNUSED)
{
    int64_t a_time = list_entry(a, struct timer_elem, timer_elem)->time_to_wake;
    int64_t b_time = list_entry(b, struct timer_elem, timer_elem)->time_to_wake;
    if(a_time < b_time) return true;
    else return false;
}

/* Function to init timer elemernt */
void timer_elem_init(struct timer_elem * t, struct semaphore * sema) {
    t->sema = sema;
}
/* Add the timer_elem in the clock. The clock is implemented to support
    timing-wheel algorithm. It must be called in a interrupt disabled environment */
void clock_add (struct clock *clock, int64_t ticks, struct timer_elem * t) {
    int64_t hour = ticks % CLOCK_SIZE;
    hour = (hour + clock->hand - 1) % CLOCK_SIZE;
    int64_t this_time_to_wake = timer_ticks() + ticks;
#ifdef DEBUG
    printf("at tick %lld: Added a timer: ticks: %lld, hour: %lld, clock hand: %lld\n", timer_ticks(),this_time_to_wake,hour, clock->hand);
#endif
    t->time_to_wake = this_time_to_wake;
    struct list * this_list = &clock->waiters[hour];
    list_insert_ordered(this_list, &t->timer_elem, less_time_left, NULL);
}

/* Function called in each timer_interrupt. Checks if any timer expires and wake
    the thread accordingly. This function is called in a interrupt environment */
void clock_tick(struct clock *clock) {
    ASSERT(intr_get_level () == INTR_OFF);
    int64_t now = timer_ticks();
    struct list *this_list = &clock->waiters[clock->hand];
    if (!list_empty(this_list)) {
        // Loop through the list
        for(struct list_elem* iter = list_begin(this_list);
            iter != list_end(this_list);
            iter = list_next(iter)
            )
        {
            // Get timer element
            struct timer_elem *this_elem = list_entry(iter, struct timer_elem, timer_elem);
            if (this_elem->time_to_wake <= now) {
                #ifdef DEBUG
                    printf("at tick %lld: Removed one timer at hand= %lld.\n", timer_ticks(),clock->hand);
                #endif
                // Remove it from list and wake it up
                list_remove(&this_elem->timer_elem);
                sema_up(this_elem->sema);
            }
            else break;
        }
    }
    clock->hand ++;
    clock->hand = clock->hand % CLOCK_SIZE;
}

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. Also initializes locks. */
void
timer_init (void)
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
  clock_init(&clock);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void)
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1))
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void)
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then)
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  ASSERT (intr_get_level () == INTR_ON);
  if(ticks <= 0) return;
  // create semaphore
  struct semaphore this_sema;
  struct timer_elem this_timer;
  sema_init(&this_sema, 0);
  timer_elem_init(&this_timer, &this_sema);

  /*Disable timer interrupt. Old level is not needed because
  the interrupt must be on */
  intr_disable ();
  //  Add the semaphore into the clock
  clock_add(&clock, ticks, &this_timer);
  // Turn interrupt back on
  intr_enable ();
  // block the process
  sema_down(&this_sema);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms)
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us)
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns)
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms)
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us)
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns)
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void)
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  clock_tick(&clock);
  thread_tick ();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops)
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops)
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom)
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.

        (NUM / DENOM) s
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */
      timer_sleep (ticks);
    }
  else
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
