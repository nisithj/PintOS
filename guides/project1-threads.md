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
    thread_unblock(current_thread);
    list_remove(current);
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







