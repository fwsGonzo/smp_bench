#include <service>
#include <smp>
#include <statman>
#include <net/inet>
#include <net/interfaces>

static const uint16_t PORT = 1234;

struct SMP_Queue
{
	bool enqueue(net::Packet_ptr packet)
	{
		this->spinner.lock();
		bool qfirst = this->queue.empty();
		this->queue.push_back(std::move(packet));
		this->spinner.unlock();
		return qfirst;
	}
	std::vector<net::Packet_ptr> grab_queue()
	{
		this->spinner.lock();
		auto vec = std::move(this->queue);
		this->spinner.unlock();
		return vec;
	}
	
	std::vector<net::Packet_ptr> queue;
	smp_spinlock spinner;
};

struct TCP_MP
{
	TCP_MP(int cpu, net::Inet& inet)
		: m_cpu(cpu), m_inet(inet)
	{
		inet.ip_obj().set_tcp_handler({this, &TCP_MP::tcp_incoming});
		inet.tcp().set_network_out4({this, &TCP_MP::tcp_outgoing});
		inet.on_transmit_queue_available({this, &TCP_MP::tcp_process_writeq});		
	}

	void tcp_incoming(net::Packet_ptr packet)
	{
		bool qfirst = incoming.enqueue(std::move(packet));
		if (qfirst)
		{
			bool first = SMP::add_task(
			[this] () -> void
			{
				for (auto& packet : incoming.grab_queue()) {
					this->m_inet.tcp().receive4(std::move(packet));
				}
			}, this->m_cpu);
			if (first) SMP::signal(this->m_cpu);
		}
	}
	void tcp_outgoing(net::Packet_ptr packet)
	{
		bool qfirst = outgoing.enqueue(std::move(packet));
		if (qfirst)
		{
			bool first = SMP::add_bsp_task(
			[this] () -> void {
				for (auto& packet : outgoing.grab_queue()) {
					this->m_inet.ip_obj().transmit(std::move(packet));
				}
			});
			if (first) SMP::signal_bsp();
		}
	}
	void tcp_process_writeq(size_t packets)
	{
		// How to accelerate this?
		bool first = SMP::add_task(
			[this, packets] () {
				this->m_inet.tcp().process_writeq(packets);
			}, this->m_cpu);
		if (first) SMP::signal(this->m_cpu);
	}

private:
	signed int m_cpu;
	net::Inet& m_inet;
	
	SMP_Queue incoming;
	SMP_Queue outgoing;
};

void Service::start()
{
	static auto& inet = net::Interfaces::get(0);
	static TCP_MP tcp_mp { 1, inet };

	auto& server = inet.tcp().listen(PORT);
	server.on_connect(
	[] (auto conn)
	{
		size_t* bytes = new size_t(0);
		uint64_t* start = new uint64_t(RTC::nanos_now());
		printf("[CPU %d] * Receiving data on port %u\n", 
				SMP::cpu_id(), PORT);

		// retrieve binary
		conn->on_read(0,
		[conn, bytes] (auto buf)
		{
			*bytes += buf->size();			
		})
		.on_close(
		[bytes, start] () {
			printf("[CPU %d] * Bytes received: %zu b\n", SMP::cpu_id(), *bytes);
			double secs = (RTC::nanos_now() - *start) / 1e9;
			double mbs  = *bytes / (1024 * 1024.0) / secs;
			printf("[CPU %d] * Time: %.2fs, %.2f MB/sec, %.2f Mbit/sec\n",
					SMP::cpu_id(), secs, mbs, mbs * 8.0);
			delete bytes;
		});
	});

	printf("Listening on %s:%u\n", inet.ip_addr().to_string().c_str(), PORT);
}
