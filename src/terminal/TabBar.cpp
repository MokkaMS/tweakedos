/*
 * terminal/TabBar.cpp
 * CraftOS-PC 2
 *
 * Implements a VS Code-style tab bar for managing multiple ComputerCraft
 * terminals and monitors within a single window.
 *
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2024 JackMacWindows.
 */

#include "TabBar.hpp"
#include "SDLTerminal.hpp"
#include "../termsupport.hpp"
#include "../runtime.hpp"
#include "../Computer.hpp"
#include <algorithm>
#include <set>

int TabBar::hoveredTab = -1;
int TabBar::hoveredCloseTab = -1;
bool TabBar::hoveredPlus = false;
int TabBar::scrollOffset = 0;

int TabBar::getTabBarHeight(int dpiScale) {
    if (dpiScale < 1) dpiScale = 1;
    return 28 * dpiScale;
}

static SDL_Rect getFontCharRect(unsigned char c) {
    SDL_Rect retval;
    retval.w = 6;
    retval.h = 9;
    retval.x = (int)(8 * (c & 0x0F) + 1);
    retval.y = (int)(11 * (c >> 4) + 1);
    return retval;
}

static void drawStrSoftware(SDL_Surface* surf, SDL_Surface* font, const std::string& str, int x, int y, Color color, int dpiScale) {
    if (font == NULL || surf == NULL) return;
    SDL_SetSurfaceColorMod(font, color.r, color.g, color.b);
    int curX = x;
    int charW = 6 * dpiScale;
    int charH = 9 * dpiScale;
    for (char ch : str) {
        if (curX + charW > surf->w) break;
        SDL_Rect srcrect = getFontCharRect((unsigned char)ch);
        SDL_Rect destrect = {curX, y, charW, charH};
        SDL_BlitScaled(font, &srcrect, surf, &destrect);
        curX += charW;
    }
}

static void drawStrHardware(SDL_Renderer* ren, SDL_Texture* font, const std::string& str, int x, int y, Color color, int dpiScale) {
    if (font == NULL || ren == NULL) return;
    SDL_SetTextureColorMod(font, color.r, color.g, color.b);
    int curX = x;
    int charW = 6 * dpiScale;
    int charH = 9 * dpiScale;
    for (char ch : str) {
        SDL_Rect srcrect = getFontCharRect((unsigned char)ch);
        SDL_Rect destrect = {curX, y, charW, charH};
        SDL_RenderCopy(ren, font, &srcrect, &destrect);
        curX += charW;
    }
}

std::vector<TabBarItem> TabBar::computeTabs(int winW, int dpiScale) {
    std::vector<TabBarItem> items;
    if (dpiScale < 1) dpiScale = 1;

    std::lock_guard<std::mutex> lock(renderTargetsLock);
    if (renderTargets.empty()) return items;

    int totalDesired = 0;
    std::vector<std::pair<Terminal*, std::string>> termData;
    for (Terminal* t : renderTargets) {
        std::string title = t->title;
        if (title.rfind("CraftOS Terminal: ", 0) == 0) title = title.substr(18);
        else if (title.rfind("CraftOS Terminal - ", 0) == 0) title = title.substr(19);
        if (title.empty()) title = "Computer";

        int idealW = (int)((title.length() + 6) * 6 * dpiScale + 28 * dpiScale);
        if (idealW < 100 * dpiScale) idealW = 100 * dpiScale;
        if (idealW > 200 * dpiScale) idealW = 200 * dpiScale;
        totalDesired += idealW;
        termData.push_back({t, title});
    }

    int availableW = winW - 36 * dpiScale;
    float scaleFactor = 1.0f;
    if (totalDesired > availableW && availableW > 0) {
        scaleFactor = (float)availableW / (float)totalDesired;
    }

    int curX = -scrollOffset;
    for (size_t i = 0; i < termData.size(); i++) {
        Terminal* t = termData[i].first;
        std::string title = termData[i].second;

        int tabW = (int)((title.length() + 6) * 6 * dpiScale + 28 * dpiScale);
        if (tabW < 100 * dpiScale) tabW = 100 * dpiScale;
        if (tabW > 200 * dpiScale) tabW = 200 * dpiScale;
        tabW = (int)(tabW * scaleFactor);
        if (tabW < 60 * dpiScale) tabW = 60 * dpiScale;

        TabBarItem item;
        item.term = t;
        item.x = curX;
        item.width = tabW;
        item.closeWidth = 14 * dpiScale;
        item.closeX = item.x + item.width - item.closeWidth - 5 * dpiScale;
        item.isActive = (renderTarget != renderTargets.end() && t == *renderTarget);
        item.title = title;

        items.push_back(item);
        curX += tabW;
    }

    return items;
}

