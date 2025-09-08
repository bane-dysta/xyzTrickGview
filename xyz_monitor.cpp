#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ctime>
#include <iomanip>

// 解决Windows ERROR宏冲突
#ifdef ERROR
#undef ERROR
#endif

// 资源ID定义
#define IDI_MAIN_ICON 101

// 托盘和菜单常量
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1001
#define ID_TRAY_RELOAD 2001
#define ID_TRAY_EXIT 2002
#define ID_TRAY_ABOUT 2003

// 热键ID
#define HOTKEY_XYZ_TO_GVIEW 1
#define HOTKEY_GVIEW_TO_XYZ 2

// 日志级别枚举
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

// 简化的日志类（无mutex，适用于cross-compilation）
class Logger {
private:
    std::ofstream logFile;
    LogLevel currentLevel;
    bool logToConsole;
    bool logToFile;

public:
    Logger() : currentLevel(LogLevel::INFO), logToConsole(true), logToFile(true) {}
    
    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
    
    bool initialize(const std::string& logFilePath, LogLevel level = LogLevel::INFO) {
        currentLevel = level;
        
        // 创建日志目录
        std::filesystem::path logPath(logFilePath);
        if (logPath.has_parent_path()) {
            try {
                std::filesystem::create_directories(logPath.parent_path());
            } catch (const std::exception& e) {
                std::cerr << "Failed to create log directory: " << e.what() << std::endl;
            }
        }
        
        logFile.open(logFilePath, std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath << std::endl;
            logToFile = false;
            return false;
        }
        
        // 写入启动分隔符
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        logFile << "\n========================================\n";
        logFile << "XYZ Monitor started at: " << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "\n";
        logFile << "========================================\n";
        logFile.flush();
        
        return true;
    }
    
    void setLogToConsole(bool enabled) {
        logToConsole = enabled;
    }
    
    void setLogToFile(bool enabled) {
        logToFile = enabled;
    }
    
    void setLogLevel(LogLevel level) {
        currentLevel = level;
    }
    
    void log(LogLevel level, const std::string& message, const std::string& file = "", int line = 0) {
        if (level < currentLevel) return;
        
        // 获取当前时间
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        
        // 构建日志消息
        std::ostringstream oss;
        oss << "[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "] ";
        
        // 添加日志级别
        switch (level) {
            case LogLevel::DEBUG:   oss << "[DEBUG] "; break;
            case LogLevel::INFO:    oss << "[INFO]  "; break;
            case LogLevel::WARNING: oss << "[WARN]  "; break;
            case LogLevel::ERROR:   oss << "[ERROR] "; break;
        }
        
        oss << message;
        
        // 添加文件和行号信息（用于错误和警告）
        if (!file.empty() && line > 0 && (level >= LogLevel::WARNING)) {
            // 只提取文件名，不包含完整路径
            std::string filename = file;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            oss << " (" << filename << ":" << line << ")";
        }
        
        std::string logMessage = oss.str();
        
        // 输出到控制台
        if (logToConsole) {
            if (level >= LogLevel::ERROR) {
                std::cerr << logMessage << std::endl;
            } else {
                std::cout << logMessage << std::endl;
            }
        }
        
        // 输出到文件
        if (logToFile && logFile.is_open()) {
            logFile << logMessage << std::endl;
            logFile.flush();
        }
    }
};

// 全局日志实例
Logger g_logger;

// 日志宏定义
#define LOG_DEBUG(msg) g_logger.log(LogLevel::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) g_logger.log(LogLevel::INFO, msg)
#define LOG_WARNING(msg) g_logger.log(LogLevel::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) g_logger.log(LogLevel::ERROR, msg, __FILE__, __LINE__)

// 配置结构体
struct Config {
    std::string hotkey = "CTRL+SHIFT+V";
    std::string hotkeyReverse = "CTRL+SHIFT+G";  // 新增：反向转换热键
    std::string gviewPath = "";
    std::string tempDir = "";
    std::string logFile = "logs/xyz_monitor.log";
    std::string gaussianClipboardPath = "";  // 新增：Gaussian clipboard文件路径
    int waitSeconds = 5;
    std::string logLevel = "INFO";
    bool logToConsole = true;
    bool logToFile = true;
    // 新增内存配置项
    int maxMemoryMB = 500;  // 默认500MB
    size_t maxClipboardChars = 0;  // 自动计算，0表示使用内存计算
};

// 原子结构体
struct Atom {
    std::string symbol;
    double x, y, z;
};

// 帧结构体
struct Frame {
    std::vector<Atom> atoms;
    std::string comment;
};

// 线程参数结构体
struct DeleteFileThreadParams {
    std::string filepath;
    int waitSeconds;
};

// 全局变量
Config g_config;
HHOOK g_hHook = NULL;
bool g_running = true;
NOTIFYICONDATAA g_nid = {};
HWND g_hwnd = NULL;

