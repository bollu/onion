#include "./deferred_tasks.h"

DeferredTasks::DeferredTasks()
{
}

DeferredTasks::~DeferredTasks()
{
    run_all();
}

void DeferredTasks::submit(deferred_task task)
{
    queue.push(task);
}

bool DeferredTasks::run_all()
{
    bool ran_task = false;
    while (!queue.empty())
    {
        queue.front()();
        queue.pop();
        ran_task = true;
    }

    return ran_task;
}
