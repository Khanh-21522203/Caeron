add_library(caeron_logbuffer STATIC
    log_buffer_descriptor.cpp
    header_writer.cpp
    term_reader.cpp
    term_scanner.cpp
    term_rebuilder.cpp
    term_gap_scanner.cpp
    term_gap_filler.cpp
    term_block_scanner.cpp
    term_unblocker.cpp
    log_buffer_unblocker.cpp
    buffer_claim.cpp
)
target_include_directories(caeron_logbuffer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_logbuffer PUBLIC caeron_common caeron_concurrent caeron_protocol)
