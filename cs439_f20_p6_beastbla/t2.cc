#include "debug.h"
#include "threads.h"
#include "atomic.h"
#include "bb.h"
#include "future.h"

constexpr int count = 50000;

uint32_t counts[4];

/* Called by one CPU */
void kernelMain(void) {

    BoundedBuffer<int> buffer{5};    

    Atomic<int> sum{0};
    Atomic<int> togo{count};
    Future<bool> done{};

    for (int i=0; i<4; i++) {
        thread([&,i] {
            while(true) {
                auto x = buffer.get();
		counts[i] ++;
                sum.add_fetch(x);
                if (togo.add_fetch(-1) == 0) done.set(true);
            }
        });
    }

    for (int i=1; i<=count; i++) {
        buffer.put(i);
    }

    done.get();

    Debug::printf("*** sum = %d\n",sum.get());
    Debug::printf("*** done = %d\n",togo.get());

    for (int i=0; i<4; i++) {
	if (counts[i] == 0) {
 	   Debug::printf("*** thread %d didn't do work\n",i);
	} else {
 	   Debug::printf("*** thread %d did work\n",i);
        }
    }

    Debug::panic("");

}

