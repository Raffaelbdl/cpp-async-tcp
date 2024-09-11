#include "async_server.h"

using namespace fi;

// TODO:
// -add handshake timeout
// -finish processing all packets before we exit thread( cba rn, but its ez.look @ client )
async_tcp_server::async_tcp_server()
{
#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data_) != 0)
		throw exception(exception::reason_id::wsastartup_failure, "async_tcp_server::async_tcp_server: WSAStartup failed");
#endif // _WIN32
}

async_tcp_server::~async_tcp_server()
{
	stop();

	if (accepting_thread_.joinable())
		accepting_thread_.join();

	if (processing_thread_.joinable())
		processing_thread_.join();

	if (receiving_thread_.joinable())
		receiving_thread_.join();

	if (heartbeat_thread_.joinable())
		heartbeat_thread_.join();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
}

void async_tcp_server::start(std::string_view port)
{
	if (running_)
		throw exception(exception::reason_id::already_running, "async_tcp_server::start: attempted to start server while it was running");

	if (!process_callback_)
		throw exception(exception::reason_id::no_callback, "async_tcp_server::start: no processing callback set");

	addrinfo hints = {}, *result = nullptr;

	hints.ai_family = AF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(nullptr, port.data(), &hints, &result) != 0)
		throw exception(exception::reason_id::getaddrinfo_failure, "async_tcp_server::start: getaddrinfo error");

	server_socket_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	if (server_socket_ == -1)
	{
		freeaddrinfo(result);
		throw exception(exception::reason_id::socket_failure, "async_tcp_server::start: failed to create socket");
	}

	if (bind(server_socket_, result->ai_addr, result->ai_addrlen) == -1)
	{
		freeaddrinfo(result);
		throw exception(exception::reason_id::bind_error, "async_tcp_server::start: failed to bind socket");
	}

	if (listen(server_socket_, SOMAXCONN) == -1)
	{
		freeaddrinfo(result);
		throw exception(exception::reason_id::listen_error, "async_tcp_server::start: failed to listen on socket");
	}

	freeaddrinfo(result);

	running_ = true;

	accepting_thread_ = std::thread(&async_tcp_server::accept_clients, this);
	processing_thread_ = std::thread(&async_tcp_server::process_data, this);
	receiving_thread_ = std::thread(&async_tcp_server::receive_data, this);
	heartbeat_thread_ = std::thread(&async_tcp_server::run_heartbeat, this);
}

void async_tcp_server::stop()
{
	if (running_)
	{
		running_ = false;

		shutdown(server_socket_, 2);
		// closesocket(server_socket_);
		close(server_socket_);

		if (on_stop_callback_)
			on_stop_callback_(this);
	}

	connected_clients_.clear();
	process_buffers_.clear();
	clients_to_disconnect_.clear();
}

void async_tcp_server::disconnect_client(SOCKET who)
{
	std::lock_guard guard1(client_mtx_);
	std::lock_guard guard2(process_mtx_);

	auto it = std::find_if(connected_clients_.begin(), connected_clients_.end(), [&who](const SOCKET &s)
						   { return s == who; });

	if (it == connected_clients_.end())
		return;

	shutdown(who, 2);
	// closesocket(who);
	close(who);

	process_buffers_.erase(who);
	connected_clients_.erase(it);

	if (on_disconnect_callback_)
		on_disconnect_callback_(this, who);
}

bool async_tcp_server::is_running()
{
	return running_;
}

void async_tcp_server::send_packet(SOCKET to, packets::base_packet *packet)
{
	if (!packet)
		throw exception(exception::reason_id::packet_nullptr, "async_tcp_server::send_packet: packet was nullptr");

	std::lock_guard guard(send_mtx_);

	serializer.reset();

	// Serialize our data
	packet->serialize(serializer);

	// Allocate a buffer for our packet
	std::vector<std::uint8_t> packet_data(sizeof(packets::header) + serializer.get_serialized_data_length());

	// Construct our packet header
	packets::header packet_header = construct_packet_header(
		serializer.get_serialized_data_length(),
		packet->get_id(),
		packets::flags::fl_none);

	// Write our packet into the buffer
	memcpy(packet_data.data(), &packet_header, sizeof(packets::header));

	memcpy(
		packet_data.data() + sizeof(packets::header),
		serializer.get_serialized_data(),
		serializer.get_serialized_data_length());

	// Attempt to send the packet
	if (!send_packet_internal(to, packet_data.data(), packet_data.size()))
		disconnect_client(to);
}

void async_tcp_server::register_callback(std::function<void(async_tcp_server *const, const SOCKET, const packets::packet_id, packets::detail::binary_serializer &)> callback_fn)
{
	if (!callback_fn)
		throw exception(exception::reason_id::null_callback, "async_tcp_server::register_callback: no callback given");

	process_callback_ = callback_fn;
}

void async_tcp_server::register_stop_callback(std::function<void(async_tcp_server *const)> callback_fn)
{
	on_stop_callback_ = callback_fn;
}

void async_tcp_server::register_connect_callback(std::function<void(async_tcp_server *const, const SOCKET)> callback_fn)
{
	on_connect_callback = callback_fn;
}

void async_tcp_server::register_disconnect_callback(std::function<void(async_tcp_server *const, const SOCKET)> callback_fn)
{
	on_disconnect_callback_ = callback_fn;
}

packets::header async_tcp_server::construct_packet_header(packets::packet_length length, packets::packet_id id, packets::packet_flags flags)
{
	packets::header packet_header = {};

	packet_header.flags = flags;
	packet_header.id = id;
	packet_header.length = sizeof(packets::header) + length;
	packet_header.magic = PACKET_MAGIC;

	return packet_header;
}

