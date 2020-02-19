#include <os>
#include <smp>
#include <rtc>
#include <algorithm>
#include <thread>
static int task_counter = 0;

static void multiprocess_second_task() {
	task_counter += 1;
}
static void multiprocess_task(int task) {
	auto* t = new std::thread(&multiprocess_second_task);
	t->join();
	delete t;
}

void SMP::init_task()
{
	// make all CPUs migrate their threads to the main CPU
	SMP::migrate_threads_to(0);	
}

static double do_benchmark(const int tasks)
{
	std::vector<std::thread*> mpthreads;
	mpthreads.reserve(tasks);
	task_counter = 0;

	auto t0 = RTC::nanos_now();
	asm("" : : : "memory");

	for (int i = 0; i < tasks; i++)
	{
		mpthreads.push_back(
	    	new std::thread(&multiprocess_task, i)
		);
	}
	while (task_counter < tasks) os::block();

	auto t1 = RTC::nanos_now();

	double micros = (t1 - t0) / 1e3;
	printf("t0=%ld t1=%ld Time was %.2f micros\n", t0, t1, micros);

	for (auto* t : mpthreads) {
		t->join();
		delete t;
	}
	return micros;
}

void Service::start()
{
	const size_t rounds = 1000;
	const int tasks  = 64;
	printf("Benchmarking the startup time of %d tasks...\n", tasks);

	static size_t i = 0;
	os::on_panic(
		[] (const char* /* reason */) -> void {
			printf("Executed rounds: %zu / %zu, tasks: %d\n", 
					i, rounds, task_counter);
			i = i * tasks + task_counter;
			printf("Crash happened after %zu tasks, %zu threads\n", 
					i, i * 2);
		});
	os::set_panic_action(os::Panic_action::reboot);

	// enable multi-processing with threads
	SMP::migrate_threads();

	// one warm-up round
	do_benchmark(tasks);

	std::vector<double> times;
	for (i = 0; i < rounds; i++) {
		times.push_back( do_benchmark(tasks) );
	}
	std::sort(times.begin(), times.end());
	
	printf("Configuration: SMP=%d Tasks=%d Repeats=%zu\n", 
			SMP::cpu_count(), tasks, rounds);
	printf("Best time was %.2f micros\n", times[0]);
	printf("Median time was %.2f micros\n", times[times.size() / 2]);
	printf("Worst time was %.2f micros\n", times[times.size()-1]);
	
	os::shutdown();
}
