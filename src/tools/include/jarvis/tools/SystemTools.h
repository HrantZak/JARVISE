#pragma once

#include <memory>
#include <vector>

#include "jarvis/system/ISystemMetricsProvider.h"
#include "jarvis/tools/ITool.h"

namespace jarvis::tools {

/// Read-only tools built on the telemetry JARVIS already collects.
///
/// They read `ISystemMetricsProvider` - the same source the HUD shows - rather
/// than starting a second monitoring mechanism. When a reading is unavailable
/// the tool fails with `Unavailable`; it never substitutes a plausible number,
/// because a fabricated measurement is worse than an honest gap.
///
/// The provider is shared and read from a worker thread, so it must outlive
/// every tool built on it. The composition root owns both.
class SystemToolFactory {
public:
    /// Creates every read-only system tool. \p provider must outlive them.
    [[nodiscard]] static std::vector<std::unique_ptr<ITool>> createAll(
        system::ISystemMetricsProvider& provider);
};

/// Applications JARVIS is allowed to open.
///
/// An enumeration, not a path. The schema has no field that could carry
/// `C:\Windows\System32\cmd.exe`, so refusing it is not a check that could be
/// forgotten - there is nowhere to put it. Each entry maps to a Windows shell
/// verb or a known executable name resolved by the system, never to a string
/// supplied by a model.
enum class AllowedApplication {
    Calculator,
    Notepad,
    Explorer,
    Settings,
};

/// Opens one of the allowed applications. CONFIRM_REQUIRED: it has a visible
/// effect on the machine, so it never runs without a human saying yes to that
/// specific request.
class OpenApplicationTool final : public ITool {
public:
    OpenApplicationTool();

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

    [[nodiscard]] ToolResult execute(const ValidatedCall& call,
                                     const std::atomic<bool>& cancelled) override;

    /// Maps an accepted enumeration value to an application. Returns nullopt
    /// for anything not in the allowlist.
    [[nodiscard]] static std::optional<AllowedApplication> resolve(
        std::string_view value);

private:
    ToolDefinition m_definition;
};

/// Applications JARVIS is allowed to close.
///
/// A separate enumeration from AllowedApplication, and deliberately so: what is
/// safe to *start* and what is safe to *stop* are different questions. Closing
/// Explorer takes the taskbar and the desktop with it, so it is openable and
/// not closable.
///
/// Same shape as the open list for the same reason - an enumeration, not a
/// process name. ITool requires the argument schema to be constant for the
/// lifetime of the process, and ArgumentType has no free-form string, so a name
/// chosen by the model has nowhere to travel. The model picks an index into a
/// table compiled into this binary.
enum class ClosableApplication {
    Telegram,
    Discord,
    Chrome,
    Edge,
    Firefox,
    Notepad,
    Calculator,
    Spotify,
    Steam,
    VlcPlayer,
};

/// Closes one of the allowed applications by asking its windows to close.
///
/// CONFIRM_REQUIRED: it has a visible effect on the machine and can lose unsaved
/// work, so it never runs without a human saying yes to that specific request.
///
/// **How it closes.** WM_CLOSE to every visible top-level window the process
/// owns - the same message the X button sends. The application runs its own
/// shutdown, asks about unsaved work, and can refuse. Nothing here terminates a
/// process: there is no TerminateProcess call in this file, and adding one
/// would turn a polite request into data loss.
///
/// **What it will not touch.** The allowlist above is the first gate. The
/// second is in the executor and does not depend on it: a process running as a
/// service, in session 0, without a visible window, or belonging to JARVIS
/// itself is refused whatever the enumeration says. Two independent checks,
/// because an allowlist entry could one day be added carelessly.
class CloseApplicationTool final : public ITool {
public:
    CloseApplicationTool();

    [[nodiscard]] const ToolDefinition& definition() const override {
        return m_definition;
    }

    [[nodiscard]] ToolResult execute(const ValidatedCall& call,
                                     const std::atomic<bool>& cancelled) override;

    /// Maps an accepted enumeration value to an application. Returns nullopt
    /// for anything not in the allowlist.
    [[nodiscard]] static std::optional<ClosableApplication> resolve(
        std::string_view value);

    /// True when this executable must never be asked to close - JARVIS itself,
    /// the Windows shell, anything under the system directories. Exposed so the
    /// rule can be tested directly rather than only through a live process.
    [[nodiscard]] static bool isProtectedExecutable(std::wstring_view path);

private:
    ToolDefinition m_definition;
};

} // namespace jarvis::tools
