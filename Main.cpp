#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <iostream>
#include <sstream>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib, "User32.lib")
#pragma comment (lib, "Shcore.lib") 

using namespace Gdiplus;

// ==========================================
//           USER CONFIGURATION
// ==========================================
namespace Config {
    // --- PHYSICS ---
    const int TICK_RATE       = 33;    
    const int GRAVITY         = 3;     
    const int WALK_SPEED      = 4;     
    const int LEAP_SPEED      = 25;    

    // --- AI LOGIC (Cumulative Thresholds out of 10,000) ---
    const int THRESH_IDLE_TO_WALK  = 150; 
    const int THRESH_IDLE_TO_SIT   = 300;  
    const int THRESH_IDLE_TO_SLEEP = 350;  

    const int CHANCE_STOP_WALKING  = 100;  
    const int CHANCE_STAND_UP      = 50;   
    const int CHANCE_WAKE_UP       = 5;    

    // Jump Logic
    const int CHANCE_CHECK_JUMP    = 500;  
    const int JUMP_UP_BIAS         = 70;   
    const float JUMP_RANGE_PCT     = 0.20f; 

    // --- TIMING ---
    const int MIN_STATE_TIME     = 2000; 

    // --- ANIMATION SPEEDS ---
    const int SPEED_WALK      = 150;
    const int SPEED_IDLE      = 800;
    const int SPEED_SIT       = 1000;
    const int SPEED_SLEEP     = 2000;
    const int SPEED_MOVIE     = 1500;
    const int SPEED_JUMP_PREP = 500;
    const int SPEED_AIR       = 50;

    // --- VISUALS ---
    const int BREATH_DEPTH    = 3;     
    const int BREATH_SPEED    = 400;   
}

// ==========================================
//              CORE ENGINE
// ==========================================

enum State {
    IDLE, WALKING, SITTING, SLEEPING, FALLING, PREPARE_JUMP, LEAPING, WATCHING_MOVIE
};

struct AnimSequence {
    std::vector<Image*> frames;
    int msPerFrame;
};

// --- GLOBALS ---
std::map<State, AnimSequence> animations;
State currentState = FALLING;

int currentFrameIndex = 0;
unsigned long lastFrameTime = 0;
unsigned long lastStateChangeTime = 0;
int debugLogCounter = 0;

int posX = 0, posY = 0; 
int velX = 0, velY = 0;
bool facingRight = true;
int targetX = 0, targetY = 0;

struct RectArea { long left, top, right, bottom; };
std::vector<RectArea> monitors;
std::vector<RECT> windowRects; 

ULONG_PTR gdiplusToken;
HWND hBuddyWindow;

// --- UTILS ---
void LogDebug(std::wstring msg) {
    OutputDebugStringW(msg.c_str());
}

std::wstring GetStateName(State s) {
    switch(s) {
        case IDLE: return L"IDLE";
        case WALKING: return L"WALKING";
        case SITTING: return L"SITTING";
        case SLEEPING: return L"SLEEPING";
        case FALLING: return L"FALLING";
        case PREPARE_JUMP: return L"PREPARE_JUMP";
        case LEAPING: return L"LEAPING";
        case WATCHING_MOVIE: return L"WATCHING_MOVIE";
        default: return L"UNKNOWN";
    }
}

// --- STATE MANAGER ---
void ChangeState(State newState, std::wstring reason) {
    if (currentState == newState) return; 

    std::wstringstream ss;
    ss << L"[STATE] " << GetStateName(currentState) << L" -> " << GetStateName(newState) << L" (" << reason << L")\n";
    LogDebug(ss.str());

    currentState = newState;
    lastStateChangeTime = GetTickCount64();
    currentFrameIndex = 0; 
    lastFrameTime = GetTickCount64(); 
}

// --- ENVIRONMENT ---
BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdc, LPRECT lprc, LPARAM dwData) {
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfo(hMon, &mi)) {
        RectArea ra;
        ra.left = mi.rcWork.left;
        ra.right = mi.rcWork.right;
        ra.top = mi.rcWork.top;
        ra.bottom = mi.rcWork.bottom;
        monitors.push_back(ra);
    }
    return TRUE;
}

