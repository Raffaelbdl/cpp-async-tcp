cmake_minimum_required(VERSION 3.20)
project(tcp)

add_executable(tcp

    main.cpp

    client/async_client/async_client.cpp
    client/async_client/async_client.h

    server/async_server/async_server.cpp
    server/async_server/async_server.h

    shared/bin_serializer/bin_serializer.cpp
    shared/bin_serializer/bin_serializer.h
    shared/packets/packet_base.h
    shared/packets/packets.h
)
