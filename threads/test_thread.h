#ifndef _TEST_THREAD_H_
#define _TEST_THREAD_H_

void test_basic(void);
void test_preemptive(void);
void test_wakeup(int all);
void test_wait(void);
void test_wait_kill(void);
void test_wait_parent(void);
void test_lock(void);
void test_cv_signal(void);
void test_cv_broadcast(void);

#endif /* _TEST_THREAD_H_ */
