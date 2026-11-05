#include "signals.hpp"

#include <csignal>

namespace yuka {

    namespace {

        volatile std::sig_atomic_t g_stop_requested = 0;

        void handle_sig_int(int) {
            g_stop_requested = 1;
        }

    } // namespace

    void install_signal_handlers() {
        struct sigaction action{};
        action.sa_handler = handle_sig_int;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGINT, &action, nullptr);
    }

    bool stop_requested() {
        return g_stop_requested != 0;
    }

} // namespace yuka