// 原子序数映射
std::map<std::string, int> atomicNumbers = {
    {"H", 1}, {"He", 2}, {"Li", 3}, {"Be", 4}, {"B", 5}, {"C", 6}, {"N", 7}, {"O", 8}, {"F", 9}, {"Ne", 10},
    {"Na", 11}, {"Mg", 12}, {"Al", 13}, {"Si", 14}, {"P", 15}, {"S", 16}, {"Cl", 17}, {"Ar", 18}, {"K", 19}, {"Ca", 20},
    {"Sc", 21}, {"Ti", 22}, {"V", 23}, {"Cr", 24}, {"Mn", 25}, {"Fe", 26}, {"Co", 27}, {"Ni", 28}, {"Cu", 29}, {"Zn", 30},
    {"Ga", 31}, {"Ge", 32}, {"As", 33}, {"Se", 34}, {"Br", 35}, {"Kr", 36}, {"Rb", 37}, {"Sr", 38}, {"Y", 39}, {"Zr", 40},
    {"Nb", 41}, {"Mo", 42}, {"Tc", 43}, {"Ru", 44}, {"Rh", 45}, {"Pd", 46}, {"Ag", 47}, {"Cd", 48}, {"In", 49}, {"Sn", 50},
    {"Sb", 51}, {"Te", 52}, {"I", 53}, {"Xe", 54}, {"Cs", 55}, {"Ba", 56}, {"La", 57}, {"Ce", 58}, {"Pr", 59}, {"Nd", 60},
    {"Pm", 61}, {"Sm", 62}, {"Eu", 63}, {"Gd", 64}, {"Tb", 65}, {"Dy", 66}, {"Ho", 67}, {"Er", 68}, {"Tm", 69}, {"Yb", 70},
    {"Lu", 71}, {"Hf", 72}, {"Ta", 73}, {"W", 74}, {"Re", 75}, {"Os", 76}, {"Ir", 77}, {"Pt", 78}, {"Au", 79}, {"Hg", 80},
    {"Tl", 81}, {"Pb", 82}, {"Bi", 83}, {"Po", 84}, {"At", 85}, {"Rn", 86}, {"Fr", 87}, {"Ra", 88}, {"Ac", 89}, {"Th", 90},
    {"Pa", 91}, {"U", 92}, {"Np", 93}, {"Pu", 94}, {"Am", 95}, {"Cm", 96}, {"Bk", 97}, {"Cf", 98}, {"Es", 99}, {"Fm", 100},
    {"Md", 101}, {"No", 102}, {"Lr", 103}, {"Rf", 104}, {"Db", 105}, {"Sg", 106}, {"Bh", 107}, {"Hs", 108}, {"Mt", 109}, {"Ds", 110},
    {"Rg", 111}, {"Cn", 112}, {"Nh", 113}, {"Fl", 114}, {"Mc", 115}, {"Lv", 116}, {"Ts", 117}, {"Og", 118}
};

// 新增：原子序数到元素符号的映射
std::map<int, std::string> atomicNumberToSymbol = {
    {1, "H"}, {2, "He"}, {3, "Li"}, {4, "Be"}, {5, "B"}, {6, "C"}, {7, "N"}, {8, "O"},
    {9, "F"}, {10, "Ne"}, {11, "Na"}, {12, "Mg"}, {13, "Al"}, {14, "Si"}, {15, "P"}, {16, "S"},
    {17, "Cl"}, {18, "Ar"}, {19, "K"}, {20, "Ca"}, {21, "Sc"}, {22, "Ti"}, {23, "V"}, {24, "Cr"},
    {25, "Mn"}, {26, "Fe"}, {27, "Co"}, {28, "Ni"}, {29, "Cu"}, {30, "Zn"}, {31, "Ga"}, {32, "Ge"},
    {33, "As"}, {34, "Se"}, {35, "Br"}, {36, "Kr"}, {37, "Rb"}, {38, "Sr"}, {39, "Y"}, {40, "Zr"},
    {41, "Nb"}, {42, "Mo"}, {43, "Tc"}, {44, "Ru"}, {45, "Rh"}, {46, "Pd"}, {47, "Ag"}, {48, "Cd"},
    {49, "In"}, {50, "Sn"}, {51, "Sb"}, {52, "Te"}, {53, "I"}, {54, "Xe"}, {55, "Cs"}, {56, "Ba"},
    {57, "La"}, {58, "Ce"}, {59, "Pr"}, {60, "Nd"}, {61, "Pm"}, {62, "Sm"}, {63, "Eu"}, {64, "Gd"},
    {65, "Tb"}, {66, "Dy"}, {67, "Ho"}, {68, "Er"}, {69, "Tm"}, {70, "Yb"}, {71, "Lu"}, {72, "Hf"},
    {73, "Ta"}, {74, "W"}, {75, "Re"}, {76, "Os"}, {77, "Ir"}, {78, "Pt"}, {79, "Au"}, {80, "Hg"},
    {81, "Tl"}, {82, "Pb"}, {83, "Bi"}, {84, "Po"}, {85, "At"}, {86, "Rn"}
};

// 前置声明
bool reloadConfiguration();
void cleanupTrayIcon();

size_t calculateMaxChars(int memoryMB) {
    const int BYTES_PER_CHAR = 8;
    size_t totalBytes = static_cast<size_t>(memoryMB) * 1024 * 1024;
    size_t maxChars = totalBytes / BYTES_PER_CHAR;
    
    const size_t MIN_CHARS = 10000;      // 最少1万字符
    const size_t MAX_CHARS = 100000000;  // 最多1亿字符
    
    if (maxChars < MIN_CHARS) maxChars = MIN_CHARS;
    if (maxChars > MAX_CHARS) maxChars = MAX_CHARS;
    
    return maxChars;
}

// 工具函数：字符串修整
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

// 工具函数：字符串分割
std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            tokens.push_back(trimmed);
        }
    }
    return tokens;
}

// 工具函数：字符串分割（多个分隔符）
std::vector<std::string> splitWhitespace(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// 字符串转日志级别
LogLevel stringToLogLevel(const std::string& levelStr) {
    std::string upper = levelStr;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "INFO") return LogLevel::INFO;
    if (upper == "WARNING" || upper == "WARN") return LogLevel::WARNING;
    if (upper == "ERROR") return LogLevel::ERROR;
    
    return LogLevel::INFO; // 默认
}

