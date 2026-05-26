#include <windows.h>
#include <GL/gl.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <thread>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm>

#define PI 3.14159265358979323846

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE  0x809D
#endif

// --- HighResolutionTimer ---
class HighResolutionTimer {
private:
    LARGE_INTEGER frequency;
    LARGE_INTEGER startTime;
    LARGE_INTEGER pauseTime;
    bool isRunning;
    double accumulatedTime;
public:
    HighResolutionTimer() {
        QueryPerformanceFrequency(&frequency);
        reset();
    }
    void start() { if (!isRunning) { QueryPerformanceCounter(&startTime); isRunning = true; } }
    void pause() {
        if (isRunning) {
            QueryPerformanceCounter(&pauseTime);
            accumulatedTime += (double)(pauseTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
            isRunning = false;
        }
    }
    void reset() { isRunning = false; accumulatedTime = 0.0; startTime.QuadPart = 0; }
    bool getIsRunning() { return isRunning; }
    double getElapsedSeconds() {
        if (!isRunning) return accumulatedTime;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return accumulatedTime + (double)(now.QuadPart - startTime.QuadPart) / frequency.QuadPart;
    }
};

// --- global  ---
HighResolutionTimer stopwatch;
bool isStopwatchRunning = false;
struct LapRecord { double totalSec; double splitSec; };
std::vector<LapRecord> lapTimes;
int lapScrollOffset = 0; // scroll offset

bool isCountdownRunning = false;
bool isCountdownPaused = false;
double countdownDuration = 0.0;
HighResolutionTimer countdownTimer;

// UI input 
bool isInputting = false;
std::string inputBuffer = "";


GLuint fontBase;

// --- create font ---
void buildSystemFont(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    HDC hdc = GetDC(hwnd);
    fontBase = glGenLists(96);
    HFONT font = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    wglUseFontBitmapsA(hdc, 32, 96, fontBase);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    ReleaseDC(hwnd, hdc);
}

void glPrint(float x, float y, const char* text) {
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(fontBase - 32);
    glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
    glPopAttrib();
}


void glDrawCircle(float cx, float cy, float r, int segments, bool fill) {
    glBegin(fill ? GL_POLYGON : GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
        float theta = 2.0f * PI * float(i) / float(segments);
        glVertex2f(cx + r * cos(theta), cy + r * sin(theta));
    }
    glEnd();
}

void glDrawHand(float length, float width, float angleDegrees, float r, float g, float b) {
    glPushMatrix();
    glRotatef(-angleDegrees, 0.0f, 0.0f, 1.0f);
    glColor3f(r, g, b);
    glLineWidth(width);
    glBegin(GL_LINES);
        glVertex2f(0.0f, 0.0f);
        glVertex2f(0.0f, length);
    glEnd();
    glPopMatrix();
}

// --- drawWatchDial ---
void drawWatchDial(SYSTEMTIME st) {
    glColor3f(0.12f, 0.14f, 0.18f);
    glDrawCircle(0.0f, 0.0f, 0.85f, 100, true);
    glColor3f(0.0f, 0.5f, 1.0f); 
    glLineWidth(3.0f);
    glDrawCircle(0.0f, 0.0f, 0.85f, 100, false);

    glColor3f(0.8f, 0.8f, 0.8f);
    for (int i = 0; i < 60; i++) {
        glPushMatrix();
        glRotatef(i * 6.0f, 0.0f, 0.0f, 1.0f);
        float len = (i % 5 == 0) ? 0.06f : 0.02f;
        glLineWidth((i % 5 == 0) ? 2.5f : 1.0f);
        glBegin(GL_LINES);
            glVertex2f(0.0f, 0.85f - len);
            glVertex2f(0.0f, 0.85f);
        glEnd();
        glPopMatrix();
    }

    // countdown watch
    glPushMatrix();
    glTranslatef(0.0f, 0.38f, 0.0f);
    glColor3f(0.15f, 0.18f, 0.22f);
    glDrawCircle(0.0f, 0.0f, 0.25f, 40, true);
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(1.5f);
    glDrawCircle(0.0f, 0.0f, 0.25f, 40, false);
    double cdRemain = countdownDuration;
    if (isCountdownRunning || isCountdownPaused) {
        cdRemain = countdownDuration - countdownTimer.getElapsedSeconds();
        if (cdRemain < 0) cdRemain = 0;
    }
    float cdAngle = (countdownDuration > 0) ? (fmod(cdRemain, 60.0) / 60.0f) * 360.0f : 0.0f;
    glDrawHand(0.20f, 2.0f, cdAngle, 1.0f, 0.27f, 0.0f); 
    glPopMatrix();

    // stopwatch
    glPushMatrix();
    glTranslatef(0.0f, -0.38f, 0.0f);
    glColor3f(0.15f, 0.18f, 0.22f);
    glDrawCircle(0.0f, 0.0f, 0.25f, 40, true);
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(1.5f);
    glDrawCircle(0.0f, 0.0f, 0.25f, 40, false);
    double swSec = stopwatch.getElapsedSeconds();
    float msAngle = (swSec - (int)swSec) * 360.0f;
    glDrawHand(0.20f, 2.0f, msAngle, 1.0f, 0.27f, 0.0f);
    glPopMatrix();

    float hourAngle = (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f;
    float minAngle = (st.wMinute + st.wSecond / 60.0f) * 6.0f;
    glDrawHand(0.45f, 5.0f, hourAngle, 1.0f, 1.0f, 1.0f); 
    glDrawHand(0.68f, 3.5f, minAngle, 1.0f, 1.0f, 1.0f);  

    glColor3f(1.0f, 1.0f, 1.0f);
    glDrawCircle(0.0f, 0.0f, 0.03f, 20, true);
}

// --- draw HUD pannel ---
void drawHUD(SYSTEMTIME st) {
    char buf[256];
    float startX = 620.0f; 
    float y = 50.0f;
    float spacing = 32.0f;

    glColor3f(0.0f, 0.8f, 1.0f);
    glPrint(startX, y, "CHRONO GL DASHBOARD");
    y += spacing * 1.5;

    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "SYS TIME : %02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    glPrint(startX, y, buf); y += spacing;

    double swSec = stopwatch.getElapsedSeconds();
    int swH = (int)(swSec / 3600), swM = (int)((swSec - swH * 3600) / 60), swS = (int)swSec % 60, swMs = (int)((swSec - (int)swSec) * 1000);
    sprintf(buf, "STOPWATCH: %02d:%02d:%02d.%03d", swH, swM, swS, swMs);
    glPrint(startX, y, buf); y += spacing;

    double cdRemain = 0;
    if (isCountdownRunning || isCountdownPaused) {
        cdRemain = countdownDuration - countdownTimer.getElapsedSeconds();
        if (cdRemain <= 0) {
            cdRemain = 0; isCountdownRunning = false; isCountdownPaused = false; countdownDuration = 0;
            std::thread([]() {
                MessageBoxA(NULL, "time is up !", "message", MB_OK | MB_ICONINFORMATION);
            }).detach();
        }
    } else { cdRemain = countdownDuration; }
    
    int cdH = (int)(cdRemain / 3600), cdM = (int)((cdRemain - cdH * 3600) / 60), cdS = (int)cdRemain % 60, cdMs = (int)((cdRemain - (int)cdRemain) * 1000);
    
    if (isCountdownPaused) glColor3f(1.0f, 0.8f, 0.0f); 
    sprintf(buf, "COUNTDOWN: %02d:%02d:%02d.%03d %s", cdH, cdM, cdS, cdMs, isCountdownPaused ? "[PAUSED]" : "");
    glPrint(startX, y, buf); y += spacing * 1.5;
    glColor3f(1.0f, 1.0f, 1.0f); 

    glColor3f(0.0f, 0.8f, 1.0f);
    glPrint(startX, y, "--- LAP TIMES (UP/DN) ---"); y += spacing;
    
    glColor3f(0.7f, 0.7f, 0.7f);
    int totalLaps = lapTimes.size();
    int maxDisplay = 4;
    
    
    if (lapScrollOffset < 0) lapScrollOffset = 0;
    if (lapScrollOffset > std::max(0, totalLaps - maxDisplay)) lapScrollOffset = std::max(0, totalLaps - maxDisplay);

    for (int i = 0; i < maxDisplay; i++) {
        
        int idx = totalLaps - 1 - i - lapScrollOffset; 
        if (idx >= 0 && idx < totalLaps) {
            double lSec = lapTimes[idx].totalSec;
            int lH = (int)(lSec / 3600), lM = (int)((lSec - lH * 3600) / 60), lS = (int)lSec % 60, lMs = (int)((lSec - (int)lSec) * 1000);
            sprintf(buf, "L%02d: %02d:%02d:%02d.%03d", idx + 1, lH, lM, lS, lMs);
            glPrint(startX, y, buf); 
            
            if (i == 0 && lapScrollOffset > 0) glPrint(startX + 300, y, "^");
            if (i == maxDisplay - 1 && lapScrollOffset < totalLaps - maxDisplay) glPrint(startX + 300, y, "v");
            y += spacing;
        } else { y += spacing; }
    }

    y += spacing * 0.2;
    glColor3f(0.4f, 0.4f, 0.4f);
    glPrint(startX, y, "[SPACE]: SW START/STOP"); y += spacing;
    glPrint(startX, y, "[  L  ]: SW LAP/RESET"); y += spacing;
    glPrint(startX, y, "[  C  ]: CD SET (MINS)"); y += spacing;
    glPrint(startX, y, "[  P  ]: CD PAUSE/RESUME"); y += spacing;
    glPrint(startX, y, "[  R  ]: CD CLEAR"); y += spacing;

    if (isInputting) {
        glColor3f(1.0f, 0.8f, 0.0f); 
        sprintf(buf, "SET MINS: %s_", inputBuffer.c_str()); 
        glPrint(startX, y, buf);
    }
}

// --- keyboard input ---
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (isInputting) {
            if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9 && action == GLFW_PRESS) inputBuffer += std::to_string(key - GLFW_KEY_0);
            if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9 && action == GLFW_PRESS) inputBuffer += std::to_string(key - GLFW_KEY_KP_0);
            
            
            if ((key == GLFW_KEY_PERIOD || key == GLFW_KEY_KP_DECIMAL) && action == GLFW_PRESS) {
                if (inputBuffer.find('.') == std::string::npos) {
                    if (inputBuffer.empty()) inputBuffer = "0.";
                    else inputBuffer += ".";
                }
            }
            
