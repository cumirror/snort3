
add_library(preprocessor_states
    pps_arpspoof.cc
    pps_bo.cc
    pps_frag3_engine.cc
    pps_frag3_global.cc
    pps_ftp_telnet.cc
    pps_ftp_telnet_protocol.cc
    pps_gtp.cc
    pps_http_inspect.cc
    pps_http_inspect_server.cc
    pps_normalizers.cc
    pps_perfmonitor.cc
    pps_rpc_decode.cc
    pps_ssh.cc
    pps_dns.cc
    pps_pop.cc
    pps_imap.cc
    pps_sfportscan.cc
    pps_stream5_ip.cc
    pps_stream5_global.cc
    pps_stream5_tcp.cc
    pps_stream5_udp.cc
    preprocessor_api.h
    preprocessor_api.cc
)

target_link_libraries( preprocessor_states
    snort2lua_helpers
    conversion_data
)
