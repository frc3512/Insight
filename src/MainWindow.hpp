// Copyright (c) 2013-2018 FRC Team 3512. All Rights Reserved.

#pragma once

#include <stdint.h>

#include <memory>
#include <string>

#include <QMainWindow>

#include "ImageProcess/FindTarget2016.hpp"
#include "MJPEG/MjpegServer.hpp"
#include "MJPEG/WindowCallbacks.hpp"
#include "MJPEG/mjpeg_sck.hpp"
#include "Settings.hpp"

class ClientBase;
class QAction;
class QMenu;
class QPushButton;
class QSlider;
class VideoStream;

/**
 * Creates application's main window
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
    QWidget* centralWidget;

public:
    MainWindow();
    virtual ~MainWindow();

private slots:
    void startMJPEG();
    void stopMJPEG();
    void about();

    void toggleButton();
    void handleSlider(int value);
    void newImageFunc();

private:
    void createActions();
    void createMenus();

    Settings m_settings{"IPSettings.txt"};

    WindowCallbacks m_streamCallback;
    ClientBase* m_client;
    VideoStream* m_stream;
    QPushButton* m_button;
    QSlider* m_slider;

    QMenu* m_serverMenu;
    QMenu* m_helpMenu;
    QAction* m_startMJPEGAct;
    QAction* m_stopMJPEGAct;
    QAction* m_aboutAct;

    std::unique_ptr<MjpegServer> m_server;
    std::unique_ptr<FindTarget2016> m_processor;

    /* ===== Image Processing Variables ===== */
    uint8_t* m_imgBuffer = nullptr;
    uint8_t* m_tempImg = nullptr;
    uint32_t m_imgWidth = 0;
    uint32_t m_imgHeight = 0;
    uint32_t m_lastWidth = 0;
    uint32_t m_lastHeight = 0;
    /* ====================================== */

    /* ===== Robot Data Sending Variables ===== */
    mjpeg_socket_t m_ctrlSocket;

    /* Used for sending control packets to robot
     * data format:
     * 8 byte header
     *     "ctrl\r\n\0\0"
     * 2 bytes of x-y pair
     *     char x
     *     char y
     * 2 empty bytes
     */
    char m_data[12];

    bool m_newData;
    uint32_t m_robotIP;
    std::string m_robotIPStr;
    uint16_t m_robotCtrlPort;

    // Make sure control data isn't sent too fast
    std::chrono::time_point<std::chrono::system_clock> m_lastSendTime;
    /* ======================================== */
};