            if (key == GLFW_KEY_BACKSPACE && !inputBuffer.empty() && action != GLFW_RELEASE) inputBuffer.pop_back();
            if (key == GLFW_KEY_ESCAPE) isInputting = false; 
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                if (!inputBuffer.empty() && inputBuffer != ".") {
                    countdownDuration = std::stod(inputBuffer) * 60.0;
                    countdownTimer.reset();
                    countdownTimer.start();
                    isCountdownRunning = true;
                    isCountdownPaused = false;
                }
                isInputting = false;
            }
        } else {
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                switch (key) {
                    case GLFW_KEY_UP:
                        lapScrollOffset++; 
                        break;
                    case GLFW_KEY_DOWN:
                        lapScrollOffset--; 
                        break;
                }
            }

            if (action == GLFW_PRESS) {
                switch (key) {
                    case GLFW_KEY_SPACE:
                        isStopwatchRunning = !isStopwatchRunning;
                        if (isStopwatchRunning) stopwatch.start();
                        else stopwatch.pause();
                        break;
                    case GLFW_KEY_L:
                        if (isStopwatchRunning) {
                            double curTotal = stopwatch.getElapsedSeconds();
                            double curSplit = curTotal;
                            if (!lapTimes.empty()) curSplit = curTotal - lapTimes.back().totalSec;
                            lapTimes.push_back({curTotal, curSplit});
                            lapScrollOffset = 0; 
                        } else {
                            stopwatch.reset();
                            lapTimes.clear();
                            lapScrollOffset = 0;
                        }
                        break;
                    case GLFW_KEY_C:
                        isInputting = true;
                        inputBuffer = "";
                        break;
                    case GLFW_KEY_P: 
                        if (countdownDuration > 0 && !isInputting) {
                            if (isCountdownRunning) {
                                countdownTimer.pause();
                                isCountdownRunning = false;
                                isCountdownPaused = true;
                            } else if (isCountdownPaused) {
                                countdownTimer.start();
                                isCountdownRunning = true;
                                isCountdownPaused = false;
                            }
                        }
                        break;
                    case GLFW_KEY_R: 
                        isCountdownRunning = false;
                        isCountdownPaused = false;
                        countdownDuration = 0.0;
                        countdownTimer.reset();
                        break;
                }
            }
        }
    }
}

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); 
    GLFWwindow* window = glfwCreateWindow(1000, 600, "ChronoWatch Pro - Pure GUI", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSwapInterval(1); 
    glEnable(GL_MULTISAMPLE); 

    buildSystemFont(window);

    while (!glfwWindowShouldClose(window)) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        glClearColor(0.08f, 0.09f, 0.12f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, 600, 600);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0); 
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        drawWatchDial(st);

        glViewport(0, 0, 1000, 600);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1000.0, 600.0, 0.0, -1.0, 1.0); 
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        drawHUD(st);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
