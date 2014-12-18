//=============================================================================
//File Name: Main.cpp
//Description: Receives images from the robot, processes them, then forwards
//             them to the DriverStationDisplay
//Author: FRC Team 3512, Spartatroniks
//=============================================================================

#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <cstdint>
#include <cstring>
#include <atomic>
#include <functional>
#include <chrono>

#include "MJPEG/MjpegStream.hpp"
#include "MJPEG/MjpegServer.hpp"
#include "MJPEG/WindowCallbacks.hpp"
#include "MJPEG/mjpeg_sck.hpp"
#include "Settings.hpp"
#include "Resource.h"

#include "ImageProcess/FindTarget2014.hpp"

// Global because IP configuration settings are needed in CALLBACK OnEvent
Settings gSettings( "IPSettings.txt" );

// Allows manipulation of objects in CALLBACK OnEvent
MjpegStream* gStreamWinPtr = NULL;
MjpegServer* gServer = NULL;
FindTarget2014* gProcessor = NULL;

// Allows usage of socket in CALLBACK OnEvent
std::atomic<mjpeg_socket_t> gCtrlSocket( INVALID_SOCKET );

std::function<void(void)> gNewImageFunc = NULL;

LRESULT CALLBACK OnEvent( HWND handle , UINT message , WPARAM wParam , LPARAM lParam );
BOOL CALLBACK AboutCbk( HWND hDlg , UINT message , WPARAM wParam , LPARAM lParam );

