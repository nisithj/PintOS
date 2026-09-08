# Pintos Threads Project - A Walkthrough Guide

**Scope:** Project 1 - Threads (Mission 1: Alarm Clock, Mission 2: Priority Scheduling & Donation, Mission 3: MLFQS)
**Audience:** Anyone picking up this assignment for the first time

Like the [Interactive Shell guide](./project0-Interactive-shell.md), this walks through *why* the code looks the way it does, not just *what* to paste. Each mission builds on the last, so read them in order even if you're only trying to understand one.

--

## Mission 1 - Alarm Clock

### What's happening now

Imagine a queue of runnable threads (threads that are ready to run) and a CPU that processes one thread at a time.

Now imagine the thread currently running on the CPU wants to sleep for a certain amount of time (don't confuse this with waiting for I/O or waiting for a lock, it's neither). To do this, the thread calls the `timer_sleep(ticks)` function in ~/pintos/src/devices/timer.c, passing in the number of ticks it wants to sleep for.

The problem is in how `timer_sleep()` is currently implemented.

**Current code:**

```c
void
timer_sleep (int64_t ticks)
{
  int64_t start = timer_ticks ();

  ASSERT (intr_get_level () == INTR_ON); //This is error detection code line, no need to understand for this mission
  while (timer_elapsed (start) < ticks)
    thread_yield ();
}
```

**The functions involved:**

- `timer_ticks()` - returns the current tick value. (If you don't know what a tick is, look it up - it'll help with everything that follows.)
- `timer_elapsed(start)` - returns how many ticks have passed since the given `start` tick.
- `thread_yield()` - the key function to understand and the source of the problem. When a thread calls this, it takes itself off the CPU and puts itself at the back of the ready queue.


### The problem

When a thread wants to sleep, it calls `timer_sleep()`. As shown above, this runs a `while` loop that keeps checking whether the sleep duration is over and on every iteration where it isn't, it calls `thread_yield()`.

Calling `thread_yield()` sends the thread to the back of the ready queue but the thread is still **runnable**. That's the core of the problem and it wastes CPU time in two distinct ways:

1. **While it sits in the ready queue.** The scheduler still has to consider this thread as a candidate every time it decides what to thread to run next - the time and attention spent on a thread that is waiting is wasteful.
2. **When it actually gets picked to run again.** Under round robin especially, this can happen almost immediately. The thread doesn't necessarily wait behind everyone else it can be picked to run again. When it's again choosen to run, it re-checks the clock, finds it still has to wait and calls `thread_yield()` again - which means the dispatcher has to do another context switch, spedning time on context switching a waiting thread is wasteful.

So the thread is technically "waiting" but it's doing so by consuming real CPU cycles and scheduler time over and over. That's why this pattern is called **busy waiting**.

