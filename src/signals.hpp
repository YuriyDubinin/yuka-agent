#pragma once

namespace yuka {

    // Installs the SIGINT handler (without SA_RESTART, so blocking reads are interrupted).
    void install_signal_handlers();

    // True once the user pressed Ctrl+C.
    bool stop_requested();

} // namespace yuka