void UpdateEnvironment() {
    monitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;
    if (hwnd == hBuddyWindow) return TRUE;
    RECT r;
    GetWindowRect(hwnd, &r);
    if ((r.right - r.left) < 200 || (r.bottom - r.top) < 100) return TRUE;
    if (r.bottom < -30000 || r.right < -30000) return TRUE;

    bool isFullScreen = false;
    for(const auto& mon : monitors) {
        if (abs(r.left - mon.left) < 2 && abs(r.right - mon.right) < 2 && 
            abs(r.top - mon.top) < 2 && abs(r.bottom - mon.bottom) < 2) {
            isFullScreen = true;
            break;
        }
    }
    std::vector<RECT>* list = reinterpret_cast<std::vector<RECT>*>(lParam);
    list->push_back(r);
    return TRUE;
}

bool IsInAnyMonitor(int x, int y) {
    for (const auto& mon : monitors) {
        if (x >= mon.left && x <= mon.right && y >= mon.top && y <= mon.bottom) return true;
    }
    return false;
}

// Z-ORDER CHECK:
// Window list is sorted Top-to-Bottom (0 is top).
// We check if any window with index < 'ignoreBelowIndex' covers the point.
bool IsPointObscured(int x, int y, int ignoreBelowIndex) {
    int limit = (ignoreBelowIndex == -1) ? windowRects.size() : ignoreBelowIndex;
    for (int i = 0; i < limit; i++) {
        RECT& r = windowRects[i];
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            return true; 
        }
    }
    return false;
}

void GetSmartSize(int origW, int origH, int& outW, int& outH) {
    int screenH = 1080;
    if (!monitors.empty()) screenH = monitors[0].bottom - monitors[0].top;
    int targetH = screenH / 8; 
    if (origH > targetH) {
        float ratio = (float)targetH / (float)origH;
        outH = targetH;
        outW = (int)(origW * ratio);
    } else {
        int scale = targetH / origH;
        if (scale < 1) scale = 1;
        if (scale > 6) scale = 6;
        outW = origW * scale;
        outH = origH * scale;
    }
}

