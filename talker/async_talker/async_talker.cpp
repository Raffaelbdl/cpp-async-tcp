#include "async_talker.h"

using namespace fi;

fi::async_udp_talker::async_udp_talker()
{
    // do windows stuff
}

fi::async_udp_talker::~async_udp_talker()
{
    if (dest != nullptr)
        freeaddrinfo(dest);
    // do windows stuff
}

void fi::async_udp_talker::set_destination(std::string_view ip, std::string_view port)
{
    if (dest != nullptr)
        freeaddrinfo(dest);

    addrinfo hints = {};

    hints.ai_family = AF_INET;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(ip.data(), port.data(), &hints, &dest) != 0)
        throw exception(exception::reason_id::getaddrinfo_failure, "async_udp_talker::connect: getaddrinfo error");

    socket_ = ::socket(dest->ai_family, dest->ai_socktype, dest->ai_protocol);

    if (socket_ == -1)
    {
        freeaddrinfo(dest);
        throw exception(exception::reason_id::socket_failure, "async_tcp_client::connect: failed to create socket");
    }

    // freeaddrinfo(result); // do not free
}

void fi::async_udp_talker::send_packet(packets::base_packet *const packet)
{
    if (!packet)
        throw exception(exception::reason_id::packet_nullptr, "async_udp_talker::send_packet: packet was nullptr");

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
    send_packet_internal(packet_data.data(), packet_data.size());
}

packets::header fi::async_udp_talker::construct_packet_header(packets::packet_length length, packets::packet_id id, packets::packet_flags flags)
{
    packets::header packet_header = {};

    packet_header.flags = flags;
    packet_header.id = id;
    packet_header.length = sizeof(packets::header) + length;
    packet_header.magic = PACKET_MAGIC;

    return packet_header;
}

bool fi::async_udp_talker::send_packet_internal(void *const data, const packets::packet_length length)
{
    std::uint32_t bytes_sent = 0;
    do
    {
        int sent = sendto(
            socket_,
            reinterpret_cast<char *>(data) + bytes_sent,
            length - bytes_sent,
            0,
            dest->ai_addr, dest->ai_addrlen);

        // int sent = send(
        //     socket_,
        //     reinterpret_cast<char *>(data) + bytes_sent,
        //     length - bytes_sent,
        //     0);

        if (sent <= 0)
            return false;

        bytes_sent += sent;
    } while (bytes_sent < length);

    return true;
}
