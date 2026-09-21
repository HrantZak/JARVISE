#include "jarvis/agent/Planner.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <format>

namespace jarvis::agent {
namespace {

using Rejection = Planner::Rejection;

Rejection reject(PlanError code, std::string message, std::size_t stepIndex = 0) {
    return Rejection{code, std::move(message), stepIndex};
}

/// Re-serialises one step object so ToolValidator sees exactly the same text a
/// standalone tool call would.
///
/// The point is that there is *one* validator, and the plan path does not get a
/// gentler one. Rebuilding the JSON rather than reaching into the object keeps
/// that literally true: the same bytes, through the same function.
QByteArray stepToCallJson(const QJsonObject& step) {
    return QJsonDocument{step}.toJson(QJsonDocument::Compact);
}

} // namespace

std::string_view planErrorName(PlanError error) noexcept {
    switch (error) {
    case PlanError::None:          return "NONE";
    case PlanError::TooLarge:      return "TOO_LARGE";
    case PlanError::MalformedJson: return "MALFORMED_JSON";
    case PlanError::NotAnObject:   return "NOT_AN_OBJECT";
    case PlanError::UnknownField:  return "UNKNOWN_FIELD";
    case PlanError::WrongType:     return "WRONG_TYPE";
    case PlanError::MissingSteps:  return "MISSING_STEPS";
    case PlanError::EmptyPlan:     return "EMPTY_PLAN";
    case PlanError::TooManySteps:  return "TOO_MANY_STEPS";
    case PlanError::InvalidStep:   return "INVALID_STEP";
    case PlanError::UnknownTool:   return "UNKNOWN_TOOL";
    case PlanError::DeniedTool:    return "DENIED_TOOL";
    }
    return "MALFORMED_JSON";
}

bool Plan::needsConfirmation() const {
    return std::ranges::any_of(steps, [](const PlannedStep& step) {
        return step.permission == tools::PermissionLevel::ConfirmRequired
               || step.permission == tools::PermissionLevel::Destructive;
    });
}

Planner::Planner(const tools::ToolRegistry& registry,
                 const tools::ToolValidator& validator)
    : m_registry{registry}
    , m_validator{validator} {}

void Planner::setMaxSteps(std::size_t maxSteps) {
    // Clamped, not honoured. A configuration file may narrow the limit; it can
    // never widen it past the structural ceiling, so an oversized plan cannot
    // be configured into existence.
    m_maxSteps = std::clamp<std::size_t>(maxSteps, 1, kMaxPlanSteps);
}

std::string Planner::extractPlanJson(std::string_view modelOutput) {
    // The same balanced-brace scan the tool path uses. Deliberately dumb: it
    // locates a candidate, and the candidate still has to survive parse().
    return tools::ToolValidator::extractCallJson(modelOutput);
}

bool Planner::looksLikePlan(std::string_view modelOutput) {
    const std::string json = extractPlanJson(modelOutput);
    if (json.empty()) {
        return false;
    }
    // A structural hint only, used to decide whether to take the single-step
    // path. Being wrong here costs a refusal, never an execution.
    return json.find("\"steps\"") != std::string::npos
           || json.find("\"type\"") != std::string::npos;
}

std::expected<Plan, Rejection> Planner::parse(std::string_view json) const {
    if (json.empty()) {
        return std::unexpected(reject(PlanError::MalformedJson, "the plan is empty"));
    }
    if (json.size() > kMaxPlanLength) {
        return std::unexpected(reject(
            PlanError::TooLarge,
            std::format("the plan is {} bytes, limit is {}", json.size(),
                        kMaxPlanLength)));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray{json.data(), static_cast<qsizetype>(json.size())}, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(reject(
            PlanError::MalformedJson,
            std::format("invalid JSON at offset {}", parseError.offset)));
    }
    if (!document.isObject()) {
        return std::unexpected(
            reject(PlanError::NotAnObject, "a plan must be a JSON object"));
    }

    const QJsonObject root = document.object();

    // Only "type" and "steps" exist. Any other key rejects the plan - which is
    // what refuses a plan carrying "confirmed", "permission" or "admin", and
    // what will refuse the next invented field without new code.
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString key = it.key();
        if (key != QStringLiteral("type") && key != QStringLiteral("steps")) {
            return std::unexpected(reject(
                PlanError::UnknownField,
                std::format("unexpected field '{}' in the plan", key.toStdString())));
        }
    }

    if (root.contains(QStringLiteral("type"))) {
        const QJsonValue type = root.value(QStringLiteral("type"));
        if (!type.isString() || type.toString() != QStringLiteral("plan")) {
            return std::unexpected(
                reject(PlanError::WrongType, "'type' must be the string \"plan\""));
        }
    }