(If you're not familiar with the scheduler and dispatcher, look those up too, understanding what each one actually does will make this a lot clearer.)




### The fix

The plan has three parts:

1. Record exactly when the thread needs to wake up.
2. Create a `sleep_list` to hold sleeping threads.
3. Coding the full workflow from a thread going to sleep and later being woken up and added back to `ready_list`.

---

**1. Record exactly when the thread needs to wake up.**

We add a new field to `struct thread`, in `threads/thread.h`, to store the tick at which this thread should wake up.

```c
/* Add to threads/thread.h, inside struct thread */
int64_t wakeup_tick;
```

This is set to `current_tick + ticks_to_sleep` at the moment the thread goes to sleep, and later compared against the live tick count to decide when it's time to wake it back up.

---

**2. Create the `sleep_list` to hold sleeping threads, in `devices/timer.c`.**

Before writing this, it helps to understand a couple of basics about Pintos's list implementation.

A Pintos `list` is a generic doubly linked list. Its nodes are `struct list_elem` - just two pointers, nothing else:

```c
/* Alredy present in lib/kernel/list.h */
struct list_elem {
  struct list_elem *prev;
  struct list_elem *next;
};
```

To actually store *a thread* in a list, `struct thread` has a field to store the list node:

```c
/* Already present in struct thread in threads/thread.h */
struct list_elem elem;
```


With that in place, `sleep_list` is declared the same way `ready_list` is declared in `thread.c` - a `static` list, private to the file that owns this feature:

```c
/* Add to devices/timer.c */
static struct list sleep_list; // Declearing the sleep_list
```

```c
/* Add Inside timer_init() */
list_init(&sleep_list); // Initalizing the sleep list
```
      
**3. Coding the full workflow from a thread going to sleep and later being woken up and added back to `ready_list`**

There are a few functions we need to know before implementing this:

**1. `timer_sleep()`**

We have discussed this function in the "What's happening now" section above.

**2. `thread_block()`**

```c
/* This is present in threads/thread.c */
void
thread_block (void)
{
  ASSERT (!intr_context ()); // Checking if 'thread_block()' function is called inside the 'timer_interrupt()' function
  ASSERT (intr_get_level () == INTR_OFF); // Checking if the interrupter is off

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}
```

- This function writes the status of the thread to 'Blocked' and takes the thread out of the CPU (the `schedule()` function takes the thread out of the CPU - we refer to it as "putting the thread to sleep").
- The interrupter needs to be turned off to call this function. The reason for it is, if `timer_interrupt()` function gets called in between the code lines `thread_current()->status = THREAD_BLOCKED;` and `schedule();`, it can cause problems since it shows a blocked thread is running on the CPU.

**3. `timer_interrupt()`**

```c
/* This is present in devices/timer.c */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++; // Increasing the global tick counter
  thread_tick (); // A function in threads/thread.c
}
```

- This function is run automatically and independent of what's happening anywhere else.
- This function is run as a reaction to the hardware interrupt signal (this function is triggered by the hardware timer). The hardware timer ticks 100 times per second as set by default in Pintos.

**4. `thread_unblock()`**

```c
/* This is present in threads/thread.c */
void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;

  ASSERT (is_thread (t)); // Checking if it's a thread

  old_level = intr_disable (); // Disable the interrupt and save the previous status of the interrupt in the 'old_level' variable
  ASSERT (t->status == THREAD_BLOCKED); // Checking if it's blocked
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}
```

- This is the only function that can unblock a thread that has been blocked by the `thread_block()` function.
- This function puts the thread back into the `ready_list` and writes the thread status to `THREAD_READY`.
- The interrupt needs to be turned off. The reason for this is this function can be called from inside `timer_interrupt()` - which is exactly how our wake up logic will use it and as we know, at every interrupt `timer_interrupt()` is called - so if the function is called while `thread_unblock()` is running and the thread is being moved to `ready_list`, it will cause a corruption in data. To avoid this, we turn off the interrupt before moving the thread to `ready_list` and turn the interrupt back on at the end of the function.



### The implementation, piece by piece

**1. Comparator function**

```c
/* Comparator function - This needs to match the following structure */
/* bool list_less_func(const struct list_elem *a, const struct list_elem *b, void *aux); */

bool comparator(const struct list_elem *a, const struct list_elem *b, void *aux)
{
  struct thread *a_thread = list_entry(a, struct thread, elem);
  struct thread *b_thread = list_entry(b, struct thread, elem);
  return a_thread->wakeup_tick < b_thread->wakeup_tick;
}
```

- We need to implement a comparator function before the `timer_sleep()` function.
- The reason for this implementation is the `list_insert_ordered()` function needs a comparator passed as an argument (you will understand it better when we are implementing `list_insert_ordered()` in the `timer_sleep()` function below).
- The comparator needs to follow the structure below:

```c
bool list_less_func(const struct list_elem *a, const struct list_elem *b, void *aux)
{
  // Your code
}
```

**2. `timer_sleep()` function**

```c
void
timer_sleep (int64_t ticks)
{
  /* Get the current thread */
  struct thread *t = thread_current();
  /* Add the wakeup tick to the thread */
  t->wakeup_tick = timer_ticks() + ticks;

  /* Disabling interrupt */
  enum intr_level old_level = intr_disable();

  /* Add the thread to the sleep_list in a sorted way */
  list_insert_ordered(&sleep_list, &t->elem, comparator, NULL);

  /* Call block on thread */
  thread_block();

  /* Enabling interrupt */
  intr_set_level(old_level); // This line of code only runs after the thread is unblocked
}
```

- The `list_insert_ordered()` function is used to add threads to the `sleep_list` in the ordered manner we specify. We pass a custom comparator as an argument for that.
- The reason we maintain an order is that when we are checking the list to see which threads to wake up, we don't need to check the whole list. It reduces time complexity.

**3. `timer_interrupt()` function**

```c
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++; // Already existing

  /* The new implementation to wake up the threads and remove them from the sleep_list */
  int64_t current_tick = timer_ticks();

  while (!list_empty(&sleep_list) && list_entry(list_front(&sleep_list), struct thread, elem)->wakeup_tick <= current_tick)
  {
    struct list_elem *current = list_front(&sleep_list);
    struct thread *current_thread = list_entry(current, struct thread, elem);
    list_remove(current);
    thread_unblock(current_thread);
  }

  thread_tick(); // Already existing
}
```

- The following functions help with the above implementation:
  1. `list_empty(&sleep_list)` - returns true if the list is empty
  2. `list_front(&sleep_list)` - returns the pointer to the first node of the list
  3. `list_entry(list_front(&sleep_list), struct thread, elem)` - returns the thread that the node belongs to (check the implementation in `lib/kernel/list.h`)
  4. `list_remove(current)` - removes the node from the list

- We only check the first node of the `sleep_list` and remove it if the time is up. This is the advantage we gained from the sorted list implementation.
- 

### Testing Mission 1 - Alarm Clock

Pintos doesn't grade missions separately - the full Threads project is checked together with one `make check` run at the end, covering alarm clock, priority donation and MLFQS tests all at once. But while you're still building Mission 1 (before Mission 2 and 3 exist), you can run just its tests:

1. **Build the kernel:**
```bash
   cd src/threads
   make
```

2. **Move into the build directory** - all test commands run from here, not from `threads/`:
```bash
   cd build
```

3. **Run one alarm-clock test at a time**, by asking `make` for its `.result` file:
```bash
   make tests/threads/alarm-single.result
   make tests/threads/alarm-multiple.result
   make tests/threads/alarm-simultaneous.result
   make tests/threads/alarm-zero.result
   make tests/threads/alarm-negative.result
```
   These five cover all of Mission 1.

4. **Read the result.** Each command prints `pass` or `fail` directly. For more detail on a failure, open the matching `.output` file inside `build/` (e.g. `tests/threads/alarm-single.output`) - it shows the full kernel boot log and test trace for that run.

5. **Force a re-run**, if `make` claims a result is already up to date but you've since changed your code:
```bash
   rm tests/threads/alarm-single.output
   make tests/threads/alarm-single.result
```





## Mission 2 - Priority Scheduling

### What's happening now

Every thread already carries a `priority` field. It gets set the moment the thread is created and it lives right there in `struct thread`:

```c
/* Already present in threads/thread.h */
int priority;

#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63
```

The field exists but that doesn't mean anything actually uses it. The real question is whether the scheduler looks at this number when it's deciding who runs next.

In the unmodified starter code threads get added to `ready_list` with a plain unordered append. You can see this inside `thread_unblock()`:

```c
/* Original code, in threads/thread.c */
list_push_back(&ready_list, &t->elem);
```

And the function that actually picks the next thread to run, `next_thread_to_run()`:

```c
static struct thread *
next_thread_to_run(void)
{
  if (list_empty(&ready_list))
    return idle_thread;
  else
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
}
```

This just pops whichever thread happens to sit at the **front** of the list. Nothing about priority enters into it.

### The problem

Since insertion is plain FIFO (`list_push_back`) and selection is "pop the front," the actual behavior we get is: whichever thread has been waiting the longest runs next. Completely regardless of what its `priority` value is. A thread with `priority = 63` gets no special treatment over a thread sitting at `priority = 0`. The field exists on paper but nothing enforces it in practice.

### The fix

Here's the key insight that makes this fix simple: `next_thread_to_run()` doesn't need to change at all. "Pop the front" is already the correct behavior, as long as the front of the list actually represents the highest priority thread. So the fix belongs entirely on the *insertion* side. Keep `ready_list` sorted by priority and "pop the front" turns into "pop the highest priority thread" for free, with no change needed to the pop logic itself.

**1. A comparator function**, following the same `list_less_func` shape used for the sleep list back in Mission 1:

```c
/* Comparator function */
bool
priority_comparator(const struct list_elem *a, const struct list_elem *b, void *aux)
{
  struct thread *a_thread = list_entry(a, struct thread, elem);
  struct thread *b_thread = list_entry(b, struct thread, elem);
  return a_thread->priority > b_thread->priority;
}
```

Notice the direction here is the opposite of Mission 1's comparator. We want the **highest** priority sitting at the front this time, not the lowest tick value.

**2. Every place a thread gets added back to `ready_list` needs to use this comparator with `list_insert_ordered` instead of `list_push_back`.** There are two such places.

```c
/* In thread_unblock() */
void
thread_unblock(struct thread *t)
{
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list, &t->elem, priority_comparator, NULL);
  t->status = THREAD_READY;
  intr_set_level(old_level);
}
```

```c
/* In thread_yield() */
void
thread_yield(void)
{
  struct thread *cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread)
    list_insert_ordered(&ready_list, &cur->elem, priority_comparator, NULL);
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}
```

With these two changes `ready_list` stays sorted highest priority first at all times. `next_thread_to_run()`'s existing "pop the front" logic now correctly returns the highest priority ready thread every single time, and we didn't have to touch that function at all.

---

### Problem 2 - Preemption

Sorting `ready_list` only matters at the moment a scheduling decision actually happens. That's whenever `schedule()` gets called, which is the point where whoever's at the front actually gets picked. But sorting alone says nothing about *when* that decision gets made.

Here's a scenario worth walking through. A low priority thread L is currently running. Actively executing, not yielding, not blocking. Now a high priority thread H becomes ready (say it just got unblocked). Thanks to the sorting fix above H is correctly inserted at the **front** of `ready_list`. But L isn't calling `thread_yield()`. L isn't calling `thread_block()` either. Nothing is forcing `schedule()` to run at this exact moment, so L just keeps running uninterrupted until its timeslice runs out or it happens to yield or block on its own.

This directly breaks the assignment's requirement: *"When a thread is added to the ready list that has a higher priority than the currently running thread, the current thread should immediately yield the processor."* Sorting only affects who gets picked the next time a decision is made. It does nothing to force that decision to happen *immediately* when priorities change. This is what preemption means: forcing a running thread off the CPU before it voluntarily gives it up.

**Where the fix goes.** There are two functions where a thread gets added to `ready_list`, `thread_unblock()` and `thread_yield()`. The fix only needs to go in one of them.

Why only `thread_unblock()` and not `thread_yield()`? Because `thread_yield()` is called by the currently running thread itself, voluntarily giving up the CPU. It already unconditionally calls `schedule()` as part of what it does, so there's no "should I preempt?" question to even ask there. The thread calling it is already stepping aside on its own regardless of priority. The preemption question only makes sense inside `thread_unblock()`, because that's the spot where a thread *other than* the currently running one is the one being affected. Some other thread just became ready and we need to decide whether the currently running thread should step aside for it.

**Where `thread_unblock()` actually gets called from.** Two different situations matter here:

1. From normal thread code, for example a thread being newly created or a lock/semaphore waking someone up.
2. From inside `timer_interrupt()`. This is exactly where your Mission 1 wake up loop calls `thread_unblock()` on sleeping threads.

Calling `thread_yield()` directly from inside the timer interrupt is unsafe. `timer_interrupt()` is a hardware interrupt handler, running in a special context. `thread_yield()` internally calls `schedule()`, which does a full context switch. Just like `thread_block()` asserts `!intr_context()` for the same underlying reason, doing a context switch from inside an interrupt handler is unsafe. The handler hasn't finished its own bookkeeping yet and switching away mid-interrupt can leave the whole system in an inconsistent state.

Pintos already provides the fix for this specific case, `intr_yield_on_return()`. It doesn't switch threads immediately, it just sets a flag:

```c
void
intr_yield_on_return(void)
{
  ASSERT(intr_context());
  yield_on_return = true;
}
```

Later, once the interrupt handler has fully finished and control is about to return to normal execution, Pintos's interrupt return code checks this flag and calls `thread_yield()` at that point, once it's actually safe to do so. So `intr_yield_on_return()` works as a "tag it for later" mechanism. It defers the actual yield until the interrupt is completely done instead of trying to do it immediately while still inside the handler.

Putting it all together inside `thread_unblock()`:

```c
/* In thread_unblock(), after inserting into ready_list and setting status */
if (t->priority > thread_current()->priority)
{
  if (intr_context())
    intr_yield_on_return();
  else
    thread_yield();
}
```

This checks whether the newly unblocked thread `t` has a higher priority than whoever's currently running. If so and we're inside an interrupt handler right now (`intr_context()` is true) we tag it for a deferred yield through `intr_yield_on_return()`. Otherwise we're in normal thread code so it's safe to just call `thread_yield()` directly.

**Why `schedule()` can't be called from inside `timer_interrupt()` at all.** `timer_interrupt()` doesn't have its own separate stack. It runs directly on top of whatever thread happened to be executing when the timer fired. Its whole job is to run quickly, do its work and return control back to that exact thread, resuming right where it left off.

`schedule()` breaks this promise. If it got called from inside the interrupt handler it would switch away to a completely different thread's stack and execution, abandoning the interrupt mid flight with the original thread (and its unfinished interrupt) frozen and set aside. There's no guarantee of when, or even if, that thread gets scheduled again to properly finish returning from the interrupt.

That's why interrupt handlers make an implicit promise to run to completion quickly and return. A full context switch breaks that promise with unpredictable and unbounded consequences for the system's timing and bookkeeping. This is exactly why `thread_block()` asserts `!intr_context()` and why `thread_yield()` can't be called directly from inside `timer_interrupt()` either. It's also the whole reason `intr_yield_on_return()` exists in the first place, it lets the interrupt handler tag a yield to happen safely right after the interrupt has fully returned instead of trying to switch away mid handler.

### Problem 3 - Lowering your own priority

`thread_set_priority()` lets a thread change its own priority at any time:

```c
/* Original code, in threads/thread.c */
void
thread_set_priority(int new_priority)
{
  thread_current()->priority = new_priority;
}
```

It just overwrites the value. Nothing checks what should happen after the change. Per the assignment: *"A thread may raise or lower its own priority at any time, but lowering its priority such that it no longer has the highest priority must cause it to immediately yield the CPU."*

Think through this: a thread X is running with `priority = 50`. Another thread Y sits in `ready_list` with `priority = 40`. X calls `thread_set_priority(10)`, dropping its own priority below Y's. Nothing in the current code makes X actually step aside, it just keeps running with a new lower priority value even though Y deserves the CPU more now.

This is the same underlying idea as the Preemption fix above, compare the relevant priority against the front of `ready_list` and yield if outranked. The one real difference here is that it's not a *new* thread arriving that triggers the check, it's the *currently running* thread's own priority changing. So the comparison is between the thread's new priority and whoever currently sits at the front of `ready_list`.

One thing that's simpler here than in `thread_unblock()`: `thread_set_priority()` is only ever called by a thread acting on itself voluntarily from normal code, never from inside an interrupt handler. There's no scenario where a hardware interrupt would call this function on a thread's behalf. Because of that no `intr_context()` check is needed here, a plain unconditional `thread_yield()` is enough whenever preemption is warranted.

```c
void
thread_set_priority(int new_priority)
{
  thread_current()->priority = new_priority;

  if (!list_empty(&ready_list)) {
    if (thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
      thread_yield();
    }
  }
}
```

Why does the `!list_empty(&ready_list)` guard matter here? `struct list` always keeps sentinel `head`/`tail` nodes present even when the list is logically empty. Calling `list_front()` on an empty list still returns something, it just doesn't point to a real thread. Without this guard `list_entry()` would recover a `struct thread*` out of sentinel memory that isn't actually a valid thread, and reading `->priority` off it would be undefined behavior. Checking `list_empty()` first makes sure `list_front()` only gets called when there's an actual thread to compare against.

### Problem 4 - Semaphore waiters list is not priority ordered

Semaphores keep their own internal waiting list, separate from `ready_list`:

```c
struct semaphore {
  unsigned value;
  struct list waiters;
};
```

The original `sema_down()` inserts into this list with plain FIFO insertion:

```c
/* Original code, in threads/synch.c */
void
sema_down(struct semaphore *sema)
{
  ...
  while (sema->value == 0) {
    list_push_back(&sema->waiters, &thread_current()->elem);
    thread_block();
  }
  ...
}
```

Same underlying issue as `ready_list` before it got fixed. A high priority thread waiting on a semaphore gets no special treatment over a low priority one, whoever asked first gets woken up first regardless of priority. This directly breaks the spec: *"when threads are waiting for a lock, semaphore, or condition variable, the highest priority waiting thread should be awakened first."*

The fix swaps the plain insertion for a sorted one, using the same `priority_comparator` already built for `ready_list`:

```c
void
sema_down(struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT(sema != NULL);
  ASSERT(!intr_context());

  old_level = intr_disable();
  while (sema->value == 0) {
    list_insert_ordered(&sema->waiters, &thread_current()->elem, priority_comparator, NULL);
    thread_block();
  }
  sema->value--;
  intr_set_level(old_level);
}
```

`sema_up()` still pops with `list_pop_front()`, unsorted at pop time, and this is intentional for now. Without priority donation a thread waiting in `sema->waiters` is `THREAD_BLOCKED` and can only change its own priority through `thread_set_priority()`, which requires the thread to actually be running. A blocked thread can't run so its priority genuinely can't change while it sits here. The list can only go stale once priority donation exists (a mechanism that changes a thread's priority from the outside while it's still blocked), so the `sema_up()` re-sort-before-pop fix gets deferred until the Priority Donation section where it actually becomes necessary.

### Problem 5 - Condition variable waiters list is not priority ordered

Before reading the problem and the fix below, make sure you actually understand what a condition variable and a semaphore are and how they relate to each other in Pintos. Read the current unmodified `cond_wait()` / `cond_signal()` / `semaphore_elem` code first, on its own, until it makes sense. Only then come back and look at what's wrong with it and why the fix looks the way it does.

Condition variables don't store waiting threads directly. `cond_wait()` wraps each waiter in a private single use semaphore before pushing it onto `cond->waiters`:

```c
/* threads/synch.h */
struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
};
```

The original `cond_wait()` / `cond_signal()` insert and pop this wrapper with plain FIFO order:

```c
/* Original code, in threads/synch.c */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  if (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}
```

Same underlying issue as `ready_list` and `sema.waiters` had before them. Except here it's worse, because `semaphore_elem` doesn't expose *which thread* it belongs to at all. You can't write a priority comparator over `cond->waiters` without first knowing whose priority each wrapper actually represents.

**The fix.** Add a direct pointer back to the waiting thread, set it at creation time and switch the insertion over to sorted:

```c
struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
  struct thread *thread_waiting;   /* NEW - exposes who this wrapper belongs to */
};

bool
semaphore_comparator (const struct list_elem *a, const struct list_elem *b, void *aux)
{
  struct semaphore_elem *a_semaphore_elem = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *b_semaphore_elem = list_entry (b, struct semaphore_elem, elem);

  return a_semaphore_elem->thread_waiting->priority > b_semaphore_elem->thread_waiting->priority;
}

void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;
  sema_init (&waiter.semaphore, 0);
  waiter.thread_waiting = thread_current ();
  list_insert_ordered (&cond->waiters, &waiter.elem, semaphore_comparator, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}
```

### Problem 6 - Priority Donation

Locks are the third synchronization tool in Pintos and they're built directly on top of the semaphore, not something separate:

```c
struct lock
{
  struct thread *holder;      /* Thread holding lock, or NULL. */
  struct semaphore semaphore; /* Binary semaphore controlling access. */
};
```

A lock **is** a semaphore, with exactly one thing added on top, the `holder` pointer. `lock_init()` initializes that embedded semaphore to 1 rather than some arbitrary number. A semaphore starting at 1 can only ever sit at 0 or 1, meaning exactly one thread can be "in" at a time and everyone else blocks. This is what's called a binary semaphore and it's specifically what makes lock-like behavior possible in the first place.

A binary semaphore alone is already enough to enforce mutual exclusion, so why does a lock exist as a separate thing built on top of it? Because a semaphore has no memory of *who* is inside. Any thread can call `sema_down()` and a completely different thread can later call `sema_up()`, the semaphore itself never checks or cares who's calling. That's fine for signaling between threads but it's dangerous for protecting a critical section, where you specifically want only whoever locked it to be the one allowed to unlock it. It also matters for donation specifically, since donation needs to answer "who currently holds this, so I know who to boost?" and a bare semaphore structurally has no way to answer that question at all.

**The new fields.** We add four new fields to `struct thread`:

```c
int base_priority;                 /* undonated priority, priority reverts here once donations end */
struct lock *waiting_lock;         /* the lock THIS thread is currently blocked on, if any */
struct list donations;             /* threads currently donating priority TO this thread */
struct list_elem donation_elem;    /* this thread's own hook, for sitting inside SOMEONE ELSE's donations list */
```

`priority` (which already existed) becomes the *effective*, possibly donated value. `base_priority` is the real undonated one underneath it.

`donation_elem` needs to be a separate `list_elem` from `elem` and can't just reuse it. Think about what `elem` is already used for, it's the hook a thread uses to sit inside `ready_list` or a `sema->waiters` list, whichever list is deciding whether it's running, ready, or blocked. A `list_elem` can only belong to one list at a time since it's just a pair of prev/next pointers. A thread that's currently blocked, waiting on a lock, is at that exact moment also sitting inside `sema->waiters` through its `elem`. If it also needs to sit inside some holder's `donations` list at the same time, it needs its own separate hook to do that with, which is exactly what `donation_elem` is for.

**Recording who a thread is waiting on, and donating to the holder.** Inside `lock_acquire()`, before calling `sema_down()`, we check whether the lock is already held:

```c
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *curr = thread_current ();

  if (lock->holder != NULL)
  {
    curr->waiting_lock = lock;
    list_insert_ordered (&lock->holder->donations, &curr->donation_elem,
                          priority_comparator, NULL);

    struct thread *holder = lock->holder;
    while (holder != NULL)
    {
      if (holder->priority < curr->priority)
      {
        holder->priority = curr->priority;
        holder = (holder->waiting_lock != NULL) ? holder->waiting_lock->holder : NULL;
      }
      else
        break;
    }
  }

  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  lock->holder->waiting_lock = NULL;   /* no longer waiting on anything, we now hold it */
}
```

A few things worth being explicit about here. We check `lock->holder != NULL` rather than reaching into `lock->semaphore.value` directly, since `holder` is the field that actually exists for this purpose and checking `value` instead would leave a narrow window where `value` has already dropped to 0 but `holder` hasn't been set yet by the thread that's still in the middle of acquiring.

The `while` loop is the multi hop case. Say H is waiting on a lock held by M, and M is itself already blocked on a different lock held by L. Boosting M alone isn't enough, since L is the one actually running (or at least the one that needs to finish and get out of the way) and L never finds out it should also be boosted. The loop walks up the chain one hop at a time using each thread's own `waiting_lock` field, which we already have from the single hop case, so nothing new needs to be tracked to make this work. The value being propagated the whole way up is always the same one, `curr`'s priority, the original donor at the bottom of the chain.

And once the thread actually gets the lock, `waiting_lock` gets cleared back to `NULL` right away. If we left it pointing at the lock we just acquired, a later chain-walk from some other thread could resolve `holder->waiting_lock->holder` back onto the holder itself, since `lock->holder` is now this same thread, which risks the walk looping on a self reference instead of terminating correctly.

**Releasing a lock, and cleaning up donations.** Inside `lock_release()`:

```c
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *curr = thread_current ();

  struct list_elem *e = list_begin (&curr->donations);
  while (e != list_end (&curr->donations))
  {
    struct thread *donor = list_entry (e, struct thread, donation_elem);
    if (donor->waiting_lock == lock)
      e = list_remove (e);
    else
      e = list_next (e);
  }

  if (list_empty (&curr->donations))
    curr->priority = curr->base_priority;
  else
  {
    struct thread *top_donor = list_entry (list_front (&curr->donations),
                                            struct thread, donation_elem);
    curr->priority = (curr->base_priority > top_donor->priority)
                       ? curr->base_priority : top_donor->priority;
  }

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}
```

The `while` loop removes every donor whose `waiting_lock` points at the lock being released, not just one. This matters because more than one thread can be waiting on the exact same lock at the same time, and releasing that lock invalidates all of their donations at once, not just the first one found.

After that, priority gets recomputed as the max of `base_priority` and whoever's left at the front of `donations`. If nobody's left it falls all the way back to `base_priority`. If someone's still there but they're lower than `base_priority` itself, we still want the thread's own natural priority to win rather than getting artificially dragged down, which is exactly why this is a max rather than just always taking the front of the list.

**`thread_set_priority()` also has to change.** A thread can no longer just overwrite its effective priority directly, since that could accidentally erase an active donation it's currently benefiting from. What it can always change freely is `base_priority`. Whether that new value actually takes effect right away depends on whether it still beats the current top donor:

```c
void
thread_set_priority (int new_priority)
{
  struct thread *curr = thread_current ();

  curr->base_priority = new_priority;

  if (list_empty (&curr->donations) ||
      new_priority > list_entry (list_front (&curr->donations),
                                  struct thread, donation_elem)->priority)
  {
    curr->priority = new_priority;
  }

  if (!list_empty (&ready_list) &&
      curr->priority < list_entry (list_front (&ready_list), struct thread, elem)->priority)
  {
    thread_yield ();
  }
}
```

`base_priority` is written unconditionally every time. `priority`, the effective value, only gets overwritten if the new value would still beat the current top donor, otherwise it stays exactly where it was, still reflecting the donation that's propping it up. The preemption check at the bottom is unchanged from Problem 3, it just now reads whatever `priority` ended up being after the logic above runs.

**New fields need to be initialized, and that has to happen in `init_thread()`.** `thread_create()` is what gets called to spawn a new thread while the system is already running, and it calls `init_thread()` internally as one of its own steps, so any thread made through `thread_create()` gets its fields set up either way. The problem is `main`, the very first thread, which never goes through `thread_create()` at all. It's set up by a separate boot time function, `thread_init()`, that calls `init_thread()` directly and skips `thread_create()` entirely. So putting the new field initialization inside `thread_create()` would work for every normal thread but would silently skip `main`, leaving its fields as raw uninitialized memory instead of a real empty list. The first time anything tried to touch that field on `main`'s behalf, say `main` holds a lock and another thread tries to donate to it, it would be operating on garbage memory instead of a real list. Any new field added to `struct thread` has to be initialized inside `init_thread()`, since that's the one function guaranteed to run for every thread in the system, `main` included, with no exceptions.

```c
/* Added inside init_thread(), alongside the existing t->priority = priority; */
t->base_priority = priority;
t->waiting_lock = NULL;
list_init (&t->donations);
```

**One more piece, in `sema_up()` and `cond_signal()`.** Back in Problem 4 and Problem 5 we deliberately left `sema_up()` and `cond_signal()` popping from an unsorted position, reasoning that a blocked thread's priority couldn't change while it waits, without donation existing. Donation is exactly a mechanism that changes a blocked thread's priority from the outside, so that assumption no longer holds and both need a sort right before the pop:

```c
/* sema_up() */
if (!list_empty (&sema->waiters))
{
  list_sort (&sema->waiters, priority_comparator, NULL);
  thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
}
```

```c
/* cond_signal() */
if (!list_empty (&cond->waiters))
{
  list_sort (&cond->waiters, semaphore_comparator, NULL);
  sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
}
```

**And `next_thread_to_run()` needs the same treatment.** A thread doesn't have to be running or blocked to receive a donation, it can also be sitting in `ready_list` at the time. Say a thread got preempted while still holding a lock, and while it's sitting in `ready_list` someone else tries to acquire that lock and boosts it. Its priority just changed but its position in `ready_list` didn't move to match, so it could end up sitting in the wrong spot even though it now outranks threads ahead of it. Rather than chasing this down at every single place a priority might change, we fix it once, right where it actually matters, immediately before the pick:

```c
static struct thread *
next_thread_to_run (void)
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
  {
    list_sort (&ready_list, priority_comparator, NULL);
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
}
```

The earlier `list_insert_ordered()` calls in `thread_unblock()` and `thread_yield()` are still worth keeping even though this makes them technically redundant for correctness. Keeping the list mostly sorted on insert means this sort has less work to do each time it runs.

---

## Debugging session - build and runtime issues found

Everything above was reasoned out and written correctly on paper, but two real bugs only surfaced once the code actually ran. Neither one was catchable just by reading the code carefully, which is exactly why running `make check` matters as a step and not just a formality.

### Build error - duplicate definition of `priority_comparator`

The symptom showed up as a linker error, not a compiler error:

```
/usr/bin/x86_64-linux-gnu-ld.bfd: threads/synch.o: in function `priority_comparator':
synch.c:56: multiple definition of `priority_comparator'; thread.o:thread.c:106: first defined here
```

`priority_comparator` was needed in both `thread.c` (for `ready_list`) and `synch.c` (for `sema->waiters`) and ended up with a full function body defined in both files. Each `.c` file compiles independently into its own `.o` with no visibility into any other file, so it's only at the final link step that two object files both claiming to fully define the same function name gets discovered, and that's ambiguous enough that the linker refuses to proceed.

The fix keeps the definition in exactly one file, `thread.c`, and every other file that needs it gets only a declaration added to the shared header instead:

```c
/* threads/thread.h */
bool priority_comparator (const struct list_elem *a, const struct list_elem *b, void *aux);
```

`synch.c` already includes `thread.h` so it now sees the declaration and links against the one real definition living in `thread.c`.

Worth noting, the `-Wmissing-prototypes` warning (`no previous prototype for 'priority_comparator'`) had actually been firing on every single build before this got fixed. That warning specifically flags a function defined without a prior header declaration, which is exactly the condition that let a silent duplicate slip through undetected until link time.

### Runtime bug - lost wakeup in `sema_up()`, caused by preemption

Every test hung at boot, including plain `alarm-single`, not just the donation specific tests. Nothing printed past the initial memory banner, just `TIMEOUT after 61 seconds`.

This turned out to be a race exposed by the Problem 2 preemption fix. `thread_start()` creates the idle thread and then blocks on `sema_down(&idle_started)`. Idle signals readiness through `sema_up(idle_started)`. The original `sema_up()` unblocked the waiter before incrementing `value`:

```c
/* Buggy order */
if (!list_empty (&sema->waiters))
  thread_unblock (...);   /* now can trigger an IMMEDIATE context switch */
sema->value++;
```

In vanilla Pintos this ordering was always safe, since `thread_unblock()` never actually switched threads mid call, it just marked one ready. The Problem 2 preemption addition changed that. `thread_unblock()` now calls `thread_yield()` immediately if the newly ready thread outranks the running one. At boot `main` (priority 31) outranks `idle` (priority 0), so the instant idle calls `thread_unblock(main)`, control switches to `main` mid function, before idle ever reaches `value++`.

`main` resumes inside `sema_down()`'s `while (sema->value == 0)` loop, finds `value` still `0`, and blocks again. A lost wakeup. Idle eventually resumes, finishes incrementing `value` (too late, it already used its one wake a waiter call), then blocks itself forever inside its idle loop. Both threads end up asleep with nothing left to wake either of them, and that's the permanent hang.

The fix is to increment `value` before unblocking, inside `sema_up()`:

```c
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;                          /* moved BEFORE thread_unblock() */

  if (!list_empty (&sema->waiters))
  {
    list_sort (&sema->waiters, priority_comparator, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }

  intr_set_level (old_level);
}
```

By the time `thread_unblock()` might trigger an immediate yield, `value` is already correct regardless of when the switch actually happens.

This matters well beyond just the boot sequence, since this is the same code path used by `lock_release()` and `cond_signal()`. Left unfixed it would have silently broken any priority based wakeup anywhere in the system, not only at boot.

### Bug caught during review - stale `waiting_lock` after acquiring

`lock_acquire()` sets `curr->waiting_lock = lock` when the lock is contended, but the original version never cleared it back to `NULL` once the thread actually acquired the lock. A thread that once waited on a lock and later goes on to hold that same lock would still show `waiting_lock` pointing at it, which means a future chain walk from some other thread could resolve `holder->waiting_lock->holder` right back onto the holder itself, risking a self referencing loop instead of terminating the chain correctly.

The fix is already folded into the `lock_acquire()` shown above, right after the lock is actually acquired:

```c
sema_down (&lock->semaphore);
lock->holder = thread_current ();
lock->holder->waiting_lock = NULL;   /* no longer waiting on anything */
```

### Open item, not yet confirmed either way

`lock_acquire()`'s donation list insertion reuses `priority_comparator`, which reads a node back out through `list_entry(a, struct thread, elem)`. The nodes actually being inserted into `donations` are `&current_thread->donation_elem`, not `&current_thread->elem`, two different fields sitting at two different byte offsets inside `struct thread`. In principle this means the comparator recovers the wrong base address whenever it's sorting `donations`, which should produce wrong ordering or garbage reads. In practice the full `priority-donate-*` suite, including `-multiple`, `-multiple2`, `-nest` and `-chain`, all passed. This is worth deliberately re-verifying rather than trusting that a passing test suite fully rules it out, maybe with a scenario built specifically to force a `donations` list to reorder after insertion. A dedicated `donation_comparator` that reads through `donation_elem` instead would be the safer fix if it turns out to matter after all.

### Testing Mission 2

Same as Mission 1, Pintos doesn't grade this mission on its own, everything gets checked together with one `make check` run at the end. But it's still worth running the Mission 2 tests on their own while you're building this piece, before Mission 3 exists.

1. **Build the kernel:**
```bash
   cd src/threads
   make
```

2. **Move into the build directory**, same as before, all test commands run from here:
```bash
   cd build
```

3. **Run the Mission 2 tests one at a time**, or all together with `make check` once everything else is in place:
```bash
   make tests/threads/priority-change.result
   make tests/threads/priority-donate-one.result
   make tests/threads/priority-donate-multiple.result
   make tests/threads/priority-donate-multiple2.result
   make tests/threads/priority-donate-nest.result
   make tests/threads/priority-donate-sema.result
   make tests/threads/priority-donate-lower.result
   make tests/threads/priority-donate-chain.result
   make tests/threads/priority-fifo.result
   make tests/threads/priority-preempt.result
   make tests/threads/priority-sema.result
   make tests/threads/priority-condvar.result
```

4. **Read the result**, same as Mission 1, each command prints `pass` or `fail` directly and the matching `.output` file inside `build/tests/threads/` has the full trace if something fails.

After the fixes described above, every one of these passes, along with all five Mission 1 alarm tests. `mlfqs-*` tests are Mission 3 scope, a separate scheduler that hasn't been rebuilt yet in this pass, and they're expected to fail until that mission gets implemented.




