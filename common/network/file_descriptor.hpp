/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Ported this from Mahimahi */

#ifndef FILE_DESCRIPTOR_HPP
#define FILE_DESCRIPTOR_HPP

#include <string>
#include <unistd.h>
#include <fcntl.h>

#include "exception.hpp"

using namespace std;

/* Unix file descriptors (sockets, files, etc.) */
class FileDescriptor
{
private:
    int fd_;
    bool eof_;

    unsigned int read_count_, write_count_;


protected:
    void register_read( void ) { read_count_++; }
    void register_write( void ) { write_count_++; }
    void set_eof( void ) { eof_ = true; }

    /* maximum size of a read */
    const static size_t BUFFER_SIZE = 1024 * 1024;

public:
    /* construct from fd number */
    FileDescriptor( const int fd )
    : fd_( fd ),
      eof_( false ),
      read_count_( 0 ),
      write_count_( 0 )
    {
        if ( fd_ <= 2 ) { /* make sure not overwriting stdout/stderr */
            throw unix_error( "FileDescriptor: fd <= 2" );
        }

        /* set close-on-exec flag so our file descriptors
        aren't passed on to unrelated children (like a shell) */
        SystemCall( "fcntl FD_CLOEXEC", fcntl( fd_, F_SETFD, FD_CLOEXEC ) );
    }

    /* move constructor */
    FileDescriptor( FileDescriptor && other )
        : fd_( other.fd_ ),
        eof_( other.eof_ ),
        read_count_( other.read_count_ ),
        write_count_( other.write_count_ )
    {
        /* mark other file descriptor as inactive */
        other.fd_ = -1;
    }

    /* destructor */
    virtual ~FileDescriptor()
    {
        if ( fd_ < 0 ) { /* has already been moved away */
            return;
        }

        try {
            close( fd_ );
        } catch ( const exception & e ) { /* don't throw from destructor */
            print_exception( e );
        }
    }

    /* accessors */
    const int & fd_num( void ) const { return fd_; }
    const bool & eof( void ) const { return eof_; }
    unsigned int read_count( void ) const { return read_count_; }
    unsigned int write_count( void ) const { return write_count_; }

    /* read and write methods */
    std::string read( const size_t limit = BUFFER_SIZE )
    {
        char buffer[ BUFFER_SIZE ];

        ssize_t bytes_read = SystemCall( "read", ::read( fd_, buffer, min( BUFFER_SIZE, limit ) ) );
        if ( bytes_read == 0 ) {
            set_eof();
        }

        register_read();

        return string( buffer, bytes_read );
    }

    std::string::const_iterator write( const std::string & buffer, const bool write_all = true ) 
    {
        auto it = buffer.begin();

        do {
            it = write( it, buffer.end() );
        } while ( write_all and (it != buffer.end()) );

        return it;
    }

    std::string::const_iterator write( const std::string::const_iterator & begin,
                                       const std::string::const_iterator & end )
    {
        if ( begin >= end ) {
            throw runtime_error( "nothing to write" );
        }

        ssize_t bytes_written = SystemCall( "write", ::write( fd_, &*begin, end - begin ) );
        if ( bytes_written == 0 ) {
            throw runtime_error( "write returned 0" );
        }

        register_write();

        return begin + bytes_written;
    }

    void reset_eof( void ) { eof_ = false; }

    void replace_fd( const int new_fd ) 
    {
        if ( fd_ < 0 ) { /* has already been moved away */
            return;
        }

        try {
            SystemCall( "close", close( fd_ ) );
        } catch ( const exception & e ) { /* don't throw from destructor */
            print_exception( e );
        }

        fd_ = new_fd;
        write_count_ = 0;
        read_count_ = 0;
        eof_ = false;

        if ( fd_ <= 2 ) { /* make sure not overwriting stdout/stderr */
            throw unix_error( "FileDescriptor: fd <= 2" );
        }

        /* set close-on-exec flag so our file descriptors
        aren't passed on to unrelated children (like a shell) */
        SystemCall( "fcntl FD_CLOEXEC", fcntl( fd_, F_SETFD, FD_CLOEXEC ) );
    }

    /* forbid copying FileDescriptor objects or assigning them */
    FileDescriptor( const FileDescriptor & other ) = delete;
    const FileDescriptor & operator=( const FileDescriptor & other ) = delete;

};

#endif /* FILE_DESCRIPTOR_HPP */
