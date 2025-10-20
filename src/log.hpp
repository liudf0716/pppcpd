#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>
#include <fstream>
#include <memory>
#include <iomanip>
#include <chrono>
#include <ctime>

enum class LOGL: uint8_t {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    ALERT
};

enum class LOGS: uint8_t {
    MAIN,
    PACKET,
    PPPOED,
    PPP,
    LCP,
    IPCP,
    CHAP,
    PPP_AUTH,
    AAA,
    RADIUS,
    VPP,
    SESSION
};

std::ostream& operator<<( std::ostream &os, const LOGL &l );
std::ostream& operator<<( std::ostream &os, const LOGS &l );

class Logger {
private:
    std::unique_ptr<std::ofstream> file_stream;  // 持有文件流（如果输出到文件）
    std::ostream &os;
    LOGL minimum;
    bool noop;
    using endl_type = decltype( std::endl<char, std::char_traits<char>> );

    Logger& printTime() {
        auto in_time_t = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
        *this << std::put_time( std::localtime( &in_time_t ), "%Y-%m-%d %X: ");
        return *this;
    }

public:
    // 默认构造函数：输出到 cout
    Logger():
        file_stream( nullptr ),
        os( std::cout ),
        minimum( LOGL::INFO ),
        noop( false )
    {}

    // 构造函数：输出到指定的 ostream
    Logger( std::ostream &o ):
        file_stream( nullptr ),
        os( o ),
        minimum( LOGL::INFO ),
        noop( false )
    {}

    // 构造函数：输出到文件
    Logger( const std::string &filename ):
        file_stream( std::make_unique<std::ofstream>( filename, std::ios::app ) ),
        os( *file_stream ),
        minimum( LOGL::INFO ),
        noop( false )
    {
        if( !file_stream->is_open() ) {
            throw std::runtime_error( "Cannot open log file: " + filename );
        }
    }

    void setLevel( const LOGL &level ) {
        minimum = level;
    }

    Logger& operator<<( std::ostream& (*fun)( std::ostream& ) ) {
        if( !noop ) {
            os << std::endl;
            os.flush();  // 确保立即写入文件
        }
        noop = false;
        return *this;
    }

    template<typename T>
    Logger& operator<<( const T& data ) {
        if( !noop ) {
            os << data;
        }
        return *this;
    }

    Logger& logInfo() {
        if( minimum > LOGL::INFO ) {
            noop = true;
        }
        return printTime();
    }

    Logger& logDebug() {
        if( minimum > LOGL::DEBUG ) {
            noop = true;
        }
        return printTime();
    }

    Logger& logError() {
        if( minimum > LOGL::ERROR ) {
            noop = true;
        }
        return printTime();
    }

    Logger& logAlert() {
        if( minimum > LOGL::ALERT ) {
            noop = true;
        }
        return printTime();
    }
};

void log( const std::string &msg );

#endif