INT WINAPI WinMain( HINSTANCE Instance , HINSTANCE , LPSTR , INT ) {
    INITCOMMONCONTROLSEX icc;

    // Initialize common controls
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    const char* mainClassName = "Insight";

    HICON mainIcon = LoadIcon( Instance , "mainIcon" );
    HMENU mainMenu = LoadMenu( Instance , "mainMenu" );

    // Define a class for our main window
    WNDCLASSEX WindowClass;
    ZeroMemory( &WindowClass , sizeof(WNDCLASSEX) );
    WindowClass.cbSize        = sizeof(WNDCLASSEX);
    WindowClass.style         = 0;
    WindowClass.lpfnWndProc   = &OnEvent;
    WindowClass.cbClsExtra    = 0;
    WindowClass.cbWndExtra    = 0;
    WindowClass.hInstance     = Instance;
    WindowClass.hIcon         = mainIcon;
    WindowClass.hCursor       = LoadCursor( NULL , IDC_ARROW );
    WindowClass.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    WindowClass.lpszMenuName  = NULL;
    WindowClass.lpszClassName = mainClassName;
    WindowClass.hIconSm       = mainIcon;
    RegisterClassEx(&WindowClass);

    MSG message;

    FindTarget2014 processor;
    WindowCallbacks streamCallback;
    streamCallback.clickEvent = [&](int x , int y) { processor.clickEvent( x , y ); };
    gProcessor = &processor;

    RECT winSize = { 0 , 0 , static_cast<int>(320) , static_cast<int>(278) }; // set the size, but not the position
    AdjustWindowRect(
            &winSize ,
            WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_MINIMIZEBOX | WS_CLIPCHILDREN ,
            TRUE ); // window has menu

    // Create a new window to be used for the lifetime of the application
    HWND mainWindow = CreateWindowEx( 0 ,
            mainClassName ,
            "Insight" ,
            WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_MINIMIZEBOX | WS_CLIPCHILDREN ,
            ( GetSystemMetrics(SM_CXSCREEN) - (winSize.right - winSize.left) ) / 2 ,
            ( GetSystemMetrics(SM_CYSCREEN) - (winSize.bottom - winSize.top) ) / 2 ,
            winSize.right - winSize.left ,
            winSize.bottom - winSize.top ,
            NULL ,
            mainMenu ,
            Instance ,
            NULL );

    gServer = new MjpegServer( gSettings.getInt( "streamServerPort" ) );

    /* ===== Robot Data Sending Variables ===== */
    gCtrlSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if ( mjpeg_sck_valid(gCtrlSocket) ) {
        mjpeg_sck_setnonblocking(gCtrlSocket, 1);
    }
    else {
        std::cout << __FILE__ << ": failed to create robot control socket\n";
    }

    /* Used for sending control packets to robot
     * data format:
     * 8 byte header
     *     "ctrl\r\n\0\0"
     * 6 bytes of x-y pairs
     *     char x
     *     char y
     *     char x
     *     char y
     *     char x
     *     char y
     * 2 empty bytes
     */
    char data[9] = "ctrl\r\n\0\0";
    std::memset( data + 8 , 0 , sizeof(data) - 8 );
    bool newData = false;

    uint32_t robotIP = 0;
    std::string robotIPStr = gSettings.getString( "robotIP" );

    if ( robotIPStr == "255.255.255.255" ) {
        robotIP = INADDR_BROADCAST;
    }
    else {
        robotIP = inet_addr( robotIPStr.c_str() );

        if ( robotIP == INADDR_NONE ) {
            // Not a valid address, try to convert it as a host name
            hostent* host = gethostbyname( robotIPStr.c_str() );

            if ( host ) {
                robotIP = reinterpret_cast<in_addr*>(host->h_addr_list[0])->s_addr;
            }
            else {
                // Not a valid address nor a host name
                robotIP = 0;
            }
        }
    }

    unsigned short robotCtrlPort = gSettings.getInt( "robotControlPort" );

    // Make sure control data isn't sent too fast
    std::chrono::time_point<std::chrono::system_clock> lastSendTime;
    /* ======================================== */

    /* ===== Image Processing Variables ===== */
    uint8_t* imgBuffer = NULL;
    uint8_t* tempImg = NULL;
    uint32_t imgWidth = 0;
    uint32_t imgHeight = 0;
    uint32_t lastWidth = 0;
    uint32_t lastHeight = 0;
    /* ====================================== */

    // Image processing debugging is disabled by default
    if ( gSettings.getString( "enableImgProcDebug" ) == "true" ) {
        processor.enableDebugging( true );
    }

    gNewImageFunc = [&]{
        // Get new image to process
        imgBuffer = gStreamWinPtr->getCurrentImage();
        imgWidth = gStreamWinPtr->getCurrentSize().X;
        imgHeight = gStreamWinPtr->getCurrentSize().Y;

        if ( imgBuffer != NULL && imgWidth > 0 && imgHeight > 0 ) {
            if ( tempImg == NULL ) {
                tempImg = new uint8_t[imgWidth * imgHeight * 3];
            }
            else if ( lastWidth * lastHeight != imgWidth * imgHeight ) {
                delete[] tempImg;
                tempImg = new uint8_t[imgWidth * imgHeight * 3];
            }

            /* ===== Convert RGBA image to BGR for OpenCV ===== */
            // Copy R, G, and B channels but ignore A channel
            for ( unsigned int posIn = 0, posOut = 0 ; posIn < imgWidth * imgHeight ; posIn++, posOut++ ) {
                // Copy bytes of pixel into corresponding channels
                tempImg[3*posOut+0] = imgBuffer[4*posIn+2];
                tempImg[3*posOut+1] = imgBuffer[4*posIn+1];
                tempImg[3*posOut+2] = imgBuffer[4*posIn+0];
            }
            /* ================================================ */

            // Process the new image
            processor.setImage( tempImg , imgWidth , imgHeight );
            processor.processImage();

            gServer->serveImage( tempImg , imgWidth , imgHeight );

            // Send status on target search to robot
            data[8] = processor.foundTarget();

#if 0
            // Retrieve positions of targets and send them to robot
            if ( processor.getTargetPositions().size() > 0 ) {
                char x = 0;
                char y = 0;

                // Pack data structure with points
                for ( unsigned int i = 0 ; i < processor.getTargetPositions().size() &&
                        i < 3 ; i++ ) {
                    quad_t target = processor.getTargetPositions()[i];
                    for ( unsigned int j = 0 ; j < 4 ; j++ ) {
                        x += target.point[j].x;
                        y += target.point[j].y;
                    }
                    x /= 4;
                    y /= 4;

                    data[9 + 2*i] = x;
                    data[10 + 2*i] = y;
                }

                /* If there are less than three points in the data
                 * structure, zero the rest out.
                 */
                for ( unsigned int i = processor.getTargetPositions().size() ;
                        i < 3 ; i++ ) {
                    data[9 + 2*i] = 0;
                    data[10 + 2*i] = 0;
                }

                // We have new target data to send to the robot
                newData = true;
            }
#endif
            newData = true;

            // Update width and height
            lastWidth = imgWidth;
            lastHeight = imgHeight;
        }

        // If socket is valid, data was sent at least 200ms ago, and there is new data
        if ( mjpeg_sck_valid(gCtrlSocket) && std::chrono::system_clock::now() - lastSendTime > std::chrono::milliseconds(200) &&
                newData ) {
            // Build the target address
            sockaddr_in addr;
            std::memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
            addr.sin_addr.s_addr = htonl(robotIP);
            addr.sin_family      = AF_INET;
            addr.sin_port        = htons(robotCtrlPort);

            int sent = sendto(gCtrlSocket, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

            // Check for errors
            if (sent >= 0) {
                newData = false;
                lastSendTime = std::chrono::system_clock::now();
            }
        }
    };

    /* If this isn't allocated on the heap, it can't be destroyed soon enough.
     * If it were allocated on the stack, it would be destroyed when it leaves
     * WinMain's scope, which is after its parent window is destroyed. This
     * causes the cleanup in this object's destructor to not complete
     * successfully.
     */
    gStreamWinPtr = new MjpegStream( gSettings.getString( "streamHost" ) ,
            gSettings.getInt( "streamPort" ) ,
            gSettings.getString( "streamRequestPath" ) ,
            mainWindow ,
            0 ,
            0 ,
            320 ,
            240 ,
            Instance ,
            &streamCallback ,
            gNewImageFunc );

    // Make sure the main window is shown before continuing
    ShowWindow( mainWindow , SW_SHOW );

    while ( GetMessage( &message , NULL , 0 , 0 ) > 0 ) {
        // If a message was waiting in the message queue, process it
        TranslateMessage( &message );
        DispatchMessage( &message );
    }

    // Delete MJPEG stream window and server
    delete gStreamWinPtr;
    delete gServer;

    delete[] tempImg;

    // Clean up windows
    DestroyWindow( mainWindow );
    UnregisterClass( mainClassName , Instance );

    return message.wParam;
}

LRESULT CALLBACK OnEvent( HWND handle , UINT message , WPARAM wParam , LPARAM lParam ) {
    switch ( message ) {
    case WM_CREATE: {
        RECT winSize;
        GetClientRect( handle , &winSize );

        // Create slider that controls JPEG quality of MJPEG server
        HWND slider = CreateWindowEx( 0 ,
                TRACKBAR_CLASS ,
                "JPEG Quality" ,
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_BOTTOM | TBS_TOOLTIPS ,
                (winSize.right - winSize.left) - 100 ,
                (winSize.bottom - winSize.top) - 32 ,
                100 ,
                32 ,
                handle ,
                NULL ,
                GetModuleHandle(NULL) ,
                NULL );

        // Make one tick for every two values
        SendMessage( slider , TBM_SETTICFREQ , (WPARAM)20 , (LPARAM)0 );

        // Set initial box size
        int overlayPercent = gSettings.getInt( "overlayPercent" );
        SendMessage( slider , TBM_SETPOS , (WPARAM)TRUE , (LPARAM)overlayPercent );
        gProcessor->setOverlayPercent( overlayPercent );

        break;
    }
    case WM_COMMAND: {
        switch( LOWORD(wParam) ) {
            case IDC_STREAM_BUTTON: {
                 if ( gStreamWinPtr != NULL ) {
                     if ( gStreamWinPtr->isStreaming() ) {
                         // Stop streaming
                         if ( gServer != NULL ) {
                             gServer->stop();
                         }
                         gStreamWinPtr->stop();
                     }
                     else {
                         // Start streaming
                         gStreamWinPtr->start();
                         if ( gServer != NULL ) {
                             gServer->start();
                         }
                     }
                 }

                 break;
            }
            case IDM_SERVER_START: {
                if ( gStreamWinPtr != NULL ) {
                    if ( !gStreamWinPtr->isStreaming() ) {
                        // Start streaming
                        gStreamWinPtr->start();
                        if ( gServer != NULL ) {
                            gServer->start();
                        }
                    }
                }

                break;
            }
            case IDM_SERVER_STOP: {
                if ( gStreamWinPtr != NULL ) {
                    if ( gStreamWinPtr->isStreaming() ) {
                        // Stop streaming
                        if ( gServer != NULL ) {
                            gServer->stop();
                        }
                        gStreamWinPtr->stop();
                    }
                }

                break;
            }

            case IDM_ABOUT: {
                DialogBox( GetModuleHandle(NULL) , MAKEINTRESOURCE(IDD_ABOUTBOX) , handle , AboutCbk );

                break;
            }
        }

        break;
    }

    case WM_HSCROLL: {
        switch ( LOWORD(wParam) ) {
        case SB_ENDSCROLL: {
            int overlayPercent = SendMessage( (HWND)lParam , TBM_GETPOS , (WPARAM)0 , (LPARAM)0 );
            /* NULL check on gProcessor isn't necessary since gProcessor is
             * assigned before window creation
             */
            gProcessor->setOverlayPercent( overlayPercent );

            break;
        }
        case SB_THUMBPOSITION: {
            /* NULL check on gProcessor isn't necessary since gProcessor is
             * assigned before window creation
             */
            gProcessor->setOverlayPercent( HIWORD(wParam) );

            break;
        }
        case SB_THUMBTRACK: {
            /* NULL check on gProcessor isn't necessary since gProcessor is
             * assigned before window creation
             */
            gProcessor->setOverlayPercent( HIWORD(wParam) );

            break;
        }
        }

        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( handle , &ps );

        /* ===== Create buffer DC ===== */
        RECT rect;
        GetClientRect( handle , &rect );

        HDC bufferDC = CreateCompatibleDC( hdc );
        HBITMAP bufferBmp = CreateCompatibleBitmap( hdc , rect.right , rect.bottom );

        HBITMAP oldBmp = static_cast<HBITMAP>( SelectObject( bufferDC , bufferBmp ) );
        /* ============================ */

        // Fill buffer DC with a background color
        HRGN region = CreateRectRgn( 0 , 0 , rect.right , rect.bottom );
        FillRgn( bufferDC , region , GetSysColorBrush(COLOR_3DFACE) );
        DeleteObject( region );

        // Creates 1:1 relationship between logical units and pixels
        int oldMapMode = SetMapMode( bufferDC , MM_TEXT );

        BitBlt( hdc , 0 , 0 , rect.right , rect.bottom , bufferDC , 0 , 0 , SRCCOPY );

        // Restore old DC mapping mode
        SetMapMode( bufferDC , oldMapMode );

        // Replace the old bitmap and delete the created one
        DeleteObject( SelectObject( bufferDC , oldBmp ) );

        // Free the buffer DC
        DeleteDC( bufferDC );

        EndPaint( handle , &ps );

        break;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);

        break;
    }

    default: {
        return DefWindowProc(handle, message, wParam, lParam);
    }
    }

    return 0;
}

// Message handler for "About" box
BOOL CALLBACK AboutCbk( HWND hDlg , UINT message , WPARAM wParam , LPARAM lParam ) {
    UNREFERENCED_PARAMETER(lParam);
    switch ( message ) {
    case WM_INITDIALOG: {
        return TRUE;
    }

    case WM_COMMAND: {
        if ( LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL ) {
            EndDialog( hDlg , LOWORD(wParam) );
            return TRUE;
        }

        break;
    }
    }

    return FALSE;
}
