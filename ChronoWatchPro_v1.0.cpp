#include <glad/glad.h>
#include <windows.h>

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
#include <fstream>

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

// --- global variables ---
HighResolutionTimer stopwatch;
bool isStopwatchRunning = false;
struct LapRecord { double totalSec; double splitSec; };
std::vector<LapRecord> lapTimes;
int lapScrollOffset = 0; 

bool isCountdownRunning = false;
bool isCountdownPaused = false;
double countdownDuration = 0.0;
HighResolutionTimer countdownTimer;

bool showSaveConfirmation = false;
double saveConfirmationTime = 0.0;

bool isInputting = false;
std::string inputBuffer = "";

GLuint fontBase;
GLuint dialFontBase;
GLYPHMETRICSFLOAT dialGmf[96];

// --- create fonts ---
void buildSystemFont(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    HDC hdc = GetDC(hwnd);

    // 1. HUD
    fontBase = glGenLists(96);
    HFONT fontHUD = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    HGDIOBJ oldFont = SelectObject(hdc, fontHUD);
    wglUseFontBitmapsA(hdc, 32, 96, fontBase);
    SelectObject(hdc, oldFont);
    DeleteObject(fontHUD);

    // 2. Dial
    dialFontBase = glGenLists(96);
    HFONT fontDial = CreateFontA(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    oldFont = SelectObject(hdc, fontDial);
    wglUseFontOutlinesA(hdc, 32, 96, dialFontBase, 0.0f, 0.0f, WGL_FONT_POLYGONS, dialGmf);
    SelectObject(hdc, oldFont);
    DeleteObject(fontDial);

    ReleaseDC(hwnd, hdc);
}

void glPrint(float x, float y, const char* text) {
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(fontBase - 32);
    glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
    glPopAttrib();
}

void glPrintVectorCentered(float cx, float cy, float scale, const char* text) {
    float width = 0.0f;
    int len = strlen(text);
    for(int i = 0; i < len; i++) {
        if (text[i] >= 32 && text[i] < 128) {
            width += dialGmf[text[i] - 32].gmfCellIncX;
        }
    }
    width *= scale;
    float heightOffset = scale * 0.35f; 

    glPushMatrix();
    glTranslatef(cx - width / 2.0f, cy - heightOffset, 0.0f);
    glScalef(scale, scale, 1.0f);
    glPushAttrib(GL_LIST_BIT);
    glListBase(dialFontBase - 32);
    glCallLists(len, GL_UNSIGNED_BYTE, text);
    glPopAttrib();
    glPopMatrix();
}

void glDrawCircle(float cx, float cy, float r, int segments, bool fill) {
    glBegin(fill ? GL_POLYGON : GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
        float theta = 2.0f * PI * float(i) / float(segments);
        glVertex2f(cx + r * cos(theta), cy + r * sin(theta));
    }
    glEnd();
}

void glDrawSubHand(float cx, float cy, float length, float width, float angleDegrees, float r, float g, float b) {
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glRotatef(-angleDegrees, 0.0f, 0.0f, 1.0f);
    glColor3f(r, g, b);
    glLineWidth(width);
    glBegin(GL_LINES);
        glVertex2f(0.0f, 0.0f);
        glVertex2f(0.0f, length);
    glEnd();
    glPopMatrix();
}

void drawSubDialTicks(float cx, float cy, float r, int numTicks, int majorInterval) {
    for (int i = 0; i < numTicks; i++) {
        float angle = 2.0f * PI * float(i) / float(numTicks);
        bool isMajor = (i % majorInterval == 0);
        float len = isMajor ? 0.025f : 0.012f;
        glLineWidth(isMajor ? 2.0f : 1.0f);
        glColor3f(isMajor ? 0.8f : 0.4f, isMajor ? 0.8f : 0.4f, isMajor ? 0.8f : 0.4f);
        glBegin(GL_LINES);
            glVertex2f(cx + (r - len) * cos(angle), cy + (r - len) * sin(angle));
            glVertex2f(cx + r * cos(angle), cy + r * sin(angle));
        glEnd();
    }
}

// --- drawWatchDial ---
void drawWatchDial(SYSTEMTIME st) {
    // Main dial background
    glColor3f(0.12f, 0.14f, 0.18f);
    glDrawCircle(0.0f, 0.0f, 0.90f, 100, true);
    glColor3f(0.0f, 0.5f, 1.0f); 
    glLineWidth(3.5f);
    glDrawCircle(0.0f, 0.0f, 0.90f, 100, false);

    // Main dial ticks
    glColor3f(0.8f, 0.8f, 0.8f);
    for (int i = 0; i < 60; i++) {
        glPushMatrix();
        glRotatef(i * 6.0f, 0.0f, 0.0f, 1.0f);
        float len = (i % 5 == 0) ? 0.06f : 0.02f;
        glLineWidth((i % 5 == 0) ? 2.5f : 1.0f);
        glBegin(GL_LINES);
            glVertex2f(0.0f, 0.90f - len);
            glVertex2f(0.0f, 0.90f);
        glEnd();
        glPopMatrix();
    }

    // Main dial numbers
    glColor3f(0.9f, 0.9f, 0.9f);
    float mainNumRadius = 0.75f; 
    for (int i = 1; i <= 12; i++) {
        float angle = (3.0f - (float)i) * (PI / 6.0f);
        float nx = mainNumRadius * cos(angle);
        float ny = mainNumRadius * sin(angle);
        char b[4]; sprintf(b, "%d", i);
        glPrintVectorCentered(nx, ny, 0.09f, b);
    }

    float subRadius = 0.22f;
    float numRadius = 0.150f; 

    // --- 12 O'Clock: Countdown Sub-dial ---
    float cdX = 0.0f, cdY = 0.42f;
    glColor3f(0.15f, 0.18f, 0.22f);
    glDrawCircle(cdX, cdY, subRadius, 50, true);
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(1.5f);
    glDrawCircle(cdX, cdY, subRadius, 50, false);
    drawSubDialTicks(cdX, cdY, subRadius, 60, 5);

    glColor3f(0.8f, 0.8f, 0.8f);
    for (int i = 1; i <= 12; i++) {
        float angle = (3.0f - (float)i) * (PI / 6.0f);
        float nx = cdX + numRadius * cos(angle);
        float ny = cdY + numRadius * sin(angle);
        char b[4]; sprintf(b, "%d", i);
        glPrintVectorCentered(nx, ny, 0.055f, b); 
    }
    glColor3f(0.3f, 0.6f, 0.9f);
    glPrintVectorCentered(cdX, cdY - 0.07f, 0.032f, "COUNTDOWN");

    double cdRemain = countdownDuration;
    if (isCountdownRunning || isCountdownPaused) {
        cdRemain = countdownDuration - countdownTimer.getElapsedSeconds();
        if (cdRemain < 0) cdRemain = 0;
    }
    float cdHourAngle = fmod(cdRemain / 3600.0, 12.0) * 30.0f;
    float cdMinAngle  = fmod(cdRemain / 60.0, 60.0) * 6.0f;
    float cdSecAngle  = fmod(cdRemain, 60.0) * 6.0f;
    glDrawSubHand(cdX, cdY, 0.11f, 3.5f, cdHourAngle, 1.0f, 0.5f, 0.0f); 
    glDrawSubHand(cdX, cdY, 0.16f, 2.2f, cdMinAngle,  0.2f, 0.8f, 1.0f); 
    glDrawSubHand(cdX, cdY, 0.19f, 1.2f, cdSecAngle,  1.0f, 0.2f, 0.2f); 


    // --- 9 O'Clock: Stopwatch Dial ---
    float swX = -0.42f, swY = 0.0f;
    glColor3f(0.15f, 0.18f, 0.22f);
    glDrawCircle(swX, swY, subRadius, 50, true);
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(1.5f);
    glDrawCircle(swX, swY, subRadius, 50, false);
    drawSubDialTicks(swX, swY, subRadius, 60, 5);

    glColor3f(0.8f, 0.8f, 0.8f);
    for (int i = 1; i <= 12; i++) {
        float angle = (3.0f - (float)i) * (PI / 6.0f);
        float nx = swX + numRadius * cos(angle);
        float ny = swY + numRadius * sin(angle);
        char b[4]; sprintf(b, "%d", i);
        glPrintVectorCentered(nx, ny, 0.055f, b); 
    }
    glColor3f(0.0f, 0.8f, 0.5f);
    glPrintVectorCentered(swX, swY - 0.07f, 0.032f, "STOPWATCH");

    double swTotalSec = stopwatch.getElapsedSeconds();
    float swHourAngle = fmod(swTotalSec / 3600.0, 12.0) * 30.0f;
    float swMinAngle  = fmod(swTotalSec / 60.0, 60.0) * 6.0f;
    float swSecAngle  = fmod(swTotalSec, 60.0) * 6.0f;
    glDrawSubHand(swX, swY, 0.11f, 3.5f, swHourAngle, 0.0f, 0.8f, 1.0f); 
    glDrawSubHand(swX, swY, 0.16f, 2.2f, swMinAngle,  1.0f, 1.0f, 1.0f); 
    glDrawSubHand(swX, swY, 0.19f, 1.2f, swSecAngle,  0.0f, 1.0f, 0.4f); 

    // --- 3 O'Clock: Stopwatch Millisecond Dial ---
    float msX = 0.42f, msY = 0.0f;
    glColor3f(0.15f, 0.18f, 0.22f);
    glDrawCircle(msX, msY, subRadius, 50, true);
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(1.5f);
    glDrawCircle(msX, msY, subRadius, 50, false);
    drawSubDialTicks(msX, msY, subRadius, 100, 10); 

    glColor3f(0.8f, 0.8f, 0.8f);
    float msNumRadius = 0.165f; 
    for (int i = 1; i <= 10; i++) {
        float angle = PI / 2.0f - (float)i * (2.0f * PI / 10.0f);
        float nx = msX + msNumRadius * cos(angle);
        float ny = msY + msNumRadius * sin(angle);
        char b[8]; sprintf(b, "%d", i * 100);
        glPrintVectorCentered(nx, ny, 0.048f, b); 
    }
    glColor3f(1.0f, 0.75f, 0.0f);
    glPrintVectorCentered(msX, msY - 0.07f, 0.032f, "1/1000 SEC");

    float msAngle = (swTotalSec - (long)swTotalSec) * 360.0f;
    glDrawSubHand(msX, msY, 0.19f, 1.8f, msAngle, 1.0f, 0.8f, 0.0f); 

    // --- Main Clock Hands ---
    float hourAngle = (st.wHour % 12 + st.wMinute / 60.0f) * 30.0f;
    float minAngle = (st.wMinute + st.wSecond / 60.0f) * 6.0f;
    glDrawSubHand(0.0f, 0.0f, 0.48f, 5.5f, hourAngle, 1.0f, 1.0f, 1.0f); 
    glDrawSubHand(0.0f, 0.0f, 0.72f, 3.8f, minAngle,  1.0f, 1.0f, 1.0f);  

    // Center Pin
    glColor3f(1.0f, 1.0f, 1.0f);
    glDrawCircle(0.0f, 0.0f, 0.03f, 20, true);
}

// --- draw HUD pannel ---
void drawHUD(SYSTEMTIME st) {
    char buf[256];
    float startX = 620.0f; 
    float y = 45.0f;
    float spacing = 30.0f;

    glColor3f(0.0f, 0.8f, 1.0f);
    glPrint(startX, y, "CHRONO WATCH PRO (GLAD INSIDE)");
    y += spacing * 1.5;

    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "SYS TIME : %02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    glPrint(startX, y, buf); y += spacing;

    double swSec = stopwatch.getElapsedSeconds();
    int swH = (int)(swSec / 3600), swM = (int)((swSec - swH * 3600) / 60), swS = (int)swSec % 60;
    int swMs = (int)((swSec - (int)swSec) * 1000.0);
    sprintf(buf, "STOPWATCH: %02d:%02d:%02d.%03d", swH, swM, swS, swMs);
    glPrint(startX, y, buf); y += spacing;

    double cdRemain = 0;
    if (isCountdownRunning || isCountdownPaused) {
        cdRemain = countdownDuration - countdownTimer.getElapsedSeconds();
        if (cdRemain <= 0) {
            cdRemain = 0; isCountdownRunning = false; isCountdownPaused = false; countdownDuration = 0;
            std::thread([]() {
                for(int i = 0; i < 3; ++i) {
                    Beep(1600, 350);
                    Sleep(150);
                }
                MessageBoxA(NULL, "Time is up!", "Timer Alert", MB_OK | MB_ICONEXCLAMATION);
            }).detach();
        }
    } else { cdRemain = countdownDuration; }
    
    int cdH = (int)(cdRemain / 3600), cdM = (int)((cdRemain - cdH * 3600) / 60), cdS = (int)cdRemain % 60;
    int cdMs = (int)((cdRemain - (int)cdRemain) * 1000.0);
    
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
            // get total times and time lags
            double lSec = lapTimes[idx].totalSec;
            double sSec = lapTimes[idx].splitSec;

            int lH = (int)(lSec / 3600), lM = (int)((lSec - lH * 3600) / 60), lS = (int)lSec % 60;
            int lSub = (int)((lSec - (int)lSec) * 1000.0);

            int sH = (int)(sSec / 3600), sM = (int)((sSec - sH * 3600) / 60), sS = (int)sSec % 60;
            int sSub = (int)((sSec - (int)sSec) * 1000.0);

            // show time lags
            
		if (sH > 0) {
			sprintf(buf, "L%02d: %02d:%02d:%02d.%03d (+%02d:%02d:%02d.%03d)", 
            idx + 1, lH, lM, lS, lSub, sH, sM, sS, sSub);
		} else {
			sprintf(buf, "L%02d: %02d:%02d:%02d.%03d (+%02d:%02d.%03d)", 
            idx + 1, lH, lM, lS, lSub, sM, sS, sSub);
		}
            glPrint(startX, y, buf); 
                        
            if (i == 0 && lapScrollOffset > 0) glPrint(startX + 350, y, "^");
            if (i == maxDisplay - 1 && lapScrollOffset < totalLaps - maxDisplay) glPrint(startX + 350, y, "v");
            y += spacing;
        } else { y += spacing; }
    }

    y += spacing * 0.2;
    if (showSaveConfirmation) {
        if (glfwGetTime() - saveConfirmationTime < 3.0) {
            glColor3f(0.0f, 1.0f, 0.4f); 
            glPrint(startX, y, "RECORDS SAVED TO 'lap_records.txt'!");
            y += spacing;
        } else { showSaveConfirmation = false; }
    }

    glColor3f(0.4f, 0.4f, 0.4f);
    glPrint(startX, y, "[SPACE]: SW START/STOP"); y += spacing;
    glPrint(startX, y, "[  L  ]: SW LAP/RESET"); y += spacing;
    glPrint(startX, y, "[  F  ]: SAVE LAPS TO FILE"); y += spacing;
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
                    case GLFW_KEY_F: 
                        if (!lapTimes.empty()) {
                            std::ofstream outFile("lap_records.txt", std::ios::app);
                            if (outFile.is_open()) {
                                SYSTEMTIME t;
                                GetLocalTime(&t);
                                outFile << "==================================================\n";
                                outFile << " CHRONO GL LAP EXPORT - " << t.wYear << "-" << t.wMonth << "-" << t.wDay << " " << t.wHour << ":" << t.wMinute << ":" << t.wSecond << "\n";
                                outFile << "==================================================\n";
                                for (size_t i = 0; i < lapTimes.size(); ++i) {
                                    double lSec = lapTimes[i].totalSec;
                                    double sSec = lapTimes[i].splitSec;
                                    
                                    int lH = (int)(lSec / 3600), lM = (int)((lSec - lH * 3600) / 60), lS = (int)lSec % 60;
                                    int lMs = (int)((lSec - (int)lSec) * 1000.0);

                                    int sH = (int)(sSec / 3600), sM = (int)((sSec - sH * 3600) / 60), sS = (int)sSec % 60;
                                    int sMs = (int)((sSec - (int)sSec) * 1000.0);

                                    // logfile laps
                                    outFile << "Lap " << (i + 1) << " -> Total: " 
                                            << lH << "h:" << lM << "m:" << lS << "s." << lMs << "ms"
                                            << " | Split: +" << sH << "h:" << sM << "m:" << sS << "s." << sMs << "ms\n";
                                }
                                outFile << "--------------------------------------------------\n\n";
                                outFile.close();
                                showSaveConfirmation = true;
                                saveConfirmationTime = glfwGetTime();
                            }
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); 
    
    GLFWwindow* window = glfwCreateWindow(1000, 600, "ChronoWatch Pro - GLAD High Precision", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Critical Error: Failed to initialize GLAD runtime hooks!" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSwapInterval(1); 
    glEnable(GL_MULTISAMPLE); 

    buildSystemFont(window);

    while (!glfwWindowShouldClose(window)) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        glClearColor(0.08f, 0.09f, 0.12f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT);

        int curWidth, curHeight;
        glfwGetFramebufferSize(window, &curWidth, &curHeight);
        if (curWidth < 1) curWidth = 1;
        if (curHeight < 1) curHeight = 1;

        int dialW = (int)(curWidth * 0.6f);
        glViewport(0, 0, dialW, curHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        
        float aspect = (float)dialW / (float)curHeight;
        if (aspect >= 1.0f) {
            glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
        } else {
            glOrtho(-1.0, 1.0, -1.0 / aspect, 1.0 / aspect, -1.0, 1.0);
        }
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        drawWatchDial(st); 

        glViewport(dialW, 0, curWidth - dialW, curHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(600.0, 1000.0, 600.0, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        drawHUD(st);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}