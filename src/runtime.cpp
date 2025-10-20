#include <memory>
#include <string>
#include <fstream>
#include <chrono>
#include <yaml-cpp/yaml.h>

#include "runtime.hpp"
#include "log.hpp"
#include "string_helpers.hpp"
#include "yaml.hpp"
#include "aaa.hpp"
#include "ethernet.hpp"
#include "encap.hpp"
#include "vpp_types.hpp"
#include "vpp.hpp"
#include "session.hpp"

PPPOERuntime::PPPOERuntime( std::string cp, io_service &i ) : 
    conf_path( cp ),
    io( i )
{
    // 先用默认 logger 加载配置
    logger = std::make_unique<Logger>();
    reloadConfig();
    
    // 根据配置重新初始化 logger
    if( !conf.log_file.empty() ) {
        try {
            logger = std::make_unique<Logger>( conf.log_file );
            logger->setLevel( conf.log_level );
            logger->logInfo() << LOGS::MAIN << "Log output redirected to file: " << conf.log_file << std::endl;
        } catch( const std::exception &e ) {
            // 如果打开文件失败，回退到控制台输出
            logger = std::make_unique<Logger>();
            logger->setLevel( conf.log_level );
            logger->logError() << LOGS::MAIN << "Failed to open log file " << conf.log_file 
                               << ": " << e.what() << ", using console output" << std::endl;
        }
    } else {
        logger->setLevel( conf.log_level );
    }
    
    aaa = std::make_shared<AAA>( io, conf.aaa_conf );

    logger->logInfo() << LOGS::MAIN << "Starting PPP control plane daemon..." << std::endl;
    vpp = std::make_shared<VPPAPI>( io, logger );
    for( auto const &tapid: vpp->get_tap_interfaces() ) {
        logger->logInfo() << LOGS::MAIN << "Deleting TAP interface with id " << tapid << std::endl;
        auto ret = vpp->delete_tap( tapid );
        if( !ret ) {
            logger->logError() << LOGS::VPP << "Cannot delete tap interface with ifindex: " << tapid << std::endl;
        }
    }
    if( auto const &[ ret, ifi ] = vpp->create_tap( conf.tap_name ); ret ) {
        std::string path { "/proc/sys/net/ipv6/conf/" + conf.tap_name + "/disable_ipv6" };
        std::ofstream dis_ipv6 { path };
        if( dis_ipv6.is_open() ) {
            dis_ipv6 << "1";
            dis_ipv6.close();
        }
        if( !vpp->set_state( ifi, true ) ) {
            logger->logError() << LOGS::VPP << "Cannot set state to interface: " << ifi << std::endl;
        }
        if( !vpp->add_pppoe_cp( ifi ) ) {
            logger->logError() << LOGS::VPP << "Cannot set pppoe cp interface: " << ifi << std::endl;
        }
    }
    for( auto const &vrf: vpp->dump_vrfs() ) {
        if( vrf.table_id == 0 ) {
            // keep default table
            continue;
        }
        logger->logInfo() << LOGS::MAIN << "Deleting VRF " << vrf.name << " with table id: " << vrf.table_id << std::endl;
        if( !vpp->set_vrf( vrf.name, vrf.table_id, false ) ) {
            logger->logError() << LOGS::MAIN << "Cannot delete VRF " << vrf.name << std::endl;
        }
    }
    for( auto const &vrf: conf.vrfs ) {
        if( !vpp->set_vrf( vrf.name, vrf.table_id ) ) {
            logger->logError() << LOGS::MAIN << "Cannot create VRF" << vrf.name << std::endl;
            continue;
        }
        for( auto const &route: vrf.rib.entries ) {
            if( auto const &[ ret, rid ] = vpp->add_route( route.destination, route.nexthop, vrf.table_id ); !ret ) {
                logger->logError() << LOGS::MAIN << "Cannot add route " << route.destination.to_string() << 
                    " via " << route.nexthop.to_string() << " in VRF " << vrf.name << std::endl;
            }
        }
    }
    for( auto const &el: vpp->get_ifaces() ) {
        for( auto const &el: vpp->dump_ip( el.sw_if_index ) ) {
            logger->logInfo() << LOGS::VPP << "Clearing IP on interface " << el.sw_if_index << " addr: " << el.address.to_string() << std::endl;
            vpp->set_ip( el.sw_if_index, el.address, false );
        }
        for( auto const &el: vpp->dump_unnumbered( el.sw_if_index ) ) {
            logger->logInfo() << LOGS::VPP << "Clearing unnumbered on interface " << el.unnumbered_sw_if_index << " IP iface: " << el.iface_sw_if_index << std::endl;
            vpp->set_unnumbered( el.unnumbered_sw_if_index, el.iface_sw_if_index, false );
        }
        if( el.type == IfaceType::SUBIF ) {
            logger->logInfo() << LOGS::VPP << "Deleting subinterface: " << el << std::endl;
            vpp->del_subif( el.sw_if_index );
            continue;
        }
        logger->logInfo() << LOGS::VPP << "Dumped interface: " << el << std::endl;
    }
    vpp->setup_interfaces( conf.interfaces );

    for( auto &rib_entry: conf.global_rib.entries ) {
        if( auto const &[ success, rid ] = vpp->add_route( rib_entry.destination, rib_entry.nexthop, 0 ); success ) {
            rib_entry.rid_in_vpp = rid;
        }
    }
}

