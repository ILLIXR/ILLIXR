#pragma once

#include "phonebook.hpp"
#include <mutex>
#include <semaphore.h>

using namespace ILLIXR;

class common_lock : public phonebook::service {
public:
    std::mutex lock;
    //std::binary_semaphore s;
    sem_t sem_monado, sem_illixr;
    int pshared = 0;
    unsigned int value = 0;
    virtual void     get_lock()                            = 0;
    virtual void     release_lock()                        = 0;
    virtual void     wait_illixr()                         = 0;
    virtual void     wait_monado()                         = 0;
    ~common_lock() override = default;
};
