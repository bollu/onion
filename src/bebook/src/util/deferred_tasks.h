#ifndef UTIL_DEFERRED_TASKS_H_
#define UTIL_DEFERRED_TASKS_H_

#include <functional>
#include <queue>

using deferred_task = typename std::function<void()>;

class DeferredTasks
{
    std::queue<deferred_task> queue;

public:

    DeferredTasks();
    virtual ~DeferredTasks();

    void submit(deferred_task task);
    
    // Return true if ran tasks
    bool run_all();
};

#endif
