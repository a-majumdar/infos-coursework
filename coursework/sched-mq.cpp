/*
 * The Priority Task Scheduler
 * SKELETON IMPLEMENTATION TO BE FILLED IN FOR TASK 1
 */

#include <infos/kernel/sched.h>
#include <infos/kernel/thread.h>
#include <infos/kernel/log.h>
#include <infos/util/list.h>
#include <infos/util/lock.h>

using namespace infos::kernel;
using namespace infos::util;

/**
 * A Multiple Queue priority scheduling algorithm
 */
class MultipleQueuePriorityScheduler : public SchedulingAlgorithm
{
public:
    /**
     * Returns the friendly name of the algorithm, for debugging and selection purposes.
     */
    const char* name() const override { return "mq"; }
    
    List<SchedulingEntity *> runqueue;
    List<SchedulingEntity *> realtime;
    List<SchedulingEntity *> interactive;
    List<SchedulingEntity *> normal;
    List<SchedulingEntity *> daemon;

    /**
     * Called during scheduler initialisation.
     */
    void init()
    {
        // TODO: Implement me!

    }

    /**
     * Called when a scheduling entity becomes eligible for running.
     * @param entity
     */
    void add_to_runqueue(SchedulingEntity& entity) override
    {
        // TODO: Implement me!
        UniqueIRQLock l;
        runqueue.enqueue(&entity);
        if ((&entity)->priority() == 1) { realtime.enqueue(&entity); }
        else if ((&entity)->priority() == 2) { interactive.enqueue(&entity); }
        else if ((&entity)->priority() == 3) { normal.enqueue(&entity); }
        else if ((&entity)->priority() == 4) { daemon.enqueue(&entity); }
        else { syslog.messagef(LogLevel::ERROR, "Entity is not of any known priority level"); }
    }

    /**
     * Called when a scheduling entity is no longer eligible for running.
     * @param entity
     */
    void remove_from_runqueue(SchedulingEntity& entity) override
    {
        // TODO: Implement me!
        UniqueIRQLock l;
        runqueue.remove(&entity);
        if ((&entity)->priority() == 1) { realtime.remove(&entity); }
        else if ((&entity)->priority() == 2) { interactive.remove(&entity); }
        else if ((&entity)->priority() == 3) { normal.remove(&entity); }
        else if ((&entity)->priority() == 4) { daemon.remove(&entity); }
        else { syslog.messagef(LogLevel::ERROR, "Entity is not of any known priority level"); }
    }

    /**
     * Called every time a scheduling event occurs, to cause the next eligible entity
     * to be chosen.  The next eligible entity might actually be the same entity, if
     * e.g. its timeslice has not expired.
     */
    SchedulingEntity *pick_next_entity() override
    {
        // TODO: Implement me!
        if (runqueue.empty()) { return nullptr; }
        else if (!realtime.empty()) 
        {
            UniqueIRQLock l;
            realtime.enqueue(realtime.dequeue());
            return realtime.last();
        }
        else if (!interactive.empty())
        {
            UniqueIRQLock l;
            interactive.enqueue(interactive.dequeue());
            return interactive.last();
        }
        else if (!normal.empty())
        {
            UniqueIRQLock l;
            normal.enqueue(normal.dequeue());
            return normal.last();
        }
        else if (!daemon.empty())
        {
            UniqueIRQLock l;
            daemon.enqueue(daemon.dequeue());
            return daemon.last();
        }
        else 
        {
            syslog.messagef(LogLevel::ERROR, "Priority queues are empty");
            return nullptr;
        }
    }
};

/* --- DO NOT CHANGE ANYTHING BELOW THIS LINE --- */

RegisterScheduler(MultipleQueuePriorityScheduler);