int TabBar::getPlusButtonX(const std::vector<TabBarItem>& tabs, int dpiScale) {
    if (dpiScale < 1) dpiScale = 1;
    if (tabs.empty()) return 4 * dpiScale;
    return tabs.back().x + tabs.back().width + 4 * dpiScale;
}

int TabBar::getPlusButtonWidth(int dpiScale) {
    if (dpiScale < 1) dpiScale = 1;
    return 24 * dpiScale;
}

void TabBar::closeTerminalScreen(Terminal * targetTerm) {
    if (targetTerm == NULL) return;
    LockGuard lock(computers);
    for (Computer * c : *computers) {
        if (targetTerm == c->term || findMonitorFromWindowID(c, targetTerm->id, NULL) != NULL) {
            std::lock_guard<std::mutex> lockQ(c->termEventQueueMutex);
            SDL_Event closeEv;
            closeEv.type = SDL_WINDOWEVENT;
            closeEv.window.event = SDL_WINDOWEVENT_CLOSE;
            closeEv.window.windowID = targetTerm->id;
            c->termEventQueue.push(closeEv);
            c->event_lock.notify_all();
            return;
        }
    }
    for (Terminal * t : orphanedTerminals) {
        if (t == targetTerm) {
            orphanedTerminals.erase(t);
            t->factory->deleteTerminal(t);
            break;
        }
    }
}

void TabBar::createNewComputerTab() {
    int newId = 0;
    {
        LockGuard lock(computers);
        std::set<int> ids;
        for (Computer * c : *computers) ids.insert(c->id);
        while (ids.find(newId) != ids.end()) newId++;
    }
    startComputer(newId);
}

bool TabBar::handleMouseDown(int x, int y, int winW, int dpiScale) {
    if (dpiScale < 1) dpiScale = 1;
    if (y >= getTabBarHeight(dpiScale)) return false;

    std::vector<TabBarItem> tabs = computeTabs(winW, dpiScale);
    for (size_t i = 0; i < tabs.size(); i++) {
        const auto& tab = tabs[i];
        if (x >= tab.x && x < tab.x + tab.width) {
            if (x >= tab.closeX && x < tab.closeX + tab.closeWidth) {
                closeTerminalScreen(tab.term);
                return true;
            } else {
                selectRenderTarget(tab.term);
                return true;
            }
        }
    }

    int plusX = getPlusButtonX(tabs, dpiScale);
    int plusW = getPlusButtonWidth(dpiScale);
    if (x >= plusX && x < plusX + plusW) {
        createNewComputerTab();
        return true;
    }

    return true; // Consume click in tab bar empty space
}

bool TabBar::handleMouseMove(int x, int y, int winW, int dpiScale, bool &needRedraw) {
    needRedraw = false;
    if (dpiScale < 1) dpiScale = 1;

    int oldHoveredTab = hoveredTab;
    int oldHoveredClose = hoveredCloseTab;
    bool oldHoveredPlus = hoveredPlus;

    if (y >= getTabBarHeight(dpiScale)) {
        hoveredTab = -1;
        hoveredCloseTab = -1;
        hoveredPlus = false;
        if (oldHoveredTab != -1 || oldHoveredClose != -1 || oldHoveredPlus) {
            needRedraw = true;
        }
        return false;
    }

    hoveredTab = -1;
    hoveredCloseTab = -1;
    hoveredPlus = false;

    std::vector<TabBarItem> tabs = computeTabs(winW, dpiScale);
    for (size_t i = 0; i < tabs.size(); i++) {
        const auto& tab = tabs[i];
        if (x >= tab.x && x < tab.x + tab.width) {
            hoveredTab = (int)i;
            if (x >= tab.closeX && x < tab.closeX + tab.closeWidth) {
                hoveredCloseTab = (int)i;
            }
            break;
        }
    }

    int plusX = getPlusButtonX(tabs, dpiScale);
    int plusW = getPlusButtonWidth(dpiScale);
    if (x >= plusX && x < plusX + plusW) {
        hoveredPlus = true;
    }

    if (hoveredTab != oldHoveredTab || hoveredCloseTab != oldHoveredClose || hoveredPlus != oldHoveredPlus) {
        needRedraw = true;
    }

    return true;
}

