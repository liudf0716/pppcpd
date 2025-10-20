#include "session.hpp"
#include "runtime.hpp"
#include "vpp_types.hpp"
#include "vpp.hpp"
#include <random>

extern std::shared_ptr<PPPOERuntime> runtime;

PPPOESession::PPPOESession( io_service &i, const encapsulation_t &e, uint16_t sid ): 
    io( i ),
    timer( io ),
    encap( e ),
    session_id( sid ),
    ifindex( UINT32_MAX ),
    lcp( *this ),
    auth( *this ),
    chap( *this ),
    ipcp( *this )
{
    runtime->logger->logDebug() << LOGS::MAIN << "Session UP: " << sid << std::endl;
}

PPPOESession::~PPPOESession() {
    timer.cancel(); // Cancel any pending timer operations
    deprovision_dp();
}

std::string PPPOESession::provision_dp() {
    if( auto const &[ ret, ifi ] = runtime->vpp->add_pppoe_session( address, session_id, encap.source_mac, vrf, true ); !ret ) {
        return "Cannot add new session to vpp ";
    } else {
        ifindex = ifi;
    }
    if( !vrf.empty() ) {
        if( !runtime->vpp->set_interface_table( ifindex, vrf ) ) {
            return "Cannot move new session to vrf";
        }
    }
    if( !unnumbered.empty() ) {
        auto [ sw_ifi, success ] = runtime->vpp->get_iface_by_name( unnumbered );
        if( !success ) {
            return "Cannot set unnumbered to new session: can't find interface with such name";
        }
        if( !runtime->vpp->set_unnumbered( ifindex, sw_ifi ) ) {
            return "Cannot set unnumbered to new session";
        }
    }
    return {};
}

std::string PPPOESession::deprovision_dp() {
    for( auto const &el: runtime->vpp->dump_unnumbered( ifindex ) ) {
        runtime->vpp->set_unnumbered( el.unnumbered_sw_if_index, el.iface_sw_if_index, false );
    }
    if( auto const &[ ret, ifi ] = runtime->vpp->add_pppoe_session( address, session_id, encap.source_mac, vrf, false ); !ret ) {
        return "Cannot delete session from vpp ";
    }
    return {};
}

void PPPOESession::startEcho() {
    // 添加随机抖动：25秒 ± 5秒（20-30秒）
    // 避免所有会话的 Echo 定时器同步，分散流量
    // 大幅降低对端压力，适合 1000+ 并发场景
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(20, 30);
    int random_interval = dis(gen);
    
    timer.expires_from_now( std::chrono::seconds( random_interval ) );
    timer.async_wait( std::bind( &PPPOESession::sendEchoReq, shared_from_this(), std::placeholders::_1 ) );
}

void PPPOESession::sendEchoReq( const boost::system::error_code& ec ) {
    if( ec ) {
        runtime->logger->logError() << LOGS::SESSION << "Error on timer for LCP ECHO REQ: " << ec.message() << std::endl;
        return;
    }

    uint8_t counter_before = lcp.get_echo_counter();
    auto const& [ action, err ] = lcp.send_echo_req();
    uint8_t counter_after = lcp.get_echo_counter();
    
    if( !err.empty() ) {
        runtime->logger->logError() << LOGS::SESSION << "LCP Echo failed for session " << session_id 
                                     << ": " << err << std::endl;
    }
    
    // 只在接近失败阈值时警告（echo_counter > 5）
    if( counter_after > 5 && counter_after > counter_before ) {
        runtime->logger->logInfo() << LOGS::SESSION 
            << "High echo_counter for session " << session_id 
            << ": " << static_cast<int>(counter_after) << std::endl;
    }
    
    if( action == PPP_FSM_ACTION::LAYER_DOWN ) {
        runtime->logger->logError() << LOGS::SESSION 
            << "LCP Echo timeout for session " << session_id 
            << " (echo_counter=" << static_cast<int>(counter_after)
            << ") - Terminating session" << std::endl;
        runtime->deallocateSession( session_id );
        return; // Don't restart the timer
    }

    startEcho();
}