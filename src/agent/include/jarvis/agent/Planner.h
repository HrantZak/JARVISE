#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/agent/Task.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

namespace jarvis::agent {

/// Why a plan was refused.
///
/// Distinct codes because the audit trail, the recovery policy and the tests
/// all need to tell "the model wrote nonsense" apart from "the model asked for
/// something it is not allowed to have".
enum class PlanError {
    None,
    TooLarge,          ///< The payload exceeded the size limit before parsing.
    MalformedJson,
    NotAnObject,
    UnknownField,      ///< A key outside the fixed schema, at any level.
    WrongType,
    MissingSteps,
    EmptyPlan,
    TooManySteps,
    InvalidStep,       ///< A step failed ToolValidator. Rejects the whole plan.
    UnknownTool,
    DeniedTool,        ///< The tool exists but may never run.
};

[[nodiscard]] std::string_view planErrorName(PlanError error) noexcept;

/// One step a plan asks for, after validation.
///
/// Holds a `tools::ValidatedCall`, so it cannot exist for a tool that is not
/// registered or for arguments that do not fit its schema. The permission is
/// read from the registry, never from the plan: a plan has no field that could
/// carry one, and if it did the value would be ignored metadata.
struct PlannedStep {
    tools::ValidatedCall call;
    tools::PermissionLevel permission{tools::PermissionLevel::Denied};
};

/// A validated plan. Every step in it has already passed the same boundary a
/// single tool call passes.
struct Plan {
    std::vector<PlannedStep> steps;

    [[nodiscard]] bool empty() const noexcept { return steps.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return steps.size(); }

    /// True when any step will need a human before it can run.
    [[nodiscard]] bool needsConfirmation() const;
};

/// Turns a model's proposed plan into validated steps, or refuses it.
///
/// **The planner has no privileges.** It cannot register a tool, raise a
/// permission, skip a confirmation or reach the executor. Everything it
/// produces goes through `ToolValidator` - the same boundary a single tool call
/// faces - and the permission attached to each step is looked up from the
/// registry rather than taken from the model's output.
///
/// Plan output is untrusted input, exactly like a tool call. The difference is
/// only that there are several of them at once.
class Planner {
public:
    /// Longest accepted plan payload, checked before parsing so a model looping
    /// on output cannot make the parser allocate without bound.
    static constexpr std::size_t kMaxPlanLength = 16384;

    /// Most steps a plan may propose. Bounded by Task::kMaxSteps, which is the
    /// hard ceiling: configuration may lower this, nothing can raise it.
    static constexpr std::size_t kMaxPlanSteps = Task::kMaxSteps;

    struct Rejection {
        PlanError code{PlanError::None};
        std::string message;

        /// Which step was at fault, when the failure was in one. Otherwise the
        /// size of the plan.
        std::size_t stepIndex{0};
    };

    Planner(const tools::ToolRegistry& registry, const tools::ToolValidator& validator);

    /// The step limit actually applied. Clamped to kMaxPlanSteps on the way in,
    /// so a configuration file cannot widen it.
    void setMaxSteps(std::size_t maxSteps);
    [[nodiscard]] std::size_t maxSteps() const noexcept { return m_maxSteps; }

    /// Extracts a plan object from model output, if there is one. A locator,
    /// not a decider: whatever it finds still has to survive parse().
    [[nodiscard]] static std::string extractPlanJson(std::string_view modelOutput);

    /// True when \p modelOutput looks like a plan rather than a single call.
    /// Lets the agent take the single-step path without a planner round trip.
    [[nodiscard]] static bool looksLikePlan(std::string_view modelOutput);

    /// Parses and validates \p json.
    ///
    /// **All or nothing.** One invalid step rejects the whole plan; steps are
    /// never silently dropped and a partial plan is never returned, because a
    /// plan missing its middle is not the plan anyone approved.
    [[nodiscard]] std::expected<Plan, Rejection> parse(std::string_view json) const;

    /// Fills \p task with \p plan. The task must be freshly created; the steps
    /// are added in order.
    [[nodiscard]] std::expected<void, Rejection> populate(Task& task,
                                                          const Plan& plan) const;

    /// How the plan format is described to the model. Documentation, not a
    /// grant: ignoring every word of it changes nothing about what may run.
    [[nodiscard]] std::string promptSection() const;

private:
    const tools::ToolRegistry& m_registry;
    const tools::ToolValidator& m_validator;
    std::size_t m_maxSteps{kMaxPlanSteps};
};

} // namespace jarvis::agent
