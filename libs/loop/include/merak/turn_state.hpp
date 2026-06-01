#pragma once
#include <string>

namespace merak {

enum class TurnState {
    Idle,
    ContextReady,
    Thinking,
    Acting,
    Observing,
    Responding,
    Complete,
    Error
};

inline bool is_terminal(TurnState s) {
    return s == TurnState::Complete || s == TurnState::Error;
}

inline bool can_continue(TurnState s) {
    return s == TurnState::ContextReady
        || s == TurnState::Observing
        || s == TurnState::Acting;
}

inline std::string state_name(TurnState s) {
    switch (s) {
        case TurnState::Idle:         return "IDLE";
        case TurnState::ContextReady: return "CONTEXT_READY";
        case TurnState::Thinking:     return "THINKING";
        case TurnState::Acting:       return "ACTING";
        case TurnState::Observing:    return "OBSERVING";
        case TurnState::Responding:   return "RESPONDING";
        case TurnState::Complete:     return "COMPLETE";
        case TurnState::Error:        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace merak
