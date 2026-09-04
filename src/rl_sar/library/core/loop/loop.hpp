/*
 * Copyright (c) 2024-2025 Ziqi Fan
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOOP_H
#define LOOP_H

#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <sstream>
#include <iomanip>
#include <exception>
#include "logger.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

class LoopFunc
{
public:
    LoopFunc(const std::string &name, float period, std::function<void()> func, int bindCPU = -1)
        : _name(name), _period(period), _func(func), _bindCPU(bindCPU), _running(false) {}

    ~LoopFunc()
    {
        shutdown();
    }

    void start()
    {
        if (_running.exchange(true)) return;
        _failed = false;
        std::cout << LOGGER::INFO << "[Loop] Loop start - name: " << _name << ", period: " << formatPeriod() << "ms"
                  << (_bindCPU != -1 ? ", cpu: " + std::to_string(_bindCPU) : ", cpu: unspecified") << std::endl;
        if (_bindCPU != -1)
        {
            _thread = std::thread(&LoopFunc::loop, this);
            setThreadAffinity(_thread.native_handle(), _bindCPU);
        }
        else
        {
            _thread = std::thread(&LoopFunc::loop, this);
        }
    }

    void shutdown()
    {
        const bool was_running = _running.exchange(false);
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.notify_one();
        }
        if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
        {
            _thread.join();
        }
        if (was_running)
        {
            std::cout << LOGGER::INFO << "[Loop] Loop end - name: " << _name << std::endl;
        }
    }

    bool failed() const { return _failed.load(); }

    std::string errorMessage() const
    {
        std::lock_guard<std::mutex> lock(_errorMutex);
        return _errorMessage;
    }

private:
    std::string _name;
    float _period;
    std::function<void()> _func;
    int _bindCPU;
    std::atomic<bool> _running;
    std::atomic<bool> _failed{false};
    std::mutex _mutex;
    std::condition_variable _cv;
    std::thread _thread;
    mutable std::mutex _errorMutex;
    std::string _errorMessage;

    void loop()
    {
        while (_running)
        {
            auto start = std::chrono::steady_clock::now();

            try
            {
                _func();
            }
            catch (const std::exception &error)
            {
                {
                    std::lock_guard<std::mutex> lock(_errorMutex);
                    _errorMessage = error.what();
                }
                _failed = true;
                _running = false;
                std::cerr << LOGGER::ERROR << "[Loop] Unhandled exception - name: " << _name
                          << ", error: " << error.what() << std::endl;
                break;
            }
            catch (...)
            {
                {
                    std::lock_guard<std::mutex> lock(_errorMutex);
                    _errorMessage = "unknown exception";
                }
                _failed = true;
                _running = false;
                std::cerr << LOGGER::ERROR << "[Loop] Unhandled exception - name: " << _name
                          << ", error: unknown exception" << std::endl;
                break;
            }

            auto end = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            auto sleepTime = std::chrono::milliseconds(static_cast<int>((_period * 1000) - elapsed.count()));
            if (sleepTime.count() > 0)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                if (_cv.wait_for(lock, sleepTime, [this]
                                 { return !_running; }))
                {
                    break;
                }
            }
        }
    }

    std::string formatPeriod() const
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(0) << _period * 1000;
        return stream.str();
    }

    void setThreadAffinity(std::thread::native_handle_type threadHandle, int cpuId)
    {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuId, &cpuset);
        if (pthread_setaffinity_np(threadHandle, sizeof(cpu_set_t), &cpuset) != 0)
        {
            std::ostringstream oss;
            oss << "Error setting thread affinity: CPU " << cpuId << " may not be valid or accessible.";
            throw std::runtime_error(oss.str());
        }
#else
        // Thread affinity not supported on this platform
        std::cout << LOGGER::WARNING << "Thread affinity not supported on this platform" << std::endl;
#endif
    }
};

#endif // LOOP_H
