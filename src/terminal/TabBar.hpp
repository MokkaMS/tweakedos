/*
 * terminal/TabBar.hpp
 * CraftOS-PC 2
 *
 * Implements a VS Code-style tab bar for managing multiple ComputerCraft
 * terminals and monitors within a single window.
 *
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2024 JackMacWindows.
 */

#ifndef TERMINAL_TABBAR_HPP
#define TERMINAL_TABBAR_HPP

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include <Terminal.hpp>

struct TabBarItem {
    Terminal * term;
    int x;
    int width;
    int closeX;
    int closeWidth;
    bool isActive;
    std::string title;
};

class SDLTerminal;

class TabBar {
public:
    static int getTabBarHeight(int dpiScale);
    static std::vector<TabBarItem> computeTabs(int winW, int dpiScale);
    static int getPlusButtonX(const std::vector<TabBarItem>& tabs, int dpiScale);
    static int getPlusButtonWidth(int dpiScale);

    static bool handleMouseDown(int x, int y, int winW, int dpiScale);
    static bool handleMouseMove(int x, int y, int winW, int dpiScale, bool &needRedraw);

    static void renderSoftware(SDLTerminal *term, SDL_Surface *surf, SDL_Surface *fontSurface, int winW, int dpiScale);
    static void renderHardware(SDLTerminal *term, SDL_Renderer *ren, SDL_Texture *fontTexture, int winW, int dpiScale);

    static int hoveredTab;
    static int hoveredCloseTab;
    static bool hoveredPlus;
    static int scrollOffset;

    static void closeTerminalScreen(Terminal * targetTerm);
    static void createNewComputerTab();
};

#endif
