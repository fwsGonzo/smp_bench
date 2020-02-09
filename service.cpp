#include <os>
#include <smp>
#include <rtc>
#include <thread>
smp_barrier barry;

static void multiprocess_task(int /* task */) {
  barry.increment();
}

void Service::start()
{
  // enable multi-processing with threads
  SMP::migrate_threads();

  const int tasks = 64;
  printf("Benchmarking the startup time of %d tasks...\n", tasks);

  std::vector<std::thread*> mpthreads;
  mpthreads.reserve(tasks);
  auto t0 = RTC::nanos_now();
  asm("" : : : "memory");

  for (int i = 0; i < tasks; i++)
  {
    mpthreads.push_back(
	    new std::thread(&multiprocess_task, i)
  	);
  }

  barry.spin_wait(tasks);
  auto t1 = RTC::nanos_now();

  double micros = (t1 - t0) / 1e3;
  printf("t0=%ld t1=%ld Time was %.2f micros\n", t0, t1, micros);

  for (auto* t : mpthreads) {
    t->join();
	delete t;
  }
}
