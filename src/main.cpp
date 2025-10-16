#include <memory>
#include <string>
#include <fstream>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>

#include <yaml-cpp/yaml.h>
#include "yaml.hpp"

#include "main.hpp"
#include "runtime.hpp"
#include "evloop.hpp"
#include "cli.hpp"

// Some global vars
std::shared_ptr<PPPOERuntime> runtime;
std::atomic_bool interrupted { false };

static void conf_init() {
    PPPOEGlobalConf global_conf;

    global_conf.tap_name = "tap0";
    global_conf.log_level = LOGL::INFO;  // Default log level

    global_conf.default_pppoe_conf.ac_name = "vBNG AC PPPoE";
    global_conf.default_pppoe_conf.insert_cookie = true;
    global_conf.default_pppoe_conf.ignore_service_name = true;
    global_conf.default_pppoe_conf.service_name = { "inet", "pppoe" };

    PPPOELocalTemplate pppoe_template;
    pppoe_template.framed_pool = "pppoe_pool1";
    pppoe_template.dns1 = address_v4_t::from_string( "8.8.8.8" );
    pppoe_template.dns2 = address_v4_t::from_string( "1.1.1.1" );
    pppoe_template.unnumbered = "G1.150";
    global_conf.pppoe_templates.emplace( "template1", pppoe_template );

    pppoe_template.framed_pool = "vrf_pool1";
    pppoe_template.vrf = "RED";
    pppoe_template.unnumbered = "G1.250";
    global_conf.pppoe_templates.emplace( "template2", pppoe_template );

    global_conf.aaa_conf.local_template = "template1";
    global_conf.aaa_conf.method = { AAA_METHODS::NONE, AAA_METHODS::RADIUS  };
    global_conf.aaa_conf.pools.emplace( std::piecewise_construct,
        std::forward_as_tuple( "pppoe_pool1" ),
        std::forward_as_tuple( "100.64.0.10", "100.64.255.255" ) );
    global_conf.aaa_conf.pools.emplace( std::piecewise_construct,
        std::forward_as_tuple( "vrf_pool1" ),
        std::forward_as_tuple( "100.66.0.10", "100.66.0.255" ) );

    global_conf.aaa_conf.dictionaries = {
        "/usr/share/freeradius/dictionary.rfc2865",
        "/usr/share/freeradius/dictionary.rfc2866",
        "/usr/share/freeradius/dictionary.rfc2869",
        "/usr/share/freeradius/dictionary.ericsson.ab"
    };

    global_conf.aaa_conf.auth_servers.emplace( std::piecewise_construct,
        std::forward_as_tuple( "main_auth_1" ),
        std::forward_as_tuple( "127.0.0.1", 1812, "testing123" ) );
    
    global_conf.aaa_conf.acct_servers.emplace( std::piecewise_construct,
        std::forward_as_tuple( "main_acct_1" ),
        std::forward_as_tuple( "127.0.0.1", 1813, "testing123" ) );

    // LCP configuration
    global_conf.lcp_conf.insertMagicNumber = true;
    global_conf.lcp_conf.MRU = 1492;
    global_conf.lcp_conf.authCHAP = true;   // Use CHAP authentication by default
    global_conf.lcp_conf.authPAP = false;  // Set to true to use PAP instead

    {
        InterfaceConf iconf;
        iconf.device = "G0";
        iconf.mtu.emplace( 1500 );

        InterfaceUnit unit;
        unit.vlan = 200;
        unit.admin_state = true;
        iconf.units.emplace( 200, unit );

        unit.vlan = 201;
        iconf.units.emplace( 201, unit );

        unit.vlan = 202;
        iconf.units.emplace( 202, unit );

        global_conf.interfaces.push_back( std::move( iconf ) );
    }

    {
        InterfaceConf iconf;
        iconf.device = "G1";
        iconf.mtu.emplace( 1500 );

        InterfaceUnit unit;
        unit.address.emplace( boost::asio::ip::make_network_v4( "10.0.0.2/24" ) );
        unit.admin_state = true;
        unit.vlan = 150;
        iconf.units.emplace( 150, unit );

        unit.address.emplace( boost::asio::ip::make_network_v4( "10.10.0.2/24" ) );
        unit.vlan = 250;
        unit.vrf = "RED";
        iconf.units.emplace( 250, unit );

        global_conf.interfaces.push_back( std::move( iconf ) );
    }

    {
        StaticRIBEntry rib_entry;
        rib_entry.destination = boost::asio::ip::make_network_v4( "0.0.0.0/0" );
        rib_entry.nexthop = boost::asio::ip::make_address_v4( "10.0.0.1" );
        rib_entry.description = "default gateway";
        global_conf.global_rib.entries.push_back( std::move( rib_entry ) );
    }

    {
        VRFConf vrf;
        vrf.name = "RED";
        vrf.table_id = 10;
        StaticRIBEntry rib_entry;
        rib_entry.destination = boost::asio::ip::make_network_v4( "0.0.0.0/0" );
        rib_entry.nexthop = boost::asio::ip::make_address_v4( "10.10.0.1" );
        rib_entry.description = "default gateway";
        vrf.rib.entries.push_back( std::move( rib_entry ) );
        global_conf.vrfs.push_back( vrf );
    }

    YAML::Node config;
    config = global_conf;

    std::ofstream fout("config.yaml");
    fout << config << std::endl;
}

int main( int argc, char *argv[] ) {
    std::string path_config { "config.yaml" };

    boost::program_options::options_description desc {
        "PPPoE control plane daemon.\n"
        "This daemon works with VPP PPPoE Plugin to process PPPoE connections. All configuration is available through config file. You can generate sample configuration to see all the parameters.\n"
        "\n"
        "Arguments"
    };

    desc.add_options()
    ( "path,p", boost::program_options::value( &path_config), "Path to config: default is \"config.yaml\"" )
    ( "genconf,g", "Generate a sample configuration" )
    ( "help,h", "Print this message" );

    try {
        boost::program_options::variables_map vm;
        boost::program_options::store( boost::program_options::parse_command_line( argc, argv, desc ), vm );

        if( vm.count( "help" ) ) {  
            std::cout << desc << "\n";
            return 0;
        }

        if( vm.count( "genconf" ) ) {
            conf_init();
            return 0;
        }
    } catch( std::exception &e ) {
        std::cerr << "Error on parsing arguments: " << e.what() << std::endl;
        return -1;
    }

    io_service io;
    runtime = std::make_shared<PPPOERuntime>( path_config, io );

    // LCP options from configuration file
    runtime->lcp_conf = std::make_shared<LCPPolicy>( runtime->conf.lcp_conf );

    EVLoop loop( io );
    std::remove( "/var/run/pppcpd.sock" );
    CLIServer cli { io, "/var/run/pppcpd.sock" };

    while( !interrupted ) {
        io.run();
    }

    // 显式清理，确保正确的析构顺序
    if( runtime ) {
        runtime->cleanup();
        runtime.reset();
    }

    return 0;
}