void PPPOERuntime::reloadConfig() {
    try {
        YAML::Node config = YAML::LoadFile( conf_path );
        conf = config.as<PPPOEGlobalConf>();
    } catch( std::exception &e ) {
        logger->logError() << LOGS::MAIN << "Error on reloading config: " << e.what() << std::endl;
    }
}

bool operator<( const pppoe_key_t &l, const pppoe_key_t &r ) {
    return std::tie( l.session_id, l.outer_vlan, l.inner_vlan, l.mac ) < std::tie( r.session_id, r.outer_vlan, r.inner_vlan, r.mac );
}

bool operator<( const pppoe_conn_t &l, const pppoe_conn_t &r ) {
    return std::tie( l.cookie, l.outer_vlan, l.inner_vlan, l.mac ) < std::tie( r.cookie, r.outer_vlan, r.inner_vlan, r.mac );
}

std::string PPPOERuntime::pendeSession( mac_t mac, uint16_t outer_vlan, uint16_t inner_vlan, const std::string &cookie ) {
    pppoe_conn_t key { mac, outer_vlan, inner_vlan, cookie };

    if( auto const &[it, ret ] = pendingSession.emplace( key ); !ret ) {
        return { "Cannot allocate new Pending session" };
    }

    auto timer_to_delete = std::make_shared<boost::asio::steady_timer>( io, boost::asio::chrono::seconds( 10 ) );
    timer_to_delete->async_wait( std::bind( &PPPOERuntime::clearPendingSession, this, timer_to_delete, key ) );
    return {};
}

bool PPPOERuntime::checkSession( mac_t mac, uint16_t outer_vlan, uint16_t inner_vlan, const std::string &cookie ) {
    pppoe_conn_t key { mac, outer_vlan, inner_vlan, cookie };

    if( auto const &it = pendingSession.find( key ); it != pendingSession.end() ) {
        pendingSession.erase( it );
        return true;
    }
    return false;
}

