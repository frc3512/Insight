//=============================================================================
//File Name: MjpegClient.cpp
//Description: Receives an MJPEG stream and displays it in a child window with
//             the specified properties
//Author: FRC Team 3512, Spartatroniks
//=============================================================================

#include "../Util.hpp"
#include "MjpegClient.hpp"
#include "mjpeg_sleep.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include <iostream>
#include <cstring>

// Convert a string to lower case
std::string toLower( std::string str ) {
    for ( auto i : str ) {
        i = static_cast<char>(std::tolower(i));
    }
    return str;
}

MjpegClient::MjpegClient( const std::string& hostName , unsigned short port ,
        const std::string& requestPath
        ) :
        m_hostName( hostName ) ,
        m_port( port ) ,
        m_requestPath( requestPath ) ,

        m_pxlBuf( NULL ) ,
        m_imgWidth( 0 ) ,
        m_imgHeight( 0 ) ,

        m_extBuf( NULL ) ,
        m_extWidth( 0 ) ,
        m_extHeight( 0 ) ,

        m_newImageAvailable( false ) ,

        m_streamInst( NULL ) ,

        m_stopReceive( true )
{
    // Initialize mutexes
    mjpeg_mutex_init( &m_imageMutex );
    mjpeg_mutex_init( &m_extMutex );


    // Set up the callback description structure
    std::memset( &m_callbacks , 0 , sizeof(struct mjpeg_callbacks_t) );
    m_callbacks.readcallback = readCallback;
    m_callbacks.donecallback = doneCallback;
    m_callbacks.optarg = this;
}

MjpegClient::~MjpegClient() {
    stop();

    delete[] m_pxlBuf;
    delete[] m_extBuf;

    // Destroy mutexes
    mjpeg_mutex_destroy( &m_imageMutex );
    mjpeg_mutex_destroy( &m_extMutex );
}

void MjpegClient::start() {
    if ( m_stopReceive == true ) { // if stream is closed, reopen it
        m_stopReceive = false;

        // Launch the MJPEG receiving/processing thread
        m_streamInst = mjpeg_launchthread( const_cast<char*>( m_hostName.c_str() ) , m_port , const_cast<char*>( m_requestPath.c_str() ) , &m_callbacks );
        if ( m_streamInst == NULL ) {
            m_stopReceive = true;
        }
    }
}

void MjpegClient::stop() {
    if ( m_stopReceive == false ) { // if stream is open, close it
        m_stopReceive = true;

        // Close the receive thread
        if ( m_streamInst != NULL ) {
            mjpeg_stopthread( m_streamInst );
        }
    }
}

bool MjpegClient::isStreaming() const {
    return !m_stopReceive;
}

void MjpegClient::saveCurrentImage( const std::string& fileName ) {
    mjpeg_mutex_lock( &m_imageMutex );

    // Deduce the image type from its extension
    if ( fileName.size() > 3 ) {
        // Extract the extension
        std::string extension = fileName.substr(fileName.size() - 3);

        if ( toLower(extension) == "bmp" ) {
            // BMP format
            stbi_write_bmp( fileName.c_str(), m_imgWidth, m_imgHeight, 4, m_pxlBuf );
        }
        else if ( toLower(extension) == "tga" ) {
            // TGA format
            stbi_write_tga( fileName.c_str(), m_imgWidth, m_imgHeight, 4, m_pxlBuf );
        }
        else if( toLower(extension) == "png" ) {
            // PNG format
            stbi_write_png( fileName.c_str(), m_imgWidth, m_imgHeight, 4, m_pxlBuf, 0 );
        }
        else {
            std::cout << "MjpegClient: failed to save image to '" << fileName << "'\n";
        }
    }

    mjpeg_mutex_unlock( &m_imageMutex );
}

uint8_t* MjpegClient::getCurrentImage() {
    mjpeg_mutex_lock( &m_imageMutex );
    mjpeg_mutex_lock( &m_extMutex );

    if ( m_pxlBuf != NULL ) {
        // If buffer is wrong size, reallocate it
        if ( m_imgWidth != m_extWidth || m_imgHeight != m_extHeight ) {
            if ( m_extBuf != NULL ) {
                delete[] m_extBuf;
            }

            // Allocate new buffer to fit latest image
            m_extBuf = new uint8_t[m_imgWidth * m_imgHeight * 4];
            m_extWidth = m_imgWidth;
            m_extHeight = m_imgHeight;
        }

        std::memcpy( m_extBuf , m_pxlBuf , m_extWidth * m_extHeight * 4 );

        /* Since the image just got copied, it's no longer new. This is checked
         * in the if statement "m_pxlBuf != NULL" because it's impossible for
         * this to be set to true when no image has been received yet.
         */
        m_newImageAvailable = false;
    }

    mjpeg_mutex_unlock( &m_extMutex );
    mjpeg_mutex_unlock( &m_imageMutex );

    return m_extBuf;
}

Vector2i MjpegClient::getCurrentSize() {
    mjpeg_mutex_lock( &m_extMutex );

    Vector2i temp( m_extWidth , m_extHeight );

    mjpeg_mutex_unlock( &m_extMutex );

    return temp;
}

bool MjpegClient::newImageAvailable() const {
    return m_newImageAvailable;
}

void MjpegClient::doneCallback( void* optarg ) {
    static_cast<MjpegClient*>(optarg)->m_stopReceive = true;
    static_cast<MjpegClient*>(optarg)->m_streamInst = NULL;

    // Call virtually overridden function
    static_cast<MjpegClient*>(optarg)->done( optarg );
}

void MjpegClient::readCallback( char* buf , int bufsize , void* optarg ) {
    // Create pointer to stream to make it easier to access the instance later
    MjpegClient* streamPtr = static_cast<MjpegClient*>( optarg );

    // Load the image received (converts from JPEG to pixel array)
    int width, height, channels;
    uint8_t* ptr = stbi_load_from_memory((unsigned char*)buf, bufsize, &width, &height, &channels, STBI_rgb_alpha);

    if ( ptr && width && height ) {
        mjpeg_mutex_lock( &streamPtr->m_imageMutex );

        // Free old buffer and store new one created by stbi_load_from_memory()
        delete[] streamPtr->m_pxlBuf;

        streamPtr->m_pxlBuf = ptr;

        streamPtr->m_imgWidth = width;
        streamPtr->m_imgHeight = height;

        mjpeg_mutex_unlock( &streamPtr->m_imageMutex );

        if ( !streamPtr->m_newImageAvailable ) {
            streamPtr->m_newImageAvailable = true;
        }

        // Call virtually overridden function
        streamPtr->read( buf , bufsize , optarg );
    }
    else {
        std::cout << "MjpegClient: image failed to load: " << stbi_failure_reason() << "\n";
    }
}

void MjpegClient::done( void* optarg ) {

}

void MjpegClient::read( char* buf , int bufsize , void* optarg ) {

}