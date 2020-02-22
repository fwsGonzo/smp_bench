#include <service>
#include <smp>
#include <statman>
#include <net/inet>
#include <net/interfaces>
static const uint16_t PORT = 1234;

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
		bool first = SMP::add_task(
			SMP::task_func::make_packed(
			[this, p = std::move(packet)] () mutable -> void {
				this->m_inet.tcp().receive4(std::move(p));
			}), this->m_cpu);
		if (first) SMP::signal(this->m_cpu);
	}
	void tcp_outgoing(net::Packet_ptr packet)
	{
		bool first = SMP::add_bsp_task(
			SMP::task_func::make_packed(
			[this, p = std::move(packet)] () mutable -> void {
				this->m_inet.ip_obj().transmit(std::move(p));
			}));
		if (first) SMP::signal_bsp();
	}
	void tcp_process_writeq(size_t packets)
	{
		bool first = SMP::add_task(
			[this, packets] () {
				this->m_inet.tcp().process_writeq(packets);
			}, this->m_cpu);
		if (first) SMP::signal(this->m_cpu);
	}

private:
	signed int m_cpu;
	net::Inet& m_inet;
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
		printf("[CPU %d] * Receiving data on port %u\n", 
				SMP::cpu_id(), PORT);

		// retrieve binary
		conn->on_read(0,
		[conn, bytes] (auto buf)
		{
			*bytes += buf->size();			
		})
		.on_close(
		[bytes] () {
			printf("[CPU %d] * Bytes received: %zu b\n", SMP::cpu_id(), *bytes);
			delete bytes;
		});
	});

	using namespace std::chrono;
	Timers::periodic(1s, 
		[] (int) {
			auto& sm = Statman::get();
			uint32_t sendq_now = sm.get_by_name("eth0.sendq_now").get_uint32();
			uint32_t sendq_max = sm.get_by_name("eth0.sendq_max").get_uint32();
			
			printf("%s:  SendQ now=%u max=%u\n", 
					inet.ifname().c_str(),
					sendq_now, sendq_max);
		});

	printf("Listening on %s:%u\n", inet.ip_addr().to_string().c_str(), PORT);
}
