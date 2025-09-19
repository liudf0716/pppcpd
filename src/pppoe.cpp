#include <string>
#include <vector>
#include <map>

#include "pppoe.hpp"
#include "encap.hpp"
#include "utils.hpp"
#include "runtime.hpp"

extern std::shared_ptr<PPPOERuntime> runtime;

uint8_t pppoe::insertTag( std::vector<uint8_t> &pkt, PPPOE_TAG tag, const std::string &val ) {
    std::vector<uint8_t> tagvec;
    tagvec.resize( 4 );
    auto tlv = reinterpret_cast<PPPOEDISC_TLV*>( tagvec.data() );
    tlv->type = bswap( static_cast<uint16_t>( tag ) );
    tlv->length = bswap( (uint16_t)val.size() );
    tagvec.insert( tagvec.end(), val.begin(), val.end() );
    pkt.insert( pkt.end(), tagvec.begin(), tagvec.end() );     

    return tagvec.size();
}

std::tuple<std::map<PPPOE_TAG,std::string>,std::string> pppoe::parseTags( std::vector<uint8_t> &pkt ) {
    std::map<PPPOE_TAG,std::string> tags;
    PPPOEDISC_TLV *tlv = nullptr;
    auto offset = pkt.data();
    while( true ) {
        tlv = reinterpret_cast<PPPOEDISC_TLV*>( offset );
        auto tag = PPPOE_TAG { ntohs( tlv->type ) };
        auto len = ntohs( tlv->length );
        std::string val;

        if( tag == PPPOE_TAG::END_OF_LIST ) {
            return { std::move( tags ), "" };   
        }

        if( len > 0 ) {
            val = std::string { reinterpret_cast<char*>( &tlv->value ), len };
        }

        if( auto const &[ it, ret ] = tags.emplace( tag, val ); !ret ) {
            return { std::move( tags ), "Cannot insert tag " + std::to_string( ntohs( tlv->type ) ) + " in tag map" };
        }

        offset += 4 + len;
        if( offset >= pkt.end().base() ) {
            break;
        }
    }
    return { std::move( tags ), "" };
}

static std::string process_padi( std::vector<uint8_t> &inPkt, std::vector<uint8_t> &outPkt, const encapsulation_t &encap ) {
    runtime->logger->logDebug() << LOGS::PPPOED << "Processing PADI packet" << std::endl;

    outPkt.resize( sizeof( PPPOEDISC_HDR ) );
    PPPOEDISC_HDR *rep_pppoe = reinterpret_cast<PPPOEDISC_HDR*>( outPkt.data() );
    
    rep_pppoe->type = 1;
    rep_pppoe->version = 1;
    rep_pppoe->session_id = 0;
    rep_pppoe->length = 0;    
    rep_pppoe->code = PPPOE_CODE::PADO;
    
    if( inPkt.size() < sizeof( PPPOEDISC_HDR ) ) {
        return "Too small packet";
    }
    inPkt.erase( inPkt.begin(), inPkt.begin() + sizeof( PPPOEDISC_HDR) );
    auto [ tags, err ] = pppoe::parseTags( inPkt );
    if( !err.empty() ) {
        return "Cannot process PADI: " + err;
    }

    auto const &pol_it = runtime->conf.pppoe_confs.find( encap.outer_vlan );
    const PPPOEPolicy &pppoe_conf = ( pol_it == runtime->conf.pppoe_confs.end() ) ? runtime->conf.default_pppoe_conf : pol_it->second;

    // Inserting tags
    uint16_t taglen { 0 };

    // At first we need to insert AC NAME
    taglen += pppoe::insertTag( outPkt, PPPOE_TAG::AC_NAME, pppoe_conf.ac_name );

    // Check for HOST UNIQ
    if( auto const &tagIt = tags.find( PPPOE_TAG::HOST_UNIQ ); tagIt != tags.end() ) {
        taglen += pppoe::insertTag( outPkt, PPPOE_TAG::HOST_UNIQ, tagIt->second );
    }

    // Check for SERVICE NAME
    if( auto const &tagIt = tags.find( PPPOE_TAG::SERVICE_NAME ); tagIt != tags.end() ) {
        std::string selected_service;

        for( auto const &service: pppoe_conf.service_name ) {
            if( service == tagIt->second ) {
                selected_service = tagIt->second;
                break;
            }
        }

        if( selected_service.empty() && pppoe_conf.ignore_service_name ) {
            selected_service = tagIt->second;
        }

        if( selected_service.empty() && !pppoe_conf.ignore_service_name ) {
            return "Wrong service name";
        }

        taglen += pppoe::insertTag( outPkt, PPPOE_TAG::SERVICE_NAME, selected_service );
    }

    // Check our policy if we need to insert AC COOKIE
    std::string cookie;
    if( pppoe_conf.insert_cookie ) {
        cookie = random_string( 16 );
        taglen += pppoe::insertTag( outPkt, PPPOE_TAG::AC_COOKIE, cookie );
    }

    if( auto const & err = runtime->pendeSession( encap.source_mac, encap.outer_vlan, encap.inner_vlan, cookie); !err.empty() ) {
        return "Cannot pende session: " + err;
    }

    rep_pppoe = reinterpret_cast<PPPOEDISC_HDR*>( outPkt.data() );
    rep_pppoe->length = bswap( taglen );

    outPkt.resize( sizeof( PPPOEDISC_HDR ) + taglen );

    return {};
}