// 获取原子序数
int getAtomicNumber(const std::string& symbol) {
    std::string processed = symbol;
    processed = trim(processed);
    
    if (!processed.empty()) {
        processed[0] = std::toupper(processed[0]);
        for (size_t i = 1; i < processed.length(); ++i) {
            processed[i] = std::tolower(processed[i]);
        }
    }
    
    auto it = atomicNumbers.find(processed);
    return (it != atomicNumbers.end()) ? it->second : 0;
}

// 延时删除文件的线程函数
DWORD WINAPI DeleteFileThread(LPVOID lpParam) {
    DeleteFileThreadParams* params = static_cast<DeleteFileThreadParams*>(lpParam);
    
    try {
        Sleep(params->waitSeconds * 1000);
        
        if (DeleteFileA(params->filepath.c_str())) {
            // 成功删除
        } else {
            DWORD error = GetLastError();
            std::cerr << "Failed to delete temporary file: " << params->filepath << " (Error: " << error << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in delete file thread: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in delete file thread" << std::endl;
    }
    
    delete params;
    return 0;
}

// 读取配置文件
bool loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        // 创建默认配置文件
        std::ofstream outFile(configFile);
        if (outFile.is_open()) {
            outFile << "hotkey=CTRL+SHIFT+V\n";
            outFile << "hotkey_reverse=CTRL+SHIFT+G\n";
            outFile << "gview_path=gview.exe\n";
            outFile << "gaussian_clipboard_path=Clipboard.frg\n";
            outFile << "temp_dir=temp\n";
            outFile << "log_file=logs/xyz_monitor.log\n";
            outFile << "log_level=INFO\n";
            outFile << "log_to_console=true\n";
            outFile << "log_to_file=true\n";
            outFile << "wait_seconds=5\n";
            outFile << "# Memory limit in MB for processing (default: 500MB)\n";
            outFile << "max_memory_mb=500\n";
            outFile << "# Optional: set explicit character limit (0 = auto calculate from memory)\n";
            outFile << "max_clipboard_chars=0\n";
            outFile.close();
            std::cout << "Created default config file: " << configFile << std::endl;
        } else {
            std::cerr << "Failed to create default config file: " << configFile << std::endl;
        }
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            try {
                if (key == "hotkey") {
                    g_config.hotkey = value;
                } else if (key == "hotkey_reverse") {
                    g_config.hotkeyReverse = value;
                } else if (key == "gview_path") {
                    g_config.gviewPath = value;
                } else if (key == "gaussian_clipboard_path") {
                    g_config.gaussianClipboardPath = value;
                } else if (key == "temp_dir") {
                    g_config.tempDir = value;
                } else if (key == "log_file") {
                    g_config.logFile = value;
                } else if (key == "log_level") {
                    g_config.logLevel = value;
                } else if (key == "log_to_console") {
                    g_config.logToConsole = (value == "true" || value == "1");
                } else if (key == "log_to_file") {
                    g_config.logToFile = (value == "true" || value == "1");
                } else if (key == "wait_seconds") {
                    g_config.waitSeconds = std::stoi(value);
                } else if (key == "max_memory_mb") {
                    g_config.maxMemoryMB = std::stoi(value);
                    if (g_config.maxMemoryMB < 50) {
                        LOG_WARNING("max_memory_mb is too small (" + std::to_string(g_config.maxMemoryMB) + "), setting to 50MB");
                        g_config.maxMemoryMB = 50;
                    }
                } else if (key == "max_clipboard_chars") {
                    size_t charLimit = std::stoull(value);
                    g_config.maxClipboardChars = charLimit;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Error parsing config value for key '" + key + "': " + std::string(e.what()));
            }
        }
    }
    file.close();
    
    if (g_config.maxClipboardChars == 0) {
        g_config.maxClipboardChars = calculateMaxChars(g_config.maxMemoryMB);
    }
    
    return true;
}

// 解析热键字符串
bool parseHotkey(const std::string& hotkeyStr, UINT& modifiers, UINT& vk) {
    modifiers = 0;
    vk = 0;
    
    try {
        std::vector<std::string> parts = split(hotkeyStr, '+');
        if (parts.empty()) {
            LOG_ERROR("Empty hotkey string");
            return false;
        }
        
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            std::string mod = parts[i];
            std::transform(mod.begin(), mod.end(), mod.begin(), ::toupper);
            
            if (mod == "CTRL") {
                modifiers |= MOD_CONTROL;
            } else if (mod == "ALT") {
                modifiers |= MOD_ALT;
            } else if (mod == "SHIFT") {
                modifiers |= MOD_SHIFT;
            } else if (mod == "WIN") {
                modifiers |= MOD_WIN;
            } else {
                LOG_ERROR("Unknown modifier: " + mod);
                return false;
            }
        }
        
        std::string key = parts.back();
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        
        if (key.length() == 1) {
            vk = key[0];
        } else if (key == "F1") vk = VK_F1;
        else if (key == "F2") vk = VK_F2;
        else if (key == "F3") vk = VK_F3;
        else if (key == "F4") vk = VK_F4;
        else if (key == "F5") vk = VK_F5;
        else if (key == "F6") vk = VK_F6;
        else if (key == "F7") vk = VK_F7;
        else if (key == "F8") vk = VK_F8;
        else if (key == "F9") vk = VK_F9;
        else if (key == "F10") vk = VK_F10;
        else if (key == "F11") vk = VK_F11;
        else if (key == "F12") vk = VK_F12;
        else {
            LOG_ERROR("Unknown key: " + key);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception parsing hotkey: " + std::string(e.what()));
        return false;
    }
}

