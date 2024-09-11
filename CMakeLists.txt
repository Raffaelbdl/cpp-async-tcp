cmake_minimum_required(VERSION 3.20)
project(tcp)

add_executable(tcp

    main.cpp

    client/async_client/async_client.cpp
    client/async_client/async_client.h

    server/async_server/async_server.cpp
    server/async_server/async_server.h

    listener/async_listener/async_listener.cpp
    listener/async_listener/async_listener.h

    talker/async_talker/async_talker.cpp
    talker/async_talker/async_talker.h

    shared/bin_serializer/bin_serializer.cpp
    shared/bin_serializer/bin_serializer.h
    shared/packets/packet_base.h
    shared/packets/packets.h
)
