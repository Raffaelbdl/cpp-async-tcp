#include <iostream>
#include "server/async_server/async_server.h"
#include "client/async_client/async_client.h"
#include "listener/async_listener/async_listener.h"
#include "talker/async_talker/async_talker.h"

#include "shared/packets/packets.h"
#include "shared/packets/packet_base.h"

using SOCKET = int;

#define sPROCESS_PACKET_FN(ID, name) void name(fi::async_tcp_server *const sv, const SOCKET from, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)

sPROCESS_PACKET_FN(fi::packets::id_example, s_on_example_packet)
{
    // Read our packet
    fi::packets::example_packet example(s);

    // Now we can access our data
    for (std::size_t i = 0; i < example.some_string_array.size(); i++)
        printf("[ %i ] %s\n", i, example.some_string_array[i].data());

    // Answer the client
    example.some_string_array = {"Hello", "from", "server!"};

    sv->send_packet(from, &example);
}

#define cPROCESS_PACKET_FN(ID, name) void name(fi::async_tcp_client *const cl, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)

cPROCESS_PACKET_FN(fi::packets::id_example, c_on_example_packet)
{
    fi::packets::example_packet example(s);

    // Now we can access our data
    for (std::size_t i = 0; i < example.some_string_array.size(); i++)
        printf("[ %i ] %s\n", i, example.some_string_array[i].data());

    // Disconnect from our server, as we're done communicating.
    cl->disconnect();
}

#define lPROCESS_PACKET_FN(ID, name) void name(fi::async_udp_listener *const sv, const SOCKET from, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)

lPROCESS_PACKET_FN(fi::packets::id_example, l_on_example_packet)
{
    // Read our packet
    fi::packets::example_packet example(s);

    // Now we can access our data
    for (std::size_t i = 0; i < example.some_string_array.size(); i++)
        printf("[ %i ] %s\n", i, example.some_string_array[i].data());

    // Does not answer!
}

int main_server()
{
    try
    {
        fi::async_tcp_server server = {};

        // Setup all our callbacks before starting the server
        server.register_connect_callback([](fi::async_tcp_server *const sv, SOCKET who)
                                         { printf("Client with socket ID %i has connected.\n", who); });

        server.register_disconnect_callback([](fi::async_tcp_server *const sv, SOCKET who)
                                            {
			printf( "Client with socket ID %i has disconnected.\n", who );
			
			// Stop the server as we're done communicating
			sv->stop( ); });

        server.register_stop_callback([](fi::async_tcp_server *const sv)
                                      { printf("Server has been stopped.\n"); });

        server.register_callback([](fi::async_tcp_server *const sv, SOCKET from, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)
                                 {
			// You can use a switch case, an unordered map, an array.. whichever suits you best
			switch ( id ) {
				case fi::packets::id_example:
					s_on_example_packet( sv, from, id, s );
					break;
				default:
					printf( "Unknown packet ID %i received\n", id );
			} });

        // Attempt to start the server
        server.start("1337");

        printf("Server running on port 1337.\n");

        // Wait for our server to stop running
        while (server.is_running())
        {
            // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::cout << "do smth" << std::endl;
        }

        return 0;
    }
    catch (const fi::async_tcp_server::exception &e)
    {
        printf("%s\n", e.what());
        return 1;
    }
}

int main_client()
{
    try
    {
        fi::async_tcp_client client = {};

        client.register_disconnect_callback([](fi::async_tcp_client *const cl)
                                            { printf("Disconnected from server.\n"); });

        client.register_callback([](fi::async_tcp_client *const cl, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)
                                 {
			// You can use a switch case, an unordered map, an array.. whichever suits you best
			switch ( id ) {
				case fi::packets::id_example:
					c_on_example_packet( cl, id, s );
					break;
				default:
					printf( "Unknown packet ID %i received\n", id );
			} });

        if (client.connect("localhost", "1337"))
        {
            printf("Connected to server!\n");

            // Craft a packet once we're connected
            fi::packets::example_packet example = {};

            example.some_short = 128;
            example.some_array = {1, 2, 3, 4, 5};
            example.some_string_array = {"Hello", "from", "client!"};

            // Send the packet
            client.send_packet(&example);

            // Wait for the server to answer
            while (client.is_connected())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        else
            printf("Handshake has failed.\n");

        std::cin.get();
        return 0;
    }
    catch (const fi::async_tcp_client::exception &e)
    {
        printf("%s\n", e.what());

        std::cin.get();
        return 1;
    }
}

int main_listener()
{
    try
    {
        fi::async_udp_listener listener = {};

        listener.register_stop_callback([](fi::async_udp_listener *const sv)
                                        { printf("Listener has been stopped.\n"); });

        listener.register_callback([](fi::async_udp_listener *const sv, SOCKET from, const fi::packets::packet_id id, fi::packets::detail::binary_serializer &s)
                                   {
			// You can use a switch case, an unordered map, an array.. whichever suits you best
			switch ( id ) {
				case fi::packets::id_example:
					l_on_example_packet( sv, from, id, s );
					break;
				default:
					printf( "Unknown packet ID %i received\n", id );
			} });

        // Attempt to start the server
        listener.start("1337");

        printf("Server running on port 1337.\n");

        // Wait for our server to stop running
        while (listener.is_running())
        {
            // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // std::cout << "do smth" << std::endl;
        }

        return 0;
    }
    catch (const fi::async_tcp_server::exception &e)
    {
        printf("%s\n", e.what());
        return 1;
    }
}

int main_talker()
{
    try
    {
        fi::async_udp_talker talker = {};

        talker.set_destination("localhost", "1337");
        std::cout << "destination set" << std::endl;

        // Craft a packet once we're connected
        fi::packets::example_packet example = {};

        example.some_short = 128;
        example.some_array = {1, 2, 3, 4, 5};
        example.some_string_array = {"Hello", "from", "client!"};

        // Send the packet
        talker.send_packet(&example);
        std::cout << "packet sent" << std::endl;

        std::cin.get();
        return 0;
    }
    catch (const fi::async_tcp_client::exception &e)
    {
        printf("%s\n", e.what());

        std::cin.get();
        return 1;
    }
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        std::cout << "server: 0, client: 1; listener: 2, talker: 3" << std::endl;
        return 1;
    }
    char c = *argv[1];
    if (c == '0')
        return main_server();
    if (c == '1')
        return main_client();
    if (c == '2')
        return main_listener();
    if (c == '3')
        return main_talker();

    std::cout << "unknown param" << std::endl;
    return 1;
}