void TabBar::renderSoftware(SDL_Surface *surf, SDL_Surface *fontSurface, int winW, int dpiScale) {
    if (surf == NULL || fontSurface == NULL) return;
    if (dpiScale < 1) dpiScale = 1;

    int h = getTabBarHeight(dpiScale);

    // VS Code Tab Bar background: #252526
    SDL_Rect bgRect = {0, 0, winW, h};
    SDL_FillRect(surf, &bgRect, SDL_MapRGB(surf->format, 37, 37, 38));

    // Bottom border: #1e1e1e
    SDL_Rect bottomBorder = {0, h - 1, winW, 1};
    SDL_FillRect(surf, &bottomBorder, SDL_MapRGB(surf->format, 30, 30, 30));

    std::vector<TabBarItem> tabs = computeTabs(winW, dpiScale);
    int textY = (h - 9 * dpiScale) / 2;

    for (size_t i = 0; i < tabs.size(); i++) {
        const auto& tab = tabs[i];

        // Tab background
        SDL_Rect tabRect = {tab.x, 0, tab.width, h - 1};
        if (tab.isActive) {
            // Active tab background: #1e1e1e
            SDL_FillRect(surf, &tabRect, SDL_MapRGB(surf->format, 30, 30, 30));

            // Top accent indicator: VS Code blue #007acc
            SDL_Rect topAccent = {tab.x, 0, tab.width, 2 * dpiScale};
            SDL_FillRect(surf, &topAccent, SDL_MapRGB(surf->format, 0, 122, 204));
        } else {
            // Inactive tab background: #2d2d2d or hover #383838
            if (hoveredTab == (int)i) {
                SDL_FillRect(surf, &tabRect, SDL_MapRGB(surf->format, 56, 56, 56));
            } else {
                SDL_FillRect(surf, &tabRect, SDL_MapRGB(surf->format, 45, 45, 45));
            }
        }

        // Right separator border: #252526
        SDL_Rect sep = {tab.x + tab.width - 1, 0, 1, h - 1};
        SDL_FillRect(surf, &sep, SDL_MapRGB(surf->format, 37, 37, 38));

        // Terminal/Monitor icon
        bool isMonitor = (tab.title.find("monitor") != std::string::npos || tab.title.find("Monitor") != std::string::npos);
        if (isMonitor) {
            drawStrSoftware(surf, fontSurface, "[M]", tab.x + 6 * dpiScale, textY, {78, 201, 176}, dpiScale); // Teal/cyan
        } else {
            drawStrSoftware(surf, fontSurface, ">_", tab.x + 6 * dpiScale, textY, {220, 182, 122}, dpiScale); // Amber/gold
        }

        // Title text
        int titleX = tab.x + (isMonitor ? 26 : 20) * dpiScale;
        int maxChars = (tab.closeX - titleX - 4 * dpiScale) / (6 * dpiScale);
        std::string displayTitle = tab.title;
        if (maxChars > 2 && (int)displayTitle.length() > maxChars) {
            displayTitle = displayTitle.substr(0, maxChars - 1) + ".";
        }
        Color textColor = tab.isActive ? Color{255, 255, 255} : Color{150, 150, 150};
        drawStrSoftware(surf, fontSurface, displayTitle, titleX, textY, textColor, dpiScale);

        // Close button 'x'
        if (hoveredCloseTab == (int)i) {
            SDL_Rect closeBg = {tab.closeX - 2 * dpiScale, textY - 2 * dpiScale, tab.closeWidth + 4 * dpiScale, 13 * dpiScale};
            SDL_FillRect(surf, &closeBg, SDL_MapRGB(surf->format, 69, 69, 69));
            drawStrSoftware(surf, fontSurface, "x", tab.closeX + 3 * dpiScale, textY, {255, 255, 255}, dpiScale);
        } else {
            drawStrSoftware(surf, fontSurface, "x", tab.closeX + 3 * dpiScale, textY, {133, 133, 133}, dpiScale);
        }
    }

    // New Tab '+' button
    int plusX = getPlusButtonX(tabs, dpiScale);
    int plusW = getPlusButtonWidth(dpiScale);
    int plusH = h - 6 * dpiScale;
    int plusY = 3 * dpiScale;
    SDL_Rect plusRect = {plusX, plusY, plusW, plusH};
    if (hoveredPlus) {
        SDL_FillRect(surf, &plusRect, SDL_MapRGB(surf->format, 56, 56, 56));
        drawStrSoftware(surf, fontSurface, "+", plusX + (plusW - 6 * dpiScale) / 2, textY, {255, 255, 255}, dpiScale);
    } else {
        drawStrSoftware(surf, fontSurface, "+", plusX + (plusW - 6 * dpiScale) / 2, textY, {180, 180, 180}, dpiScale);
    }
}