// 重新注册热键
bool reregisterHotkeys() {
    if (g_hwnd) {
        // 先注销旧热键
        UnregisterHotKey(g_hwnd, HOTKEY_XYZ_TO_GVIEW);
        UnregisterHotKey(g_hwnd, HOTKEY_GVIEW_TO_XYZ);
        
        // 注册主热键（XYZ到GView）
        UINT modifiers, vk;
        if (parseHotkey(g_config.hotkey, modifiers, vk)) {
            if (RegisterHotKey(g_hwnd, HOTKEY_XYZ_TO_GVIEW, modifiers, vk)) {
                LOG_INFO("Primary hotkey registered: " + g_config.hotkey);
            } else {
                DWORD error = GetLastError();
                LOG_ERROR("Failed to register primary hotkey: " + g_config.hotkey + " (Error: " + std::to_string(error) + ")");
                return false;
            }
        }
        
        // 注册反向热键（GView到XYZ）
        if (parseHotkey(g_config.hotkeyReverse, modifiers, vk)) {
            if (RegisterHotKey(g_hwnd, HOTKEY_GVIEW_TO_XYZ, modifiers, vk)) {
                LOG_INFO("Reverse hotkey registered: " + g_config.hotkeyReverse);
            } else {
                DWORD error = GetLastError();
                LOG_ERROR("Failed to register reverse hotkey: " + g_config.hotkeyReverse + " (Error: " + std::to_string(error) + ")");
                // 主热键已注册，不返回false
            }
        }
        
        return true;
    }
    return false;
}

// 重新加载配置
bool reloadConfiguration() {
    LOG_INFO("Reloading configuration...");
    
    try {
        std::string oldHotkey = g_config.hotkey;
        std::string oldHotkeyReverse = g_config.hotkeyReverse;
        std::string oldLogLevel = g_config.logLevel;
        bool oldLogToConsole = g_config.logToConsole;
        bool oldLogToFile = g_config.logToFile;
        
        if (!loadConfig("config.ini")) {
            LOG_WARNING("Failed to reload config file, using existing configuration");
            return false;
        }
        
        // 更新日志设置
        if (oldLogLevel != g_config.logLevel) {
            LogLevel newLevel = stringToLogLevel(g_config.logLevel);
            g_logger.setLogLevel(newLevel);
            LOG_INFO("Log level changed to: " + g_config.logLevel);
        }
        
        if (oldLogToConsole != g_config.logToConsole) {
            g_logger.setLogToConsole(g_config.logToConsole);
            LOG_INFO("Console logging changed to: " + std::string(g_config.logToConsole ? "enabled" : "disabled"));
        }
        
        if (oldLogToFile != g_config.logToFile) {
            g_logger.setLogToFile(g_config.logToFile);
            LOG_INFO("File logging changed to: " + std::string(g_config.logToFile ? "enabled" : "disabled"));
        }
        
        // 如果热键改变了，重新注册
        if (oldHotkey != g_config.hotkey || oldHotkeyReverse != g_config.hotkeyReverse) {
            if (reregisterHotkeys()) {
                LOG_INFO("Hotkeys re-registered successfully");
            }
        }
        
        LOG_INFO("Configuration reloaded successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while reloading configuration: " + std::string(e.what()));
        return false;
    }
}

// 创建托盘图标
bool createTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
    if (!g_nid.hIcon) {
        LOG_WARNING("Failed to load custom icon, using default system icon");
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    } else {
        LOG_INFO("Loaded custom icon successfully");
    }
    
    strcpy_s(g_nid.szTip, sizeof(g_nid.szTip), "XYZ Monitor - XYZ<->GView Bridge");
    
    bool result = Shell_NotifyIconA(NIM_ADD, &g_nid);
    if (result) {
        LOG_INFO("System tray icon created");
    } else {
        LOG_ERROR("Failed to create system tray icon");
    }
    
    return result;
}

// 显示托盘菜单
void showTrayMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_ABOUT, "XYZ Monitor v1.1 - by Bane Dysta");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_RELOAD, "Reload Configuration");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
        
        SetMenuDefaultItem(hMenu, ID_TRAY_ABOUT, FALSE);
        
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        
        DestroyMenu(hMenu);
    }
}

// 清理托盘图标
void cleanupTrayIcon() {
    if (g_nid.cbSize > 0) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        LOG_DEBUG("System tray icon removed");
    }
}

// 显示关于对话框
void showAboutDialog(HWND hwnd) {
    std::string message = "XYZ Monitor v1.1\n";
    message += "Author: Bane Dysta\n\n";
    message += "Bidirectional XYZ <-> GView converter.\n\n";
    message += "Current Settings:\n";
    message += "XYZ->GView: " + g_config.hotkey + "\n";
    message += "GView->XYZ: " + g_config.hotkeyReverse + "\n";
    message += "GView Path: " + (g_config.gviewPath.empty() ? "Not configured" : g_config.gviewPath) + "\n";
    message += "Gaussian Clipboard: " + (g_config.gaussianClipboardPath.empty() ? "Not configured" : g_config.gaussianClipboardPath) + "\n";
    message += "Log Level: " + g_config.logLevel + "\n\n";
    message += "Feedback:\n";
    message += "GitHub: https://github.com/bane-dysta/xyzTrickGview\n";
    message += "Forum: http://bbs.keinsci.com/forum.php?mod=viewthread&tid=55596&fromuid=63020\n\n";
    message += "Right-click tray icon for options.";
    
    MessageBoxA(hwnd, message.c_str(), "About XYZ Monitor", MB_OK | MB_ICONINFORMATION);
}

// 读取剪贴板内容
std::string getClipboardText() {
    try {
        if (!OpenClipboard(NULL)) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to open clipboard (Error: " + std::to_string(error) + ")");
            return "";
        }
        
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == NULL) {
            CloseClipboard();
            LOG_DEBUG("No text data in clipboard");
            return "";
        }
        
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText == NULL) {
            CloseClipboard();
            LOG_ERROR("Failed to lock clipboard data");
            return "";
        }
        
        std::string text(pszText);
        GlobalUnlock(hData);
        CloseClipboard();
        
        LOG_DEBUG("Clipboard text length: " + std::to_string(text.length()));
        return text;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception reading clipboard: " + std::string(e.what()));
        return "";
    }
}

