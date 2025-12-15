#include "service.hpp"

common_lock_impl::common_lock_impl(const phonebook* const pb)
        : switchboard_{pb->lookup_impl<switchboard>()} {
    int ret1 = sem_init(&sem_monado, pshared, value);
    int ret2 = sem_init(&sem_illixr, pshared, value);
    spdlog::get("illixr")->debug("Semaphore initialized ret1 = %d ret2 = %d", ret1, ret2);
}

void common_lock_impl::get_lock() {
    lock.lock();
}

void common_lock_impl::release_lock() {
    lock.unlock();
}

void common_lock_impl::wait_illixr() {
    illixr_monado_ = true;
    while (illixr_monado_);
    return;
}

void common_lock_impl::wait_monado() {
    illixr_monado_ = false;
    while (!illixr_monado_);
    return;
}

PLUGIN_MAIN(common_lock_plugin)