void TabBar::renderHardware(SDL_Renderer *ren, SDL_Texture *fontTexture, int winW, int dpiScale) {
    if (ren == NULL || fontTexture == NULL) return;
    if (dpiScale < 1) dpiScale = 1;

    int h = getTabBarHeight(dpiScale);

    // VS Code Tab Bar background: #252526
    SDL_Rect bgRect = {0, 0, winW, h};
    SDL_SetRenderDrawColor(ren, 37, 37, 38, 255);
    SDL_RenderFillRect(ren, &bgRect);

    // Bottom border: #1e1e1e
    SDL_Rect bottomBorder = {0, h - 1, winW, 1};
    SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
    SDL_RenderFillRect(ren, &bottomBorder);

    std::vector<TabBarItem> tabs = computeTabs(winW, dpiScale);
    int textY = (h - 9 * dpiScale) / 2;

    for (size_t i = 0; i < tabs.size(); i++) {
        const auto& tab = tabs[i];

        // Tab background
        SDL_Rect tabRect = {tab.x, 0, tab.width, h - 1};
        if (tab.isActive) {
            // Active tab background: #1e1e1e
            SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
            SDL_RenderFillRect(ren, &tabRect);

            // Top accent indicator: VS Code blue #007acc
            SDL_Rect topAccent = {tab.x, 0, tab.width, 2 * dpiScale};
            SDL_SetRenderDrawColor(ren, 0, 122, 204, 255);
            SDL_RenderFillRect(ren, &topAccent);
        } else {
            // Inactive tab background: #2d2d2d or hover #383838
            if (hoveredTab == (int)i) {
                SDL_SetRenderDrawColor(ren, 56, 56, 56, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 45, 45, 45, 255);
            }
            SDL_RenderFillRect(ren, &tabRect);
        }

        // Right separator border: #252526
        SDL_Rect sep = {tab.x + tab.width - 1, 0, 1, h - 1};
        SDL_SetRenderDrawColor(ren, 37, 37, 38, 255);
        SDL_RenderFillRect(ren, &sep);

        // Terminal/Monitor icon
        bool isMonitor = (tab.title.find("monitor") != std::string::npos || tab.title.find("Monitor") != std::string::npos);
        if (isMonitor) {
            drawStrHardware(ren, fontTexture, "[M]", tab.x + 6 * dpiScale, textY, {78, 201, 176}, dpiScale);
        } else {
            drawStrHardware(ren, fontTexture, ">_", tab.x + 6 * dpiScale, textY, {220, 182, 122}, dpiScale);
        }

        // Title text
        int titleX = tab.x + (isMonitor ? 26 : 20) * dpiScale;
        int maxChars = (tab.closeX - titleX - 4 * dpiScale) / (6 * dpiScale);
        std::string displayTitle = tab.title;
        if (maxChars > 2 && (int)displayTitle.length() > maxChars) {
            displayTitle = displayTitle.substr(0, maxChars - 1) + ".";
        }
        Color textColor = tab.isActive ? Color{255, 255, 255} : Color{150, 150, 150};
        drawStrHardware(ren, fontTexture, displayTitle, titleX, textY, textColor, dpiScale);

        // Close button 'x'
        if (hoveredCloseTab == (int)i) {
            SDL_Rect closeBg = {tab.closeX - 2 * dpiScale, textY - 2 * dpiScale, tab.closeWidth + 4 * dpiScale, 13 * dpiScale};
            SDL_SetRenderDrawColor(ren, 69, 69, 69, 255);
            SDL_RenderFillRect(ren, &closeBg);
            drawStrHardware(ren, fontTexture, "x", tab.closeX + 3 * dpiScale, textY, {255, 255, 255}, dpiScale);
        } else {
            drawStrHardware(ren, fontTexture, "x", tab.closeX + 3 * dpiScale, textY, {133, 133, 133}, dpiScale);
        }
    }

    // New Tab '+' button
    int plusX = getPlusButtonX(tabs, dpiScale);
    int plusW = getPlusButtonWidth(dpiScale);
    int plusH = h - 6 * dpiScale;
    int plusY = 3 * dpiScale;
    SDL_Rect plusRect = {plusX, plusY, plusW, plusH};
    if (hoveredPlus) {
        SDL_SetRenderDrawColor(ren, 56, 56, 56, 255);
        SDL_RenderFillRect(ren, &plusRect);
        drawStrHardware(ren, fontTexture, "+", plusX + (plusW - 6 * dpiScale) / 2, textY, {255, 255, 255}, dpiScale);
    } else {
        drawStrHardware(ren, fontTexture, "+", plusX + (plusW - 6 * dpiScale) / 2, textY, {180, 180, 180}, dpiScale);
    }
}