// 新增：写入剪贴板
bool writeToClipboard(const std::string& text) {
    try {
        if (!OpenClipboard(NULL)) {
            LOG_ERROR("Cannot open clipboard for writing");
            return false;
        }
        
        EmptyClipboard();
        
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.length() + 1);
        if (hMem == NULL) {
            LOG_ERROR("Cannot allocate memory for clipboard");
            CloseClipboard();
            return false;
        }
        
        char* pMem = static_cast<char*>(GlobalLock(hMem));
        if (pMem == NULL) {
            LOG_ERROR("Cannot lock memory for clipboard");
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }
        
        strcpy_s(pMem, text.length() + 1, text.c_str());
        GlobalUnlock(hMem);
        
        if (SetClipboardData(CF_TEXT, hMem) == NULL) {
            LOG_ERROR("Cannot set clipboard data");
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }
        
        CloseClipboard();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception writing to clipboard: " + std::string(e.what()));
        return false;
    }
}

// 新增：解析Gaussian clipboard文件
std::vector<Atom> parseGaussianClipboard(const std::string& filename) {
    std::vector<Atom> atoms;
    
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open Gaussian clipboard file: " + filename);
            return atoms;
        }
        
        std::string line;
        
        // 跳过第一行（头部）
        if (!std::getline(file, line)) {
            LOG_ERROR("Empty file or cannot read header");
            return atoms;
        }
        
        // 读取原子数量
        if (!std::getline(file, line)) {
            LOG_ERROR("Cannot read number of atoms");
            return atoms;
        }
        
        int numAtoms;
        try {
            numAtoms = std::stoi(line);
            LOG_DEBUG("Expected number of atoms: " + std::to_string(numAtoms));
        } catch (const std::exception& e) {
            LOG_ERROR("Cannot parse number of atoms: " + line);
            return atoms;
        }
        
        // 读取原子数据
        for (int i = 0; i < numAtoms; i++) {
            if (!std::getline(file, line)) {
                LOG_WARNING("Expected " + std::to_string(numAtoms) + " atoms, but only found " + std::to_string(i));
                break;
            }
            
            std::istringstream iss(line);
            int atomicNumber;
            double x, y, z;
            std::string label;
            
            if (iss >> atomicNumber >> x >> y >> z) {
                // 可选的标签
                iss >> label;
                
                auto it = atomicNumberToSymbol.find(atomicNumber);
                if (it != atomicNumberToSymbol.end()) {
                    Atom atom;
                    atom.symbol = it->second;
                    atom.x = x;
                    atom.y = y;
                    atom.z = z;
                    atoms.push_back(atom);
                    
                    LOG_DEBUG("Added atom " + std::to_string(i + 1) + ": " + atom.symbol + 
                             " (" + std::to_string(atomicNumber) + ") at (" + 
                             std::to_string(atom.x) + ", " + std::to_string(atom.y) + ", " + std::to_string(atom.z) + ")");
                } else {
                    LOG_WARNING("Unknown atomic number " + std::to_string(atomicNumber) + " in line: " + line);
                }
            } else {
                LOG_WARNING("Cannot parse atom data in line: " + line);
            }
        }
        
        file.close();
        LOG_INFO("Parsed " + std::to_string(atoms.size()) + " atoms from Gaussian clipboard");
    } catch (const std::exception& e) {
        LOG_ERROR("Exception parsing Gaussian clipboard: " + std::string(e.what()));
    }
    
    return atoms;
}

// 新增：创建XYZ字符串
std::string createXYZString(const std::vector<Atom>& atoms) {
    try {
        std::ostringstream oss;
        
        oss << atoms.size() << std::endl;
        oss << "Converted from Gaussian clipboard" << std::endl;
        
        for (const auto& atom : atoms) {
            oss << std::left << std::setw(2) << atom.symbol 
                << " " << std::right << std::setw(12) << std::fixed << std::setprecision(6) << atom.x
                << " " << std::right << std::setw(12) << std::fixed << std::setprecision(6) << atom.y
                << " " << std::right << std::setw(12) << std::fixed << std::setprecision(6) << atom.z
                << std::endl;
        }
        
        return oss.str();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception creating XYZ string: " + std::string(e.what()));
        return "";
    }
}

