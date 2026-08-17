# AGENTS.md

1.   This repo is a teaching demo, about `futex`, `three_states_lock`, and `condition_variable`.

2.   Do NOT compile, do NOT run. Never invoke a compiler after making changes.

3.   Layers top down:

     ```cpp
     std::condition_variable
         [pthread_cond_t]
             [futex_syscall]
             [three_states_lock_t]
             pthread_mutex_t
                 three_states_lock_t
                     [futex_syscall]
         std::unique_lock
         std::mutex
             pthread_mutex_t
                 three_states_lock_t
                 ...
     ```

     