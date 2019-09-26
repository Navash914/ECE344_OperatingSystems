#include "thread.h"
#include "interrupt.h"
#include "test_thread.h"

int
main(int argc, char **argv)
{
	thread_init();
	register_interrupt_handler(0);
	/* Test how kill interacts with wait */
	test_wait_kill();	
	return 0;
}
