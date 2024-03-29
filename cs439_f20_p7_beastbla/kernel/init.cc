#include "init.h"

#include "debug.h"
#include "config.h"
#include "u8250.h"
#include "smp.h"
#include "machine.h"
#include "kernel.h"
#include "heap.h"
#include "threads.h"
#include "pit.h"
#include "idt.h"

struct Stack {
    static constexpr int BYTES = 4096;
    uint32_t bytes[BYTES] __attribute__ ((aligned(16)));
};

PerCPU<Stack> stacks;

static bool smpInitDone = false;

extern "C" uint32_t pickKernelStack(void) {
    return (uint32_t) &stacks.forCPU(smpInitDone ? SMP::me() : 0).bytes[Stack::BYTES];
}

static Atomic<uint32_t> howManyAreHere(0);

bool onHypervisor = true;

extern "C" void kernelInit(void) {
    uint32_t me = SMP::started.fetch_add(1);

    U8250 uart;

    if (me == 0) {
        Debug::init(&uart);
        Debug::debugAll = false;
        Debug::printf("\n| What just happened? Why am I here?\n");

        {
            Debug::printf("| Discovering my identity and features\n");
            cpuid_out out;
            cpuid(0,&out);

            Debug::printf("|     CPUID: ");
            auto one = [](uint32_t q) {
                for (int i=0; i<4; i++) {
                    Debug::printf("%c",(char) q);
                    q = q >> 8;
                }
            };
            one(out.b);
            one(out.d);
            one(out.c);
            Debug::printf("\n");

            cpuid(1,&out);
            if (out.c & 0x1) {
                Debug::printf("|     has SSE3\n");
            }
            if (out.c & 0x8) {
                Debug::printf("|     has MONITOR/MWAIT\n");
            }
            if (out.c & 0x80000000) {
                onHypervisor = true;
                Debug::printf("|     running on hypervisor\n");
            } else {
                onHypervisor = false;
                Debug::printf("|     running on physical hardware\n");
            }

        }

        /* discover configuration */
        configInit(&kConfig);
        Debug::printf("| totalProcs %d\n",kConfig.totalProcs);
        Debug::printf("| memSize 0x%x %dMB\n",
            kConfig.memSize,
            kConfig.memSize / (1024 * 1024));
        Debug::printf("| localAPIC %x\n",kConfig.localAPIC);
        Debug::printf("| ioAPIC %x\n",kConfig.ioAPIC);

        /* initialize the heap */
        heapInit((void*)0x100000,4 << 20);

        /* switch to dynamically allocated UART */
        Debug::init(new U8250);
        Debug::printf("| switched to new UART\n");

        /* initialize the thread module */
        threadsInit();

        /* initialize LAPIC */
        SMP::init(true);
        smpInitDone = true;
  
        /* initialize IDT */
        IDT::init();
        Pit::calibrate(1000);

        // The reset EIP has to be
        //     - divisible by 4K (required by LAPIC)
        //     - PPN must fit in 8 bits (required by LAPIC)
        //     - consistent with mbr.S
        for (uint32_t id = 1; id < kConfig.totalProcs; id++) {
            Debug::printf("| initialize %d\n",id);
            SMP::ipi(id, 0x4500);
            Debug::printf("| reset %d\n",id);
            Debug::printf("|      eip:0x%x\n",resetEIP);
            SMP::ipi(id, 0x4600 | (((uintptr_t)resetEIP) >> 12));
            while (SMP::started <= id);
        }
    } else {
        SMP::init(false);
    }

    // Initialize the PIT
    Pit::init();

    auto id = SMP::me();

    Debug::printf("| %d enabling interrupts, I'm scared\n",id);
    sti();

    auto myOrder = howManyAreHere.add_fetch(1);
    if (myOrder == kConfig.totalProcs) {
        thread([] {
            kernelMain();
            Debug::shutdown();
        });
    }
    stop();
}