bool async_tcp_server::perform_handshake(SOCKET with)
{
	packets::header packet_header = construct_packet_header(0, packets::ids::id_handshake, packets::flags::fl_handshake_sv);

	auto buffer = reinterpret_cast<char *>(&packet_header);

	// Send our header with no body and the handshake_sv flag
	if (!send_packet_internal(with, &packet_header, sizeof(packets::header)))
		return false;

	// Receive a response back. Should be the header with handshake_cl flag
	int bytes_received = 0;
	do
	{
		int received = recv(with, buffer + bytes_received, sizeof(packets::header) - bytes_received, 0);

		if (received <= 0)
			return false;

		bytes_received += received;
	} while (bytes_received < sizeof(packets::header));

	// Check the header information for the information we are expecting
	if (packet_header.flags != packets::flags::fl_handshake_cl)
		return false;

	if (packet_header.id != packets::ids::id_handshake)
		return false;

	if (packet_header.length != sizeof(packets::header))
		return false;

	if (packet_header.magic != PACKET_MAGIC)
		return false;

	return true;
}

bool async_tcp_server::send_packet_internal(SOCKET to, void *const data, const packets::packet_length length)
{
	std::uint32_t bytes_sent = 0;
	do
	{
		int sent = send(
			to,
			reinterpret_cast<char *>(data) + bytes_sent,
			length - bytes_sent,
			0);

		if (sent <= 0)
			return false;

		bytes_sent += sent;
	} while (bytes_sent < length);

	return true;
}

void async_tcp_server::accept_clients()
{
	while (running_)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		auto client = accept(server_socket_, nullptr, nullptr);

		if (client == -1)
			continue;

		// Attempt to handshake with the client,
		// disconnect from it upon failure.
		if (!perform_handshake(client))
		{
			shutdown(client, 2);
			// closesocket(client);
			close(client);
			continue;
		}

		std::lock_guard guard(client_mtx_);
		connected_clients_.push_back(client);

		if (!on_connect_callback)
			continue;

		on_connect_callback(this, client);
	}
}

void async_tcp_server::process_data()
{
	while (running_)
	{ // The server will only process data for as long as it's running (fixme)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		std::lock_guard guard1(client_mtx_);
		std::lock_guard guard2(process_mtx_);

		for (std::size_t i = 0; i < connected_clients_.size(); i++)
		{
			auto client = connected_clients_[i];
			auto &process_buffer = process_buffers_[client];

			if (process_buffer.size() < sizeof(packets::header))
				continue;

			auto header = reinterpret_cast<packets::header *>(process_buffer.data());

			bool is_disconnect_packet = header->id == packets::ids::id_disconnect && header->flags & packets::flags::fl_disconnect;

			// Disconnect if we receive some malformed packet or
			// when the client wants to disconnect
			if (header->magic != PACKET_MAGIC || is_disconnect_packet)
			{
				disconnect_client(client);
				continue;
			}

			// We have received a full packet
			if (process_buffer.size() < header->length)
				continue;

			auto data_start = process_buffer.data() + sizeof(packets::header);
			std::uint32_t data_length = header->length - sizeof(packets::header);

			// Assign the data to our serializer
			serializer.assign_buffer(data_start, data_length);

			// Call the processing callback (it cannot be null)
			if (header->id > packets::ids::num_preset_ids)
				process_callback_(this, client, header->id, serializer);

			// Erase the packet from our buffer (client might've disconnected during callback,
			// therefore we need to check if the buffer still exists.
			if (process_buffers_.find(client) != process_buffers_.end())
				process_buffer.erase(process_buffer.begin(), process_buffer.begin() + data_length + sizeof(packets::header));
		}
	}
}

void async_tcp_server::receive_data()
{
	std::vector<std::uint8_t> buffer(buffer_size_);

	while (running_)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		std::lock_guard guard(client_mtx_);
		for (std::size_t i = 0; i < connected_clients_.size(); i++)
		{
			auto &client = connected_clients_[i];

#if _WIN32 // only perform check with window

			// Check if we received any data from our client
			// so we don't block the thread with recv
			unsigned long available_to_read = 0;
			ioctlsocket(client, FIONREAD, &available_to_read);

			if (!available_to_read)
				continue;
#endif

			int bytes_received = recv(client, reinterpret_cast<char *>(buffer.data()), buffer_size_, 0);

			switch (bytes_received)
			{
			case -1:
				// Disconnect the client on error
				// We don't disconnect him on code 0 as that
				// implies the client closed the connection by
				// himself, which means it should've sent a
				// disconnect packet.
				disconnect_client(client);
				break;
			case 0:
				break;
			default: // Received bytes, process them
				std::lock_guard guard(process_mtx_);

				auto &process_buffer = process_buffers_[client];
				process_buffer.insert(process_buffer.end(), buffer.begin(), buffer.begin() + bytes_received);
			}
		}
	}

	// Disconnect all clients on shutdown
	for (auto &client : connected_clients_)
		disconnect_client(client);
}

void async_tcp_server::run_heartbeat()
{
	auto next = std::chrono::high_resolution_clock::now() + heartbeat_interval_;
	while (running_)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		if (std::chrono::high_resolution_clock::now() < next)
			continue;

		std::lock_guard client_guard(client_mtx_);
		for (auto it = connected_clients_.begin(); it != connected_clients_.end();)
		{
			auto &client = *it;

			auto header = construct_packet_header(0, packets::ids::id_heartbeat, packets::flags::fl_heartbeat);

			// If we failed to send the packet, something is wrong. Disconnect the client
			if (!send_packet_internal(client, &header, sizeof(header)))
				disconnect_client(client);
			else
				it++;
		}

		auto next = std::chrono::high_resolution_clock::now() + heartbeat_interval_;
	}
}