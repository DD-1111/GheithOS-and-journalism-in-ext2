#include "debug.h"
#include "smp.h"
#include "debug.h"
#include "config.h"
#include "machine.h"

#include "threads.h"



namespace gheith {
    Atomic<uint32_t> TCB::next_id{0};

    TCB** activeThreads;
    TCB** idleThreads;

    Queue<TCB,InterruptSafeLock> readyQ{};
    Queue<TCB,InterruptSafeLock> zombies{};

    TCB* current() {
        auto was = Interrupts::disable();
        TCB* out = activeThreads[SMP::me()];
        Interrupts::restore(was);
        return out;
    }

    void entry() {
        sti();
        current()->doYourThing();
        stop();
    }

    void delete_zombies() {
        while (true) {
            auto it = zombies.remove();
            if (it == nullptr) return;
            delete it;
        }
    }

    void schedule(TCB* tcb) {
        if (!tcb->isIdle) {
            readyQ.add(tcb);
        }
    }

    struct IdleTcb: public TCB {
        IdleTcb(): TCB(true) {}
        void doYourThing() override {
            Debug::panic("should not call this");
        }
    };
};

void threadsInit() {
    using namespace gheith;
    activeThreads = new TCB*[kConfig.totalProcs]();
    idleThreads = new TCB*[kConfig.totalProcs]();

    // swiched to using idle threads in order to discuss in class
    for (unsigned i=0; i<kConfig.totalProcs; i++) {
        idleThreads[i] = new IdleTcb();
        activeThreads[i] = idleThreads[i];
    }

    // The reaper
    thread([] {
        Debug::printf("starting reaper\n");
        while (true) {
            ASSERT(!Interrupts::isDisabled());
            delete_zombies();
            asm volatile("pause");
            yield();
        }
    });
    
}

void yield() {
    using namespace gheith;
    block(BlockOption::CanReturn,[](TCB* me) {
        schedule(me);
    });
}

void stop() {
    using namespace gheith;

    while(true) {
        block(BlockOption::MustBlock,[](TCB* me) {
            if (!me->isIdle) {
                zombies.add(me);
            }
        });
        ASSERT(current()->isIdle);
    }
}