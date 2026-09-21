#pragma once
#include "SceneController.h"
#include "jarvis/tools/ITool.h"
#include <QMetaObject>

namespace jarvis::app {
class DrawTool final : public tools::ITool {
    scene3d::SceneController& m_scene;
    tools::ToolDefinition m_definition;
public:
    explicit DrawTool(scene3d::SceneController& scene):m_scene(scene) {
        m_definition.name="draw_live";
        m_definition.permission=tools::PermissionLevel::SafeAction;
        m_definition.description="Draw ANY requested object, diagram or curve as animated luminous line art INSIDE JARVIS, not a file. Invent meaningful detailed outlines from the user's description. Also use for requests to modify the previous drawing; supply the complete revised drawing. Argument drawing is a JSON string: {\"title\":\"short title\",\"paths\":[{\"color\":\"#65cfff\",\"points\":[[x,y],[x,y],...]},...]}. Coordinates Cartesian, y up; optional third z coordinate for 3D wire drawings. Compose silhouettes, internal details and curves sampled into points. Typical range -10..10, any bounded scale auto-fits. Order paths in drawing order. Max 200 paths, 5000 total points, each path at least 2 points. No code, SVG, images, file paths or URLs. Produce an actual recognizable drawing, never just a description. Keep coordinates concise; 100-500 points usually sufficient.";
        tools::ArgumentSpec arg;arg.name="drawing";arg.type=tools::ArgumentType::Text;arg.required=true;arg.maxTextChars=100000;arg.description="Complete drawing JSON string with title and paths.";
        m_definition.arguments.push_back(arg);
        m_definition.description += " Visual design: vary shapes meaningfully instead of repeating rectangles. Use circles/ellipses for hubs and states, rounded rectangles for processes, squares for components, cylinders for storage; sample curved outlines into points. Use restrained cyan, mint and lavender accents on black. Keep arrows outside labels. Explain mechanisms and causal relationships accurately, not vague filler; distinguish inputs, processing and outputs. Each step gets a precise short heading and one useful sentence. Text remains a fixed screen size when zooming: reserve generous spacing around labels, avoid tightly packed diagrams.";
        m_definition.description += " For explanations (explain how something works), create a clear 3-6 step diagram with boxes, connecting arrow polylines with arrowheads, and concise Russian labels. Also supports text-only drawings. Add root labels array: [{\"text\":\"1. Short heading\\nBrief explanation\",\"x\":0,\"y\":2,\"at\":0.2}]. Labels are centered, multiline, up to 160 chars each; at is reveal fraction 0..1. Place labels inside generously sized boxes with no overlaps. Order paths step by step and synchronize labels at with cumulative drawn line length. Use a balanced 2-column layout for 4-6 steps, leaving room for readable text. No long paragraphs. For drawing text use labels, never approximate letters with strokes. After success speak at most one brief sentence; put the explanation on the diagram, no duplicate lecture.";
    }
    const tools::ToolDefinition& definition() const override {return m_definition;}
    tools::ToolResult execute(const tools::ValidatedCall& call,const std::atomic<bool>& cancelled) override {
        if(cancelled) return tools::ToolResult::failure("draw_live",tools::ToolErrorCode::Cancelled,"Cancelled");
        const auto json=QString::fromStdString(call.textArgument("drawing"));
        const auto error=scene3d::SceneController::validateDrawing(json);
        if(!error.isEmpty()) return tools::ToolResult::failure("draw_live",tools::ToolErrorCode::ExecutionFailed,error.toStdString());
        bool applied=false;
        QMetaObject::invokeMethod(&m_scene,[&]{if(!cancelled)applied=m_scene.applyDrawing(json);},Qt::BlockingQueuedConnection);
        return applied?tools::ToolResult::success("draw_live",{{"status","Drawing displayed in JARVIS; strokes are being revealed. No file created."}}):tools::ToolResult::failure("draw_live",tools::ToolErrorCode::Cancelled,"Cancelled");
    }
};
}
