// Copyright (c) FRC Team 3512, Spartatroniks 2013-2016. All Rights Reserved.

#ifndef FIND_TARGET_2013_HPP
#define FIND_TARGET_2013_HPP

#include "ProcBase.hpp"

/**
 * Processes a provided image and finds targets like the ones from FRC 2013
 */
class FindTarget2013 : public ProcBase {
private:
    void prepareImage();
    void findTargets();
    void drawOverlay();
};

#endif  // FIND_TARGET_2013_HPP