std::tuple<uint16_t,std::string> PPPOERuntime::allocateSession( const encapsulation_t &encap ) {
    // Start searching from next_session_id for better performance
    uint16_t start_id = next_session_id;
    uint16_t current_id = start_id;
    
    do {
        if( current_id == 0 ) current_id = 1; // Skip 0 as it's invalid
        
        if( auto ret = sessionSet.find( current_id ); ret == sessionSet.end() ) {
            if( auto const &[ it, ret ] = sessionSet.emplace( current_id ); !ret ) {
                return { 0, "Cannot allocate session: cannot emplace value in set" };
            }
            
            // Create session as shared_ptr
            auto session = std::make_shared<PPPOESession>( io, encap, current_id );
            pppoe_key_t key{ encap, current_id };
            if( auto const &[ it, ret ] = activeSessions.emplace( key, session ); !ret ) {
                sessionSet.erase( current_id ); // Clean up on failure
                return { 0, "Cannot allocate session: cannot emplace new PPPOESession" };
            }
            
            // Warning only when approaching session limit
            if( activeSessions.size() > 60000 ) {
                logger->logError() << LOGS::MAIN << "High session count: " << activeSessions.size() 
                                     << " active sessions. Approaching maximum limit." << std::endl;
            }
            
            // Update next_session_id for next allocation
            next_session_id = current_id + 1;
            if( next_session_id == 0 ) next_session_id = 1;
            
            return { current_id, "" };
        }
        
        current_id++;
        if( current_id == 0 ) current_id = 1; // Wrap around, skip 0
        
    } while( current_id != start_id );
    
    // Critical error: all session IDs exhausted
    logger->logError() << LOGS::MAIN << "CRITICAL: Cannot allocate session - all session IDs exhausted! "
                       << "sessionSet.size=" << sessionSet.size()
                       << " activeSessions.size=" << activeSessions.size() << std::endl;
    
    return { 0, "Maximum of sessions" };
}

std::string PPPOERuntime::deallocateSession( uint16_t sid ) {
    auto const &it = sessionSet.find( sid );
    if( it == sessionSet.end() ) {
        logger->logError() << LOGS::MAIN << "Cannot find session " << sid << " in sessionSet" << std::endl;
        return "Cannot find session with this session id";
    }

    // Batch deallocation detection: check if multiple sessions are being released in a short time
    static auto last_dealloc_time = std::chrono::steady_clock::now();
    static size_t dealloc_count_in_window = 0;
    auto now = std::chrono::steady_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_dealloc_time).count();
    
    if (time_diff <= 10) {  // 10-second detection window
        dealloc_count_in_window++;
        if (dealloc_count_in_window >= 10) {  // Alert if 10+ deallocations in 10 seconds
            logger->logError() << LOGS::MAIN << "⚠️  BATCH DEALLOCATION DETECTED: " 
                               << dealloc_count_in_window << " sessions released in " 
                               << time_diff << " seconds!" << std::endl;
        }
    } else {
        dealloc_count_in_window = 1;  // Reset counter for new time window
        last_dealloc_time = now;
    }

    // More efficient: find session by iterating only once and using iterator
    auto session_it = std::find_if( activeSessions.begin(), activeSessions.end(),
        [sid]( const auto& pair ) { return pair.second->session_id == sid; } );
    
    if( session_it != activeSessions.end() ) {
        aaa->stopSession( session_it->second->aaa_session_id );
        activeSessions.erase( session_it );
    } else {
        logger->logError() << LOGS::MAIN << "Session " << sid << " found in sessionSet but not in activeSessions" << std::endl;
    }

    sessionSet.erase( it );
    return "";
}

void PPPOERuntime::clearPendingSession( std::shared_ptr<boost::asio::steady_timer> timer, pppoe_conn_t key ) {
    if( auto const &it = pendingSession.find( key ); it != pendingSession.end() ) {
        logger->logDebug() << LOGS::MAIN << "Deleting pending session due timeout: " << key << std::endl;
        pendingSession.erase( it );
    }
}

void PPPOERuntime::cleanup() {
    logger->logInfo() << LOGS::MAIN << "Starting cleanup process..." << std::endl;
    
    // 首先停止所有AAA会话
    if( aaa ) {
        aaa->stopAllSessions();
    }
    
    // 清理活动会话
    activeSessions.clear();
    sessionSet.clear();
    pendingSession.clear();
    
    logger->logInfo() << LOGS::MAIN << "Cleanup completed" << std::endl;
}