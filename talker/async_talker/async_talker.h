#pragma once

#pragma region os_dependent_includes

#ifdef _WIN32 // Windows Machine

#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#elif __linux__ // Linux machine

#include <unistd.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#else
#error OS unknown or not supported.
#endif // _WIN32

#pragma endregion os_dependent_includes

#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include <unordered_map>

#include "../../shared/packets/packets.h"

namespace fi
{
    using SOCKET = int;

    class async_udp_talker
    {
    public:
        async_udp_talker();
        ~async_udp_talker();

        void set_destination(std::string_view ip, std::string_view port);
        void send_packet(packets::base_packet *const packet);

        packets::header construct_packet_header(packets::packet_length length, packets::packet_id id, packets::packet_flags flags);

        // Function for sending our packet
        bool send_packet_internal(void *const data, const packets::packet_length length);

        SOCKET socket_ = 0;
        addrinfo *dest = nullptr;

        std::mutex send_mtx_ = {};

        // This will help us in serializing our packet data
        packets::detail::binary_serializer serializer = {};

    public:
        class exception : public std::exception
        {
        public:
            enum reason_id : std::uint8_t
            {
                none = 0,
                wsastartup_failure,
                already_connected,
                getaddrinfo_failure,
                socket_failure,
                connection_error,
                packet_nullptr,
                null_callback,
                no_callback

            };

            exception(reason_id reason, std::string_view what) : reason_(reason), what_(what) {};

            virtual const char *what() const noexcept
            {
                return what_.data();
            }

            const reason_id get_reason()
            {
                return reason_;
            }

        private:
            std::string what_ = {};
            reason_id reason_ = reason_id::none;
        };
    };

} // namespace fi
