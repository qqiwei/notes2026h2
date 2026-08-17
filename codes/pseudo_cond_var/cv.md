## 初探 条件变量



### 一、等待、唤醒 与 虚假唤醒

#### 等待、虚假唤醒

```c
// 1
lock(mutex);

while (!ready) {
    // 2 3
    wait(cond_var, mutex); // 0
}
// 4
auto item = buffer.pop();
unlock(mutex);
```

0. 虚假唤醒（比如，中断信号）

     导致：需要用 `while` 循环

     - 在 C++ 中

       带条件的 wait 接口内部自带了这样的循环

       ```cpp
       cv.wait(lock, predicate);
       ```

       相当于

       ```c
       while (!predicate()) { wait(cv, mutex); }
       ```

     设计哲学：信号，只是通知，不是资源计数

1. 等待：`pthread_cond_wait`（`wait` in C++）

   0. 调用前（手动）：持有**锁**

        锁：保护资源，对 predicate / ready 构成同步

        必要性：否则，存在灾难窗口，即，在 wait 即将执行前，signal 丢失；因此，对 signal 也是必要的

   1. 原子地（理论语义）：

      1. 释放**锁**

         不是原子地（理论语义），就存在灾难窗口

         简单说，另一个线程，在这里，持有锁，唤醒丢失

      2. 挂起线程

   2. 唤醒时（自动）：

      1. 持有**锁**

   3. 返回

   4. 唤醒后（手动）：获取资源，释放**锁**

2. 唤醒：`pthread_cond_signal`(`notify_one`) / `pthread_cond_broadcast`(`notify_all`)

#### 通知 与 等待队列 转移

通知，放在锁外，有劣势：
```c
lock(mutex);
ready = true;
unlock(mutex);

signal(cond_var);
```

1.   存在风险，解锁后，销毁条件变量，导致 signal 访问野地址
2.   解锁后，被立即消费，再做无效通知
3.   在 broadcast 场景下，全部唤醒，竞争锁

放在锁内：

```c
lock(mutex);

ready = true;
signal(cond_var);

unlock(mutex);
```

-   早期问题：唤醒后，抢锁；性能
-   优化：内核（`futex_cmp_requeue(&cond, &mutex, cond_value)`）在唤醒已经挂起的线程时，会将它先转移到锁的等待队列中，而不是像下面 “初探 内核 `futex`” 一节中的 `futex_wait(&cond, cond_value)` 那样，直接唤醒。



### 二、原子性 与 初探 内核 `futex`

#### 原子性、`futex`

简化版本，伪代码如下。

```cpp
class condition_variable {
  atomic unsigned seq{0};
  // atomic unsigned waiters{0};

  void wait(mutex &mtx) {
    // ++waiters;
    auto value = seq;
    mtx.unlock();
    futex_wait(&seq, value);
    mtx.lock();
    // --waiters;
  }

  void notify_one() {
    // if (waiters == 0) return;
    ++seq;
    futex_wake(&seq, 1);
  }
};
```

可见，wait 将原子性延迟到内核 futex 中了。好处：提前释放了用户业务锁。

futex_wait 持有哈希桶锁，再判断用户态的值，是否已修改，并据此挂起。

```c
void futex_wait(uint32_t *seq, uint32_t value) {
  {
    auto lock_guard(hash_spinlock(&seq));
    if (*seq != value)
      return;
    set_current_state(TASK_INTERRUPTIBLE);
    enqueue(&seq);
  }
  schedule();  // suspend the current thread, if it's INTERRUPTIBLE
  set_current_state(TASK_RUNNING);

  auto lock_guard(hash_spinlock(&seq));
  if (in_queue(&seq))  // interrupted, not waken
    dequeue(&seq)
}

void futex_wake(uint32_t *seq, int count) {
  wake_queue q;
  {
    auto lock_guard(hash_spinlock(&seq));
    q.add(dequeue(&seq, count));
  }
  q.wake();
}
```

#### 无锁 等待/通知

`std::atomic::wait` / `notify_*`

-   性能利器
    -   直接利用 `futex`；无用户态锁
    -   无用户态数据结构维护；数据结构简单，缓存友好
    -   更少的虚假唤醒或丢失：保证内部先做值的检查
    
-   不足
    -   只支持单原子变量，缺少锁的复合表达能力；而条件变量支持复杂的条件
    
        只能做通知，不具备临界区资源保护能力
    
    -   没有锁，不存在先转移到锁的等待队列上，`notify_all` 会唤醒所有线程，惊群效应
    
    -   在高并发场景下，存在队列共享，导致频繁唤醒无关线程
    
        而条件变量虽然有虚假唤醒，但 `notify_one` 只会唤醒一个线程，这在任务队列中非常高效
    
    -   复合设计难度大：ABA 问题 与 解法：递增序列号；虚假唤醒；内存序



## 三态锁 `mutex`

类似地，在快路径上，三态锁 只在 `unlocked` 和 `locked_no_waiter` 间切换状态，不涉及内核。抢锁失败，才会出现 `locked_waiters` 状态，并涉及内核态的挂起唤醒。

同时，实际上，状态变量，其低2位就足够使用了，高位就留给业务了。

`pthread_mutex_t`、 `pthread_cond_t` 等，正是在 三态锁 和 `futex` 基础之上构造的。



## 再探 条件变量