// --- PHYSICS ---
void UpdatePhysics() {
    if (GetAsyncKeyState(VK_ESCAPE)) { PostQuitMessage(0); return; }

    windowRects.clear();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));

    if (currentState == FALLING) {
        posY += velY;
        velY += Config::GRAVITY;
        if (velY > 25) velY = 25;

        if (velY > 0) {
            // Check Windows
            for (int i = 0; i < windowRects.size(); i++) {
                RECT& r = windowRects[i];
                if (posX >= r.left + 10 && posX <= r.right - 10) {
                    if (posY >= r.top && posY <= (r.top + velY + 15)) {
                        // Landing check: Am I obscured by something ABOVE this window?
                        if (!IsPointObscured(posX, r.top, i)) {
                            posY = r.top;
                            velY = 0;
                            ChangeState(IDLE, L"Landed Window");
                            return;
                        }
                    }
                }
            }
            // Check Floor
            for (const auto& mon : monitors) {
                if (posX >= mon.left && posX <= mon.right) {
                    if (posY >= mon.bottom) {
                        posY = mon.bottom;
                        velY = 0;
                        ChangeState(IDLE, L"Landed Floor");
                        return;
                    }
                }
            }
        }
    }
    else if (currentState == LEAPING) {
        int dx = targetX - posX;
        int dy = targetY - posY;
        float dist = sqrt(dx*dx + dy*dy);

        if (dist < Config::LEAP_SPEED) {
            posX = targetX;
            posY = targetY;
            ChangeState(IDLE, L"Jump Arrived");
        } else {
            float ratio = Config::LEAP_SPEED / dist;
            posX += (int)(dx * ratio);
            posY += (int)(dy * ratio);
        }
    }
    else if (currentState != PREPARE_JUMP) {
        // --- ON GROUND LOGIC ---
        bool supported = false;
        bool onFloor = false;
        int myWindowIndex = -1; 

        // 1. ELEVATOR CHECK (Windows moving UP into feet)
        // Check this BEFORE current support, so rising windows override falling/current pos.
        for (int i = 0; i < windowRects.size(); i++) {
            RECT& r = windowRects[i];
            if (posX >= r.left && posX <= r.right) {
                // If window top is near feet, OR slightly above (meaning it moved up past us)
                // We check a range: Feet-5 (it rose) to Feet+15 (we fell/it fell)
                if (posY >= r.top - 5 && posY <= r.top + 15) {
                    // Critical: Is this new elevator obscured by something ABOVE it?
                    if (!IsPointObscured(posX, r.top, i)) {
                        posY = r.top; // SNAP
                        supported = true;
                        myWindowIndex = i;
                        break; // Found highest support
                    }
                }
            }
        }

        // 2. Floor Check (If no window caught us)
        if (!supported) {
            for (const auto& mon : monitors) {
                if (posX >= mon.left && posX <= mon.right && abs(posY - mon.bottom) < 10) {
                    supported = true;
                    onFloor = true;
                    posY = mon.bottom;
                    break;
                }
            }
        }

        // 3. Occlusion Logic
        if (supported) {
            // Check head level (posY - 20)
            // We ignore windows BELOW our current support (myWindowIndex)
            // This allows us to stand on a window that is in front of another window without panicking.
            if (IsPointObscured(posX, posY - 20, myWindowIndex)) {
                
                // If we are sleeping, we wake up.
                if (currentState == SLEEPING || currentState == WATCHING_MOVIE) {
                    ChangeState(IDLE, L"Woke by Occlusion");
                    if (rand() % 2 == 0) posX += 10; else posX -= 10;
                }
                // If we are just standing/walking, we get pushed off.
                // UNLESS we are on the floor (Taskbar). You can't fall off the floor.
                else if (!onFloor) {
                    supported = false; // Push off ledge
                }
            }
        }

        if (!supported) {
            ChangeState(FALLING, L"No Support");
        } 
        else if (currentState == WALKING) {
            int speed = Config::WALK_SPEED;
            int nextX = facingRight ? (posX + speed) : (posX - speed);
            
            // Just walk. Only stop for monitor edges.
            if (IsInAnyMonitor(nextX, posY - 10)) {
                posX = nextX;
            } else {
                ChangeState(IDLE, L"Screen Edge");
            }
        }
    }
}

// --- AI ---
void UpdateAI() {
    if (currentState == FALLING || currentState == LEAPING) return;

    if (currentState == PREPARE_JUMP) {
        if (rand() % 15 == 0) {
            facingRight = (targetX > posX);
            ChangeState(LEAPING, L"Launch");
        }
        return;
    }

    unsigned long now = GetTickCount64();
    if (now - lastStateChangeTime < Config::MIN_STATE_TIME) return;

    int r = rand() % 10000; 

    if (currentState == IDLE) {
        if (r < Config::THRESH_IDLE_TO_WALK) { 
            facingRight = (rand() % 2 == 0); 
            ChangeState(WALKING, L"AI Walk");
        }
        else if (r < Config::THRESH_IDLE_TO_SIT) ChangeState(SITTING, L"AI Sit");
        else if (r < Config::THRESH_IDLE_TO_SLEEP) ChangeState(SLEEPING, L"AI Sleep");
        
        // JUMP SEARCH
        if (rand() % 10000 < Config::CHANCE_CHECK_JUMP) { 
            int minDim = 10000;
            for(const auto& mon : monitors) {
                int w = mon.right - mon.left;
                int h = mon.bottom - mon.top;
                if (w < minDim) minDim = w;
                if (h < minDim) minDim = h;
            }
            int maxRange = (int)(minDim * Config::JUMP_RANGE_PCT);

            std::vector<POINT> targetsUp;
            std::vector<POINT> targetsDown;

            for (const auto& w : windowRects) {
                int wx = (w.left + w.right) / 2;
                int wy = w.top;
                double dist = sqrt(pow(wx - posX, 2) + pow(wy - posY, 2));
                
                if (dist > maxRange) continue;
                if (abs(wy - posY) < 30) continue; 
                // Don't jump to obscured ledges
                if (IsPointObscured(wx, wy, -1)) continue; 
                bool nearCeiling = false;
                for(const auto& mon : monitors) if (wy < mon.top + 50) nearCeiling = true;
                if (nearCeiling) continue;

                if (wy < posY) targetsUp.push_back({wx, wy});
                else targetsDown.push_back({wx, wy});
            }

            std::vector<POINT>* chosenList = nullptr;
            bool preferUp = (rand() % 100 < Config::JUMP_UP_BIAS);
            if (preferUp && !targetsUp.empty()) chosenList = &targetsUp;
            else if (!targetsDown.empty()) chosenList = &targetsDown;
            else if (!targetsUp.empty()) chosenList = &targetsUp; 

            if (chosenList && !chosenList->empty()) {
                int idx = rand() % chosenList->size();
                targetX = (*chosenList)[idx].x;
                targetY = (*chosenList)[idx].y;
                ChangeState(PREPARE_JUMP, L"Ledge Found");
            }
        }
    }
    else if (currentState == WALKING) { 
        if (r < Config::CHANCE_STOP_WALKING) ChangeState(IDLE, L"Stop Walk");
    }
    else if (currentState == SITTING) {
        if (r < Config::CHANCE_STAND_UP) ChangeState(IDLE, L"Stand Up");
    }
    else if (currentState == SLEEPING) {
        if (r < Config::CHANCE_WAKE_UP) ChangeState(IDLE, L"Wake Up");
    }

    HWND hFg = GetForegroundWindow();
    wchar_t title[256];
    GetWindowText(hFg, title, 256);
    std::wstring wTitle = title;
    std::transform(wTitle.begin(), wTitle.end(), wTitle.begin(), ::tolower);
    bool watching = (wTitle.find(L"youtube") != std::wstring::npos || wTitle.find(L"netflix") != std::wstring::npos);
    
    if (watching && currentState != WATCHING_MOVIE && currentState != FALLING && currentState != LEAPING && currentState != PREPARE_JUMP) {
        ChangeState(WATCHING_MOVIE, L"Detect Movie");
    }
    if (!watching && currentState == WATCHING_MOVIE) {
        ChangeState(IDLE, L"Movie End");
    }
}

