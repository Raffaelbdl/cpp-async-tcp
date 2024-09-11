#include "async_listener.h"
#include <iostream>

using namespace fi;

async_udp_listener::async_udp_listener()
{
    // do stuff on windows
}

async_udp_listener::~async_udp_listener()
{
    stop();

    if (processing_thread_.joinable())
        processing_thread_.join();

    if (receiving_thread_.joinable())
        receiving_thread_.join();

    if (heartbeat_thread_.joinable())
        heartbeat_thread_.join();

    // do stuff on windows
}

void async_udp_listener::start(std::string_view port)
{
    if (running_)
        throw exception(exception::reason_id::already_running, "async_udp_listener::start: attempted to start server while it was running");

    if (!process_callback_)
        throw exception(exception::reason_id::no_callback, "async_udp_listener::start: no processing callback set");

    addrinfo hints = {}, *result = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(nullptr, port.data(), &hints, &result) != 0)
        throw exception(exception::reason_id::getaddrinfo_failure, "async_udp_listener::start: getaddrinfo error");

    server_socket_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (server_socket_ == -1)
    {
        freeaddrinfo(result);
        throw exception(exception::reason_id::socket_failure, "async_udp_listener::start: failed to create socket");
    }

    if (bind(server_socket_, result->ai_addr, result->ai_addrlen) == -1)
    {
        freeaddrinfo(result);
        close(server_socket_);
        throw exception(exception::reason_id::bind_error, "async_udp_listener::start: failed to bind socket");
    }

    running_ = true;

    processing_thread_ = std::thread(&async_udp_listener::process_data, this);
    receiving_thread_ = std::thread(&async_udp_listener::receive_data, this);
    heartbeat_thread_ = std::thread(&async_udp_listener::run_heartbeat, this);
}

void async_udp_listener::stop()
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
}

bool async_udp_listener::is_running()
{
    return running_;
}

void async_udp_listener::register_callback(std::function<void(async_udp_listener *const, const SOCKET, const packets::packet_id, packets::detail::binary_serializer &)> callback_fn)
{
    if (!callback_fn)
        throw exception(exception::reason_id::null_callback, "async_udp_listener::register_callback: no callback given");

    process_callback_ = callback_fn;
}

void async_udp_listener::register_stop_callback(std::function<void(async_udp_listener *const)> callback_fn)
{
    on_stop_callback_ = callback_fn;
}

packets::header async_udp_listener::construct_packet_header(packets::packet_length length, packets::packet_id id, packets::packet_flags flags)
{
    packets::header packet_header = {};

    packet_header.flags = flags;
    packet_header.id = id;
    packet_header.length = sizeof(packets::header) + length;
    packet_header.magic = PACKET_MAGIC;

    return packet_header;
}

void async_udp_listener::process_data()
{
    while (running_)
    { // The server will only process data for as long as it's running (fixme)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::lock_guard guard1(client_mtx_);
        std::lock_guard guard2(process_mtx_);

        // for (std::size_t i = 0; i < connected_clients_.size(); i++)
        for (std::size_t i = 0; i < 1; i++)
        {
            // auto client = connected_clients_[i];
            int client = 0;
            auto &process_buffer = process_buffers_[client];

            if (process_buffer.size() < sizeof(packets::header))
                continue;

            auto header = reinterpret_cast<packets::header *>(process_buffer.data());

            bool is_disconnect_packet = header->id == packets::ids::id_disconnect && header->flags & packets::flags::fl_disconnect;

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

void async_udp_listener::receive_data()
{
    std::vector<std::uint8_t> buffer(buffer_size_);

    struct sockaddr from;
    socklen_t fromlen = sizeof(from);

    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        int bytes_received = recvfrom(server_socket_, reinterpret_cast<char *>(buffer.data()), buffer_size_, 0, &from, &fromlen);

        switch (bytes_received)
        {
        case -1:
            // Disconnect the client on error
            // We don't disconnect him on code 0 as that
            // implies the client closed the connection by
            // himself, which means it should've sent a
            // disconnect packet.
            // disconnect_client(client);
            break;
        case 0:
            break;
        default: // Received bytes, process them

            std::lock_guard guard(process_mtx_);
            int client = 0; // TODO: handle receiving from multiple sources
            auto &process_buffer = process_buffers_[client];
            process_buffer.insert(process_buffer.end(), buffer.begin(), buffer.begin() + bytes_received);
        }
    }
}

void async_udp_listener::run_heartbeat()
{
    // no need to check if connection holds in UDP
}
