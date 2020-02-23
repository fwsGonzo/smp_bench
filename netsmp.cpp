#include <service>
#include <smp>
#include <statman>
#include <net/inet>
#include <net/interfaces>
#include <kernel/events.hpp>

static const uint16_t PORT = 1234;

struct SMP_Queue
{
	SMP_Queue(size_t r) : reserve(r) {
		// this reduces heap contention
		this->queue.reserve(this->reserve);
	}

	bool enqueue(net::Packet_ptr packet)
	{
		this->spinner.lock();
		bool qfirst = this->queue.empty();
		if (qfirst) {
			this->queue.reserve(this->reserve);
		}
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
	
	smp_spinlock spinner;
	std::vector<net::Packet_ptr> queue;
	const size_t reserve;
};

struct TCP_MP
{
	TCP_MP(int cpu, net::Inet& inet)
		: m_cpu(cpu), m_inet(inet)
	{
		inet.ip_obj().set_tcp_handler({this, &TCP_MP::tcp_incoming});
		inet.tcp().set_network_out4({this, &TCP_MP::tcp_outgoing});
		// we will only process packets our way
		inet.clear_transmit_queue_available();
		inet.on_transmit_queue_available({this, &TCP_MP::tcp_process_writeq});
		// reinstate processing for for pings, DHCP and DNS
		inet.on_transmit_queue_available({&inet.udp(), &net::UDP::process_sendq});
		// events on the TCP processing CPU
		m_inc_event = Events::get(cpu).subscribe({this, &TCP_MP::process_incoming});
		m_out_event = Events::get( 0 ).subscribe({this, &TCP_MP::process_outgoing});
		m_tqa_event = Events::get(cpu).subscribe({this, &TCP_MP::smp_process_wq});
	}

	void tcp_incoming(net::Packet_ptr packet)
	{
		if ( incoming.enqueue(std::move(packet)) )
		{
			SMP::unicast(this->m_cpu, this->m_inc_event);
		}
	}
	void tcp_outgoing(net::Packet_ptr packet)
	{
		if ( outgoing.enqueue(std::move(packet)) )
		{
			SMP::unicast(0, this->m_out_event);
		}
	}
	void tcp_process_writeq(size_t)
	{
		// trigger packet processing event on the specified CPU
		SMP::unicast(this->m_cpu, this->m_tqa_event);
	}

private:
	void smp_process_wq()
	{
		Expects(SMP::cpu_id() == this->m_cpu);
		// TQA can underflow here because its unsigned and result of a subtraction
		signed packets = m_inet.transmit_queue_available();
		// NOTE: you will have to make this public in net/tcp/tcp.hpp
		this->m_inet.tcp().process_writeq(packets >= 0 ? packets : 0);
	}
	void process_incoming()
	{
		auto vec = incoming.grab_queue();
		for (auto& packet : vec) {
			this->m_inet.tcp().receive4(std::move(packet));
		}
	}
	void process_outgoing()
	{
		auto vec = outgoing.grab_queue();
		for (auto& packet : vec) {
			this->m_inet.ip_obj().transmit(std::move(packet));
		}
	}

	signed int m_cpu;
	signed int m_inc_event;
	signed int m_out_event;
	signed int m_tqa_event;
	net::Inet& m_inet;
	
	SMP_Queue incoming {512};
	SMP_Queue outgoing {128};
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
			delete start;
		});
	});

	printf("Listening on %s:%u\n", inet.ip_addr().to_string().c_str(), PORT);
}