    const QJsonValue stepsValue = root.value(QStringLiteral("steps"));
    if (stepsValue.isUndefined() || stepsValue.isNull()) {
        return std::unexpected(reject(PlanError::MissingSteps, "'steps' is missing"));
    }
    if (!stepsValue.isArray()) {
        return std::unexpected(
            reject(PlanError::WrongType, "'steps' must be an array"));
    }

    const QJsonArray stepsArray = stepsValue.toArray();
    if (stepsArray.isEmpty()) {
        return std::unexpected(reject(PlanError::EmptyPlan, "the plan has no steps"));
    }
    if (static_cast<std::size_t>(stepsArray.size()) > m_maxSteps) {
        return std::unexpected(reject(
            PlanError::TooManySteps,
            std::format("the plan has {} steps, the limit is {}", stepsArray.size(),
                        m_maxSteps),
            static_cast<std::size_t>(stepsArray.size())));
    }

    Plan plan;
    plan.steps.reserve(static_cast<std::size_t>(stepsArray.size()));

    for (qsizetype i = 0; i < stepsArray.size(); ++i) {
        const auto index = static_cast<std::size_t>(i);
        const QJsonValue entry = stepsArray.at(i);

        if (!entry.isObject()) {
            return std::unexpected(reject(PlanError::WrongType,
                                          "every step must be an object", index));
        }

        // Through the ordinary validator, on the ordinary text. A step gets no
        // gentler treatment than a standalone call: unknown tool, unknown
        // argument, wrong type, out-of-range number and a value outside an
        // enumeration are all refused here exactly as they would be there.
        const QByteArray callJson = stepToCallJson(entry.toObject());
        auto validated = m_validator.validate(
            std::string_view{callJson.constData(),
                             static_cast<std::size_t>(callJson.size())});

        if (!validated) {
            // One bad step rejects the whole plan. Skipping it would run a plan
            // nobody proposed; running the prefix would leave the machine half
            // way through a task that was never coherent.
            return std::unexpected(reject(
                PlanError::InvalidStep,
                std::format("step {} is invalid: {} ({})", index + 1,
                            validated.error().message,
                            tools::toolErrorName(validated.error().code)),
                index));
        }

        const tools::ITool* tool = m_registry.lookup(validated->toolName());
        if (tool == nullptr) {
            // Unreachable: a ValidatedCall can only name a registered tool.
            // Failing closed rather than asserting means a future change that
            // made it reachable would still refuse.
            return std::unexpected(reject(PlanError::UnknownTool,
                                          "the step names no registered tool", index));
        }

        const tools::PermissionLevel permission = tool->definition().permission;
        if (permission == tools::PermissionLevel::Denied) {
            // Refused while planning as well as at execution. Letting a denied
            // tool into a plan would show the user a step that can never run.
            return std::unexpected(reject(
                PlanError::DeniedTool,
                std::format("step {} uses '{}', which may never run", index + 1,
                            validated->toolName()),
                index));
        }

        plan.steps.push_back(PlannedStep{std::move(*validated), permission});
    }

    return plan;
}

std::expected<void, Rejection> Planner::populate(Task& task, const Plan& plan) const {
    if (plan.empty()) {
        return std::unexpected(reject(PlanError::EmptyPlan, "the plan has no steps"));
    }

    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const PlannedStep& step = plan.steps[i];
        const StepId id = task.addStep(step.call, step.permission);
        if (!id.valid()) {
            // The task refused the step: it has already started, or it is full.
            // Either way the plan cannot be honoured as proposed.
            return std::unexpected(reject(
                PlanError::TooManySteps,
                std::format("the task would not accept step {}", i + 1), i));
        }
    }

    return {};
}

std::string Planner::promptSection() const {
    // Documentation for the model. Every sentence makes a usable plan more
    // likely; not one is load-bearing. A model that ignores all of it gets the
    // same refusals as one that read it carefully.
    std::string text =
        "PLANNING\n"
        "When a request needs more than one action, reply with a single JSON "
        "object and no other text:\n"
        "{\"type\": \"plan\", \"steps\": [{\"tool\": \"<name>\", \"arguments\": "
        "{}}]}\n"
        "Only the fields \"type\" and \"steps\" are allowed, and each step may "
        "contain only \"tool\" and \"arguments\". Steps run in order. Use the "
        "smallest number of steps that answers the request; for a single action, "
        "emit the ordinary tool call instead of a plan.\n";

    text += std::format("A plan may contain at most {} steps.\n", m_maxSteps);
    return text;
}

} // namespace jarvis::agent
