#include <iostream>
#include <string>
#include <vector>

#include "ppp.hpp"
#include "packet.hpp"
#include "log.hpp"
#include "string_helpers.hpp"
#include "encap.hpp"
#include "net_integer.hpp"
#include "runtime.hpp"
#include "aaa.hpp"

extern std::shared_ptr<PPPOERuntime> runtime;

std::string ppp::processPPP( std::vector<uint8_t> &inPkt, const encapsulation_t &encap ) {
    PPPOESESSION_HDR *pppoe = reinterpret_cast<PPPOESESSION_HDR*>( inPkt.data() );

    // Determine this session
    uint16_t sessionId = bswap( pppoe->session_id );
    pppoe_key_t key{ encap.source_mac, sessionId, encap.outer_vlan, encap.inner_vlan };
    runtime->logger->logDebug() << LOGS::PPP << "Looking up for session: " << key << std::endl;

    auto const &sessionIt = runtime->activeSessions.find( key );
    if( sessionIt == runtime->activeSessions.end() ) {
        // 这是正常情况：会话已删除（PADT），但有延迟数据包到达
        // 使用 DEBUG 级别，避免日志噪音
        runtime->logger->logDebug() << LOGS::PPP << "Session not found in runtime (likely deleted). Key: " << key 
                                     << ", SessionID: " << sessionId 
                                     << ", Active sessions: " << runtime->activeSessions.size() << std::endl;
        return "";  // 返回空字符串，不触发上层错误日志
    }

    auto &session = sessionIt->second;
    if( !session->started ) {
        session->lcp.open();
        session->lcp.layer_up();
        session->started = true;
    }

    PPP_LCP *lcp = reinterpret_cast<PPP_LCP*>( pppoe->data );

    runtime->logger->logDebug() << LOGS::PPP << "proto " << static_cast<PPP_PROTO>( bswap( pppoe->ppp_protocol ) ) << " for session " << session->session_id << std::endl;

    switch( static_cast<PPP_PROTO>( bswap( pppoe->ppp_protocol ) ) ) {
    case PPP_PROTO::LCP:
        if( auto const& [ action, err ] = session->lcp.receive( inPkt ); !err.empty() ) {
            runtime->logger->logError() << LOGS::PPP << "Error while processing LCP packet: " << err << std::endl;
        } else {
            if( action == PPP_FSM_ACTION::LAYER_UP ) {
                if( runtime->lcp_conf->authCHAP ) {
                    session->chap.open();
                } else if( runtime->lcp_conf->authPAP ) {
                    session->auth.open();
                } else {
                    // No authentication
                    session->ipcp.open();
                    session->ipcp.layer_up();
                }
            } else if( action == PPP_FSM_ACTION::LAYER_DOWN ) {
                runtime->logger->logError() << LOGS::PPP << "LCP goes down, terminate session..." << std::endl;
                if( auto const &err = runtime->deallocateSession( session->session_id ); !err.empty() ) {
                    return "Cannot terminate session: " + err;
                }
            }
        }
        break;
    case PPP_PROTO::PAP:
        if( auto const& [ action, err ] = session->auth.receive( inPkt ); !err.empty() ) {
            runtime->logger->logDebug() << LOGS::PPP << "Error while processing LCP packet: " << err << std::endl;
        }
        break;
    case PPP_PROTO::CHAP:
        if( auto const& [ action, err ] = session->chap.receive( inPkt ); !err.empty() ) {
            runtime->logger->logDebug() << LOGS::PPP << "Error while processing LCP packet: " << err << std::endl;
        }
        break;
    case PPP_PROTO::IPCP:
        if( auto const &[ action, err ] = session->ipcp.receive( inPkt ); !err.empty() ) {
            runtime->logger->logError() << LOGS::PPP << "Error while processing IPCP pkt: " << err << std::endl;
        } else {
            if( action == PPP_FSM_ACTION::LAYER_UP ) {
                runtime->logger->logInfo() << LOGS::PPP << "IPCP is opened: configuring vpp" << std::endl;
                if( auto const &err = session->provision_dp(); !err.empty() ) {
                    runtime->logger->logError() << LOGS::PPP << "Cannot get ip config for session: " << err << std::endl;
                }
                runtime->aaa->mapIfaceToSession( session->aaa_session_id, session->ifindex );
                session->startEcho(); // Start LCP Echo mechanism to detect dead sessions
            }
        }
        break;
    default:
        runtime->logger->logError() << LOGS::PPP << "Unknown PPP proto: rejecting by default" << std::endl;
        lcp->code = LCP_CODE::CODE_REJ;

        auto header = session->encap.generate_header( runtime->hwaddr, ETH_PPPOE_SESSION );
        inPkt.insert( inPkt.begin(), header.begin(), header.end() );

        // Send this CONF REQ
        runtime->ppp_outcoming.push( std::move( inPkt ) );
    }

    return "";
}
