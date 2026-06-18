#pragma once

#include <windows.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <memory>
#include <chrono>
#include <iomanip>

enum LogLevel
{
    LevelDebug = 0,
    LevelInfo,
    LevelWarning,
    LevelError
};

class Logger
{
public:
    static Logger& Instance()
    {
        static Logger instance;
        return instance;
    }

    void SetLevel(LogLevel level) { minLevel_ = level; }
    LogLevel GetLevel() const { return minLevel_; }

    void SetOutputToConsole(bool enable) { outputToConsole_ = enable; }
    void SetOutputToFile(bool enable, const wchar_t* filename = L"app.log")
    {
        outputToFile_ = enable;
        logFilename_ = filename;
    }

    void Log(LogLevel level, const char* msg)
    {
        if (level < minLevel_) return;

        std::wstring prefix = GetLevelPrefix(level);
        std::wstring timestamp = GetTimestamp();
        std::wstring fullMsg = timestamp + L" [" + prefix + L"] " + StringToWString(msg) + L"\n";

        if (outputToConsole_)
        {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, GetLevelColor(level));
            WriteConsoleW(hConsole, fullMsg.c_str(), static_cast<DWORD>(fullMsg.length()), nullptr, nullptr);
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }

        if (outputToFile_)
        {
            WriteToFile(fullMsg);
        }
    }

    template<typename T>
    Logger& operator<<(T value)
    {
        stream_ << value;
        return *this;
    }

    // Specialization for std::endl and other manipulators
    typedef Logger& (*LoggerManipulator)(Logger&);
    Logger& operator<<(LoggerManipulator manip)
    {
        return manip(*this);
    }

    void Flush()
    {
        std::wstring msg = stream_.str();
        stream_.str(L"");
        stream_.clear();
        if (!msg.empty())
        {
            Log(currentLevel_, WStringToString(msg).c_str());
        }
    }

    LogLevel GetCurrentLevel() const { return currentLevel_; }
    void SetCurrentLevel(LogLevel level) { currentLevel_ = level; }
    void Flush_Impl()
    {
        std::wstring msg = stream_.str();
        stream_.str(L"");
        stream_.clear();
        if (!msg.empty())
        {
            Log(currentLevel_, WStringToString(msg).c_str());
        }
    }

private:
    Logger()
        : minLevel_(LevelInfo)
        , outputToConsole_(true)
        , outputToFile_(false)
        , currentLevel_(LevelInfo)
    {
    }

    ~Logger()
    {
        Flush();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static Logger& endl(Logger& logger)
    {
        logger.Flush();
        return logger;
    }

    std::wstring GetLevelPrefix(LogLevel level)
    {
        switch (level)
        {
        case LevelDebug:   return L"DEBUG";
        case LevelInfo:    return L"INFO";
        case LevelWarning: return L"WARN";
        case LevelError:   return L"ERROR";
        default:          return L"UNKNOWN";
        }
    }

    WORD GetLevelColor(LogLevel level)
    {
        switch (level)
        {
        case LevelDebug:   return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case LevelInfo:    return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LevelWarning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LevelError:   return FOREGROUND_RED | FOREGROUND_INTENSITY;
        default:          return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
    }

    std::wstring GetTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tmBuf;
        localtime_s(&tmBuf, &time);
        std::wstringstream ss;
        ss << std::put_time(&tmBuf, L"%Y-%m-%d %H:%M:%S");
        ss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string WStringToString(const std::wstring& wstr)
    {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
        return strTo;
    }

    std::wstring StringToWString(const std::string& str)
    {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    void WriteToFile(const std::wstring& msg)
    {
        std::wofstream file;
        file.open(logFilename_, std::ios::app);
        if (file.is_open())
        {
            file << msg;
            file.close();
        }
    }

    LogLevel minLevel_;
    bool outputToConsole_;
    bool outputToFile_;
    std::wstring logFilename_;
    std::wstringstream stream_;
    LogLevel currentLevel_;

    friend Logger& endl(Logger&);
};

// Global macro for easy logging
#define LOG(level) \
    (Logger::Instance().SetCurrentLevel(level), Logger::Instance())

// Convenience macros for specific levels
#define LOG_DEBUG   (Logger::Instance().SetCurrentLevel(LevelDebug), Logger::Instance())
#define LOG_INFO    (Logger::Instance().SetCurrentLevel(LevelInfo), Logger::Instance())
#define LOG_WARNING (Logger::Instance().SetCurrentLevel(LevelWarning), Logger::Instance())
#define LOG_ERROR   (Logger::Instance().SetCurrentLevel(LevelError), Logger::Instance())

// Global endl manipulator
inline Logger& endl(Logger& logger)
{
    return Logger::Instance().Flush_Impl(), logger;
}