// --- RENDER ---
void DrawBuddy(HDC hdcScreen) {
    debugLogCounter++;
    bool doLog = (debugLogCounter % 60 == 0);

    Image* img = nullptr;
    bool usingFallback = false;

    if (animations.find(currentState) != animations.end() && !animations[currentState].frames.empty()) {
        AnimSequence& anim = animations[currentState];
        if (currentFrameIndex >= anim.frames.size()) currentFrameIndex = 0;
        unsigned long now = GetTickCount64();
        if (now - lastFrameTime > anim.msPerFrame) {
            currentFrameIndex = (currentFrameIndex + 1) % anim.frames.size();
            lastFrameTime = now;
        }
        img = anim.frames[currentFrameIndex];
    }
    if (img == nullptr) {
        usingFallback = true;
        if (animations.find(IDLE) != animations.end() && !animations[IDLE].frames.empty()) {
            img = animations[IDLE].frames[0];
        }
    }

    int imgW = 32, imgH = 32;
    if (img) { imgW = img->GetWidth(); imgH = img->GetHeight(); }

    int drawW, drawH;
    GetSmartSize(imgW, imgH, drawW, drawH);

    int breathingOffset = 0;
    if (currentState == SLEEPING || currentState == WATCHING_MOVIE) {
        double timeVal = (double)GetTickCount64() / (double)Config::BREATH_SPEED; 
        breathingOffset = (int)(sin(timeVal) * Config::BREATH_DEPTH + Config::BREATH_DEPTH);
    }

    int drawX = posX - (drawW / 2);
    int drawY = posY - drawH + breathingOffset; 

    if (doLog) {
        std::wstringstream ss;
        ss << L"State: " << GetStateName(currentState)
           << L" | Pos: " << posX << L"," << posY 
           << L" | Tgt: " << targetX << L"," << targetY
           << L" | Fallback: " << (usingFallback ? L"YES" : L"NO") << L"\n";
        LogDebug(ss.str());
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, drawW, drawH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    {
        Bitmap bmp(drawW, drawH, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor); 

        if (img) {
            if (facingRight) {
                g.DrawImage(img, 0, 0, drawW, drawH);
            } else {
                g.TranslateTransform((REAL)drawW, 0);
                g.ScaleTransform(-1, 1);
                g.DrawImage(img, 0, 0, drawW, drawH);
            }
        } else {
            SolidBrush brush(Color(200, 255, 0, 255));
            g.FillRectangle(&brush, 0, 0, drawW, drawH);
        }

        HBITMAP hAlphaBitmap = NULL;
        bmp.GetHBITMAP(Color(0,0,0,0), &hAlphaBitmap);
        if (hAlphaBitmap) {
            HDC hdcAlpha = CreateCompatibleDC(hdcScreen);
            HBITMAP hOldAlpha = (HBITMAP)SelectObject(hdcAlpha, hAlphaBitmap);
            BLENDFUNCTION blend = { 0 };
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            POINT ptPos = { drawX, drawY };
            SIZE sizeWnd = { drawW, drawH };
            POINT ptSrc = { 0, 0 };
            UpdateLayeredWindow(hBuddyWindow, hdcScreen, &ptPos, &sizeWnd, hdcAlpha, &ptSrc, 0, &blend, ULW_ALPHA);
            SelectObject(hdcAlpha, hOldAlpha);
            DeleteDC(hdcAlpha);
            DeleteObject(hAlphaBitmap); 
        }
    }
    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    DeleteObject(hBitmap); 
}

void LoadAnimation(State state, std::wstring baseName, int frameCount, int speedMs) {
    AnimSequence seq;
    seq.msPerFrame = speedMs;
    for (int i = 0; i < frameCount; i++) {
        std::wstring path = L"assets/" + baseName + L"_" + std::to_wstring(i) + L".png";
        Image* img = Image::FromFile(path.c_str());
        if (img && img->GetLastStatus() == Ok) seq.frames.push_back(img);
    }
    if (!seq.frames.empty()) animations[state] = seq;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: SetTimer(hwnd, 1, Config::TICK_RATE, NULL); return 0;
    case WM_DISPLAYCHANGE: UpdateEnvironment(); return 0;
    case WM_TIMER: UpdatePhysics(); UpdateAI(); { HDC hdc = GetDC(NULL); DrawBuddy(hdc); ReleaseDC(NULL, hdc); } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    srand(static_cast<unsigned int>(time(0)));

    UpdateEnvironment();
    if (!monitors.empty()) {
        posX = (monitors[0].left + monitors[0].right) / 2;
        posY = monitors[0].bottom;
    }

    // TODO: Handle animation lenght dynamically based on assets folder contents
    LoadAnimation(WALKING, L"walk", 4, Config::SPEED_WALK); 
    LoadAnimation(FALLING, L"fall", 2, Config::SPEED_AIR);
    LoadAnimation(PREPARE_JUMP, L"sit", 2, Config::SPEED_JUMP_PREP);
    LoadAnimation(LEAPING, L"jump", 4, Config::SPEED_AIR); 
    LoadAnimation(IDLE, L"idle", 2, Config::SPEED_IDLE); 
    LoadAnimation(SITTING, L"sit", 2, Config::SPEED_SIT);
    LoadAnimation(SLEEPING, L"sleep", 3, Config::SPEED_SLEEP); 
    LoadAnimation(WATCHING_MOVIE, L"popcorn", 2, Config::SPEED_MOVIE);

    const wchar_t CLASS_NAME[] = L"DesktopBuddyClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    hBuddyWindow = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, CLASS_NAME, L"Desktop Buddy", WS_POPUP, 0, 0, 10, 10, NULL, NULL, hInstance, NULL);
    if (hBuddyWindow == NULL) return 0;
    ShowWindow(hBuddyWindow, SW_SHOW);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    return 0;
}