# Media transport layer -- header-only (INTERFACE) library.
# All implementation is in headers. The .h files contain inline functions
# for the stateless utilities (socket_address_parser, network_util) and
# class methods for the transport components.

add_library(caeron_media INTERFACE)
target_include_directories(caeron_media INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(caeron_media INTERFACE
    caeron_common
    caeron_concurrent
    caeron_protocol
    caeron_platform_posix
)