static std::string process_padr( std::vector<uint8_t> &inPkt, std::vector<uint8_t> &outPkt, const encapsulation_t &encap ) {
    runtime->logger->logDebug() << LOGS::PPPOED << "Processing PADR packet" << std::endl;
        
    outPkt.resize( sizeof( PPPOEDISC_HDR ) );
    
    PPPOEDISC_HDR *rep_pppoe = reinterpret_cast<PPPOEDISC_HDR*>( outPkt.data() );

    rep_pppoe->type = 1;
    rep_pppoe->version = 1;
    rep_pppoe->session_id = 0;
    rep_pppoe->length = 0;
    rep_pppoe->code = PPPOE_CODE::PADS;

    if( inPkt.size() < sizeof( PPPOEDISC_HDR ) ) {
        return "Too small packet";
    }
    inPkt.erase( inPkt.begin(), inPkt.begin() + sizeof( PPPOEDISC_HDR) );
    auto [ tags, err ] = pppoe::parseTags( inPkt );
    if( !err.empty() ) {
        return "Cannot process PADR: " + err;
    }

    std::string cookie;
    if( auto const &it = tags.find( PPPOE_TAG::AC_COOKIE ); it != tags.end() ) {
        cookie = it->second;
    }

    if( !runtime->checkSession( encap.source_mac, encap.outer_vlan, encap.inner_vlan, cookie ) ) {
        return "We don't expect this session";
    }

    if( auto const &[ sid, err ] = runtime->allocateSession( encap ); !err.empty() ) {
        return "Cannot process PADR: " + err;
    } else {
        rep_pppoe->session_id = bswap( sid );
    }

    uint16_t taglen { 0 };

    // Check for SERVICE NAME
    if( auto const &tagIt = tags.find( PPPOE_TAG::SERVICE_NAME ); tagIt != tags.end() ) {
        taglen += pppoe::insertTag( outPkt, PPPOE_TAG::SERVICE_NAME, tagIt->second );
    }

    // Check for HOST UNIQ
    if( auto const &tagIt = tags.find( PPPOE_TAG::HOST_UNIQ ); tagIt != tags.end() ) {
        taglen += pppoe::insertTag( outPkt, PPPOE_TAG::HOST_UNIQ, tagIt->second );
    }

    rep_pppoe = reinterpret_cast<PPPOEDISC_HDR*>( outPkt.data() );
    rep_pppoe->length = bswap( taglen );

    outPkt.resize( sizeof( PPPOEDISC_HDR ) + taglen );
    
    return {};
}

std::string pppoe::processPPPOE( std::vector<uint8_t> &inPkt, const encapsulation_t &encap ) {
    std::vector<uint8_t> outPkt;
    outPkt.reserve( sizeof( PPPOEDISC_HDR ) + 128 );

    PPPOEDISC_HDR *disc = reinterpret_cast<PPPOEDISC_HDR*>( inPkt.data() );

    runtime->logger->logDebug() << LOGS::PPPOED << "Incoming PPPoE: " << disc << std::endl;
    
    // Starting to prepare the answer
    switch( disc->code ) {
    case PPPOE_CODE::PADI:
        if( auto const &err = process_padi( inPkt, outPkt, encap ); !err.empty() ) {
            return err;
        }
        break;
    case PPPOE_CODE::PADR:
        if( auto const &err = process_padr( inPkt, outPkt, encap ); !err.empty() ) {
            return err;
        }
        break;
    case PPPOE_CODE::PADT:
        runtime->logger->logDebug() << LOGS::PPPOED << "Processing PADT packet" << std::endl;
        runtime->deallocateSession( bswap( disc->session_id ) );
        return "Received PADT, send nothing";
    default:
        runtime->logger->logDebug() << LOGS::PPPOED << "Incorrect code for packet" << std::endl;
        return "Incorrect code for packet";
    }

    disc = reinterpret_cast<PPPOEDISC_HDR*>( outPkt.data() );

    auto header = encap.generate_header( runtime->hwaddr, ETH_PPPOE_DISCOVERY );
    outPkt.insert( outPkt.begin(), header.begin(), header.end() );

    runtime->pppoe_outcoming.push( std::move( outPkt ) );

    return {};
}