// 检查是否为有效的坐标行
bool isValidCoordinateLine(const std::string& line) {
    std::vector<std::string> parts = splitWhitespace(line);
    if (parts.size() < 4) return false;
    
    try {
        double x = std::stod(parts[1]);
        double y = std::stod(parts[2]);
        double z = std::stod(parts[3]);
        (void)x; (void)y; (void)z;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// 检查是否为简化XYZ格式
bool isSimplifiedXYZFormat(const std::vector<std::string>& lines) {
    if (lines.empty()) return false;
    
    size_t maxCheck = std::min(static_cast<size_t>(5), lines.size());
    for (size_t i = 0; i < maxCheck; ++i) {
        if (!isValidCoordinateLine(lines[i])) {
            return false;
        }
    }
    
    return true;
}

// 检查是否为XYZ格式
bool isXYZFormat(const std::string& content) {
    try {
        if (content.empty()) {
            LOG_DEBUG("Content is empty");
            return false;
        }
        
        if (content.find('\0') != std::string::npos) {
            LOG_DEBUG("Content contains binary data");
            return false;
        }
        
        std::vector<std::string> lines = split(content, '\n');
        if (lines.empty()) {
            LOG_DEBUG("No lines found in content");
            return false;
        }
        
        // 检查是否是标准XYZ格式（第一行是原子数）
        try {
            int atomCount = std::stoi(lines[0]);
            if (atomCount > 0 && atomCount <= 10000) {
                if (lines.size() < static_cast<size_t>(atomCount + 2)) {
                    LOG_DEBUG("Not enough lines for atom count: " + std::to_string(atomCount));
                    return false;
                }
                
                size_t maxCheck = std::min(static_cast<size_t>(5), static_cast<size_t>(atomCount));
                for (size_t i = 0; i < maxCheck; ++i) {
                    if (i + 2 < lines.size()) {
                        if (!isValidCoordinateLine(lines[i + 2])) {
                            LOG_DEBUG("Invalid coordinate line at index: " + std::to_string(i + 2));
                            return false;
                        }
                    }
                }
                LOG_DEBUG("Detected standard XYZ format");
                return true;
            }
        } catch (const std::exception&) {
            LOG_DEBUG("First line is not atom count, checking simplified format");
        }
        
        bool isSimplified = isSimplifiedXYZFormat(lines);
        if (isSimplified) {
            LOG_DEBUG("Detected simplified XYZ format");
        } else {
            LOG_DEBUG("Not recognized as XYZ format");
        }
        return isSimplified;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in isXYZFormat: " + std::string(e.what()));
        return false;
    }
}

// 读取单帧XYZ数据
bool readXYZFrame(const std::vector<std::string>& lines, size_t startLine, Frame& frame, size_t& nextStart) {
    if (startLine >= lines.size()) return false;
    
    try {
        int numAtoms = std::stoi(lines[startLine]);
        if (numAtoms <= 0) return false;
        
        frame.comment = (startLine + 1 < lines.size()) ? lines[startLine + 1] : "";
        frame.atoms.clear();
        
        for (int i = 0; i < numAtoms; ++i) {
            size_t lineIndex = startLine + 2 + static_cast<size_t>(i);
            if (lineIndex >= lines.size()) break;
            
            std::vector<std::string> parts = splitWhitespace(lines[lineIndex]);
            if (parts.size() >= 4) {
                try {
                    Atom atom;
                    atom.symbol = parts[0];
                    atom.x = std::stod(parts[1]);
                    atom.y = std::stod(parts[2]);
                    atom.z = std::stod(parts[3]);
                    frame.atoms.push_back(atom);
                } catch (const std::exception& e) {
                    LOG_WARNING("Failed to parse atom at line " + std::to_string(lineIndex) + ": " + std::string(e.what()));
                    continue;
                }
            }
        }
        
        nextStart = startLine + static_cast<size_t>(numAtoms) + 2;
        return !frame.atoms.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in readXYZFrame: " + std::string(e.what()));
        return false;
    }
}

// 读取多帧XYZ数据
std::vector<Frame> readMultiXYZ(const std::string& content) {
    std::vector<Frame> frames;
    
    try {
        std::vector<std::string> lines = split(content, '\n');
        
        if (lines.empty()) {
            LOG_DEBUG("No lines to process");
            return frames;
        }
        
        try {
            std::stoi(lines[0]);
            // 标准格式
            LOG_DEBUG("Processing standard XYZ format");
            size_t lineIndex = 0;
            while (lineIndex < lines.size()) {
                Frame frame;
                size_t nextStart;
                if (readXYZFrame(lines, lineIndex, frame, nextStart)) {
                    frames.push_back(frame);
                    lineIndex = nextStart;
                } else {
                    LOG_WARNING("Failed to read frame starting at line: " + std::to_string(lineIndex));
                    break;
                }
            }
        } catch (const std::exception&) {
            // 简化格式：直接处理坐标行
            LOG_DEBUG("Processing simplified XYZ format");
            Frame frame;
            frame.comment = "Simplified XYZ format";
            
            for (const std::string& line : lines) {
                std::vector<std::string> parts = splitWhitespace(line);
                if (parts.size() >= 4) {
                    try {
                        Atom atom;
                        atom.symbol = parts[0];
                        atom.x = std::stod(parts[1]);
                        atom.y = std::stod(parts[2]);
                        atom.z = std::stod(parts[3]);
                        frame.atoms.push_back(atom);
                    } catch (const std::exception& e) {
                        LOG_WARNING("Failed to parse simplified format line: " + std::string(e.what()));
                        continue;
                    }
                }
            }
            
            if (!frame.atoms.empty()) {
                frames.push_back(frame);
            }
        }
        
        LOG_INFO("Processed " + std::to_string(frames.size()) + " frames");
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in readMultiXYZ: " + std::string(e.what()));
    }
    
    return frames;
}

// 写入Gaussian LOG头部
std::string writeGaussianLogHeader() {
    return " ! This file was generated by XYZ Monitor\n"
           " \n"
           " 0 basis functions\n"
           " 0 alpha electrons\n"
           " 0 beta electrons\n"
           "GradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGrad\n";
}

// 写入Gaussian LOG几何结构部分
std::string writeGaussianLogGeometry(const Frame& frame, int frameNumber) {
    std::ostringstream oss;
    
    oss << "GradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGrad\n";
    oss << " \n";
    oss << "                         Standard orientation:\n";
    oss << " ---------------------------------------------------------------------\n";
    oss << " Center     Atomic      Atomic             Coordinates (Angstroms)\n";
    oss << " Number     Number       Type             X           Y           Z\n";
    oss << " ---------------------------------------------------------------------\n";
    
    for (size_t i = 0; i < frame.atoms.size(); ++i) {
        const Atom& atom = frame.atoms[i];
        int atomicNum = getAtomicNumber(atom.symbol);
        
        oss << "      " << (i + 1) << "          " << atomicNum 
            << "           0        " << std::fixed << std::setprecision(6)
            << std::setw(10) << atom.x << "    "
            << std::setw(10) << atom.y << "    "
            << std::setw(10) << atom.z << "\n";
    }
    
    oss << " ---------------------------------------------------------------------\n";
    oss << " \n";
    oss << " SCF Done:      -100.000000000\n";
    oss << " \n";
    oss << "GradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGrad\n";
    oss << " Step number   " << frameNumber << "\n";
    oss << "         Item               Value     Threshold  Converged?\n";
    oss << " Maximum Force            1.000000     1.000000     NO\n";
    oss << " RMS     Force            1.000000     1.000000     NO\n";
    oss << " Maximum Displacement     1.000000     1.000000     NO\n";
    oss << " RMS     Displacement     1.000000     1.000000     NO\n";
    
    return oss.str();
}

// 写入Gaussian LOG尾部
std::string writeGaussianLogFooter() {
    return "GradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGradGrad\n"
           " Normal termination of Gaussian\n";
}

// 转换为Gaussian LOG格式
std::string convertToGaussianLog(const std::vector<Frame>& frames) {
    if (frames.empty()) {
        LOG_ERROR("No frames to convert");
        return "";
    }
    
    try {
        std::ostringstream oss;
        
        oss << writeGaussianLogHeader();
        
        for (size_t i = 0; i < frames.size(); ++i) {
            oss << writeGaussianLogGeometry(frames[i], static_cast<int>(i + 1));
        }
        
        oss << writeGaussianLogFooter();
        
        LOG_DEBUG("Converted " + std::to_string(frames.size()) + " frames to Gaussian log format");
        return oss.str();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in convertToGaussianLog: " + std::string(e.what()));
        return "";
    }
}

// 创建临时文件
std::string createTempFile(const std::string& content) {
    try {
        if (!g_config.tempDir.empty()) {
            std::filesystem::create_directories(g_config.tempDir);
        }
        
        std::time_t t = std::time(nullptr);
        std::ostringstream filename;
        filename << "molecule_" << t << ".log";
        
        std::string filepath = g_config.tempDir.empty() ? filename.str() : g_config.tempDir + "/" + filename.str();
        
        std::ofstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to create temp file: " + filepath);
            return "";
        }
        
        file << content;
        file.close();
        
        LOG_INFO("Created temporary file: " + filepath);
        return filepath;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception creating temp file: " + std::string(e.what()));
        return "";
    }
}

// 使用GView打开文件
bool openWithGView(const std::string& filepath) {
    try {
        if (g_config.gviewPath.empty()) {
            LOG_ERROR("GView path not configured!");
            return false;
        }
        
        std::string command = "\"" + g_config.gviewPath + "\" \"" + filepath + "\"";
        LOG_DEBUG("Executing command: " + command);
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to launch GView (Error: " + std::to_string(error) + ")");
            return false;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        DeleteFileThreadParams* params = new DeleteFileThreadParams;
        params->filepath = filepath;
        params->waitSeconds = g_config.waitSeconds;
        
        HANDLE hThread = CreateThread(NULL, 0, DeleteFileThread, params, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        } else {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to create delete thread (Error: " + std::to_string(error) + ")");
            delete params;
        }
        
        LOG_INFO("Launched GView successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception launching GView: " + std::string(e.what()));
        return false;
    }
}

// 处理剪贴板内容（XYZ到GView）
void processClipboardXYZToGView() {
    LOG_INFO("Processing clipboard (XYZ to GView)...");
    
    try {
        std::string content = getClipboardText();
        if (content.empty()) {
            LOG_INFO("Clipboard is empty or not text format.");
            return;
        }
        
        if (content.length() > g_config.maxClipboardChars) {
            LOG_WARNING("Clipboard content is too large (" + std::to_string(content.length()) + 
                       " characters). Limit is " + std::to_string(g_config.maxClipboardChars) + 
                       " characters (" + std::to_string(g_config.maxMemoryMB) + "MB memory limit).");
            return;
        }
        
        if (!isXYZFormat(content)) {
            LOG_INFO("Invalid XYZ format in clipboard.");
            return;
        }
        
        double estimatedMemoryMB = (content.length() * 8.0) / (1024.0 * 1024.0);
        LOG_INFO("Processing " + std::to_string(content.length()) + " characters (estimated " + 
                std::to_string(static_cast<int>(estimatedMemoryMB)) + "MB memory usage)");
        
        std::vector<Frame> frames = readMultiXYZ(content);
        if (frames.empty()) {
            LOG_ERROR("Failed to parse XYZ data.");
            return;
        }
        
        LOG_INFO("Found " + std::to_string(frames.size()) + " frame(s) with " + std::to_string(frames[0].atoms.size()) + " atoms.");
        
        std::string gaussianContent = convertToGaussianLog(frames);
        if (gaussianContent.empty()) {
            LOG_ERROR("Failed to convert to Gaussian log format.");
            return;
        }
        
        std::string tempFile = createTempFile(gaussianContent);
        
        if (tempFile.empty()) {
            LOG_ERROR("Failed to create temporary file.");
            return;
        }
        
        if (openWithGView(tempFile)) {
            LOG_INFO("Opened with GView successfully.");
        } else {
            LOG_ERROR("Failed to open with GView.");
            if (!DeleteFileA(tempFile.c_str())) {
                LOG_ERROR("Failed to cleanup temp file: " + tempFile);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in processClipboardXYZToGView: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown exception in processClipboardXYZToGView");
    }
}

// 新增：处理GView clipboard到XYZ
void processGViewClipboardToXYZ() {
    LOG_INFO("Processing GView clipboard to XYZ...");
    
    try {
        if (g_config.gaussianClipboardPath.empty()) {
            LOG_ERROR("Gaussian clipboard path not configured!");
            return;
        }
        
        // 解析Gaussian clipboard文件
        std::vector<Atom> atoms = parseGaussianClipboard(g_config.gaussianClipboardPath);
        
        if (atoms.empty()) {
            LOG_ERROR("No atoms found in Gaussian clipboard file");
            LOG_INFO("Make sure you have copied a molecule in Gaussian and the path is correct.");
            return;
        }
        
        LOG_INFO("SUCCESS: Parsed " + std::to_string(atoms.size()) + " atoms");
        
        // 创建XYZ字符串
        std::string xyzString = createXYZString(atoms);
        
        if (xyzString.empty()) {
            LOG_ERROR("Failed to create XYZ string");
            return;
        }
        
        // 写入剪贴板
        if (writeToClipboard(xyzString)) {
            LOG_INFO("SUCCESS: XYZ data written to clipboard!");
            LOG_DEBUG("XYZ content preview (first 200 chars): " + xyzString.substr(0, 200) + "...");
        } else {
            LOG_ERROR("Failed to write to clipboard");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in processGViewClipboardToXYZ: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown exception in processGViewClipboardToXYZ");
    }
}

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    try {
        switch (uMsg) {
            case WM_HOTKEY:
                if (wParam == HOTKEY_XYZ_TO_GVIEW) {
                    processClipboardXYZToGView();
                } else if (wParam == HOTKEY_GVIEW_TO_XYZ) {
                    processGViewClipboardToXYZ();
                }
                return 0;
                
            case WM_TRAYICON:
                switch (lParam) {
                    case WM_LBUTTONDBLCLK:
                        showAboutDialog(hwnd);
                        break;
                        
                    case WM_RBUTTONUP:
                        POINT pt;
                        GetCursorPos(&pt);
                        showTrayMenu(hwnd, pt);
                        break;
                }
                return 0;
                
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case ID_TRAY_ABOUT:
                        showAboutDialog(hwnd);
                        break;
                        
                    case ID_TRAY_RELOAD:
                        if (reloadConfiguration()) {
                            MessageBoxA(hwnd, "Configuration reloaded successfully!", "XYZ Monitor", MB_OK | MB_ICONINFORMATION);
                        } else {
                            MessageBoxA(hwnd, "Failed to reload configuration. Check the log file for details.", "XYZ Monitor", MB_OK | MB_ICONERROR);
                        }
                        break;
                        
                    case ID_TRAY_EXIT:
                        g_running = false;
                        PostQuitMessage(0);
                        break;
                }
                return 0;
                
            case WM_DESTROY:
                cleanupTrayIcon();
                PostQuitMessage(0);
                return 0;
        }
        
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in WindowProc: " + std::string(e.what()));
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int main() {
    try {
        loadConfig("config.ini");
        
        LogLevel logLevel = stringToLogLevel(g_config.logLevel);
        if (!g_logger.initialize(g_config.logFile, logLevel)) {
            std::cerr << "Warning: Failed to initialize log file, logging to console only." << std::endl;
        }
        
        g_logger.setLogToConsole(g_config.logToConsole);
        g_logger.setLogToFile(g_config.logToFile);
        
        LOG_INFO("XYZ Monitor v1.1 starting...");
        
        LOG_INFO("Configuration:");
        LOG_INFO("  XYZ->GView Hotkey: " + g_config.hotkey);
        LOG_INFO("  GView->XYZ Hotkey: " + g_config.hotkeyReverse);
        LOG_INFO("  GView Path: " + g_config.gviewPath);
        LOG_INFO("  Gaussian Clipboard: " + g_config.gaussianClipboardPath);
        LOG_INFO("  Temp Dir: " + g_config.tempDir);
        LOG_INFO("  Log File: " + g_config.logFile);
        LOG_INFO("  Log Level: " + g_config.logLevel);
        LOG_INFO("  Wait Seconds: " + std::to_string(g_config.waitSeconds));
        LOG_INFO("  Max Memory: " + std::to_string(g_config.maxMemoryMB) + "MB");
        LOG_INFO("  Max Characters: " + std::to_string(g_config.maxClipboardChars));
        
        // 创建隐藏窗口
        WNDCLASSA wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "XYZMonitorClass";
        wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
        if (!wc.hIcon) {
            wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        }
        
        if (!RegisterClassA(&wc)) {
            LOG_ERROR("Failed to register window class.");
            return 1;
        }
        
        g_hwnd = CreateWindowExA(
            0, "XYZMonitorClass", "XYZ Monitor", 0,
            0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL
        );
        
        if (!g_hwnd) {
            LOG_ERROR("Failed to create window.");
            return 1;
        }
        
        if (!createTrayIcon(g_hwnd)) {
            LOG_WARNING("Failed to create tray icon, continuing without it");
        }
        
        // 注册全局热键
        if (!reregisterHotkeys()) {
            LOG_ERROR("Failed to register hotkeys");
            return 1;
        }
        
        LOG_INFO("XYZ Monitor is running. Check system tray for options.");
        LOG_INFO("Press " + g_config.hotkey + " to convert clipboard XYZ to GView.");
        LOG_INFO("Press " + g_config.hotkeyReverse + " to convert GView clipboard to XYZ.");
        
        // 消息循环
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) && g_running) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // 清理
        UnregisterHotKey(g_hwnd, HOTKEY_XYZ_TO_GVIEW);
        UnregisterHotKey(g_hwnd, HOTKEY_GVIEW_TO_XYZ);
        cleanupTrayIcon();
        DestroyWindow(g_hwnd);
        
        LOG_INFO("XYZ Monitor stopped.");
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal exception in main: " + std::string(e.what()));
        return 1;
    } catch (...) {
        LOG_ERROR("Fatal unknown exception in main");
        return 1;
    }
}