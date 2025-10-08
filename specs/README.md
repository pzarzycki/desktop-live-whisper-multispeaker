# Project Specifications & Internal Documentation

This folder contains **internal project documentation** including architecture decisions, progress tracking, and technical specifications.

## 📋 Key Documents

### Current Status & Planning
- **[plan.md](plan.md)** - ⭐ Current project plan, phases, and progress tracking
- **[NEXT_AGENT_START_HERE.md](NEXT_AGENT_START_HERE.md)** - ⭐ Starting point for development work
- **[NEXT_STEPS.md](NEXT_STEPS.md)** - Immediate next actions

### Architecture & Design
- **[architecture.md](architecture.md)** - ⭐ System architecture and design decisions
- **[continuous_architecture_findings.md](continuous_architecture_findings.md)** - Ongoing architectural discoveries
- **[application_api_design.md](application_api_design.md)** - TranscriptionController API design
- **[platform_separation.md](platform_separation.md)** - Multiplatform code organization (Windows/macOS)

### Technical Specifications
- **[transcription.md](transcription.md)** - Whisper ASR integration
- **[diarization.md](diarization.md)** - Speaker identification system
- **[speaker_identification_analysis.md](speaker_identification_analysis.md)** - Speaker ID research
- **[speaker_models_onnx.md](speaker_models_onnx.md)** - ONNX speaker embedding models
- **[MODELS.md](MODELS.md)** - Model files and download info

### Implementation Progress
- **[phase3_report.md](phase3_report.md)** - Phase 3: ASR integration completion
- **[phase4_application_api_summary.md](phase4_application_api_summary.md)** - Phase 4: Controller API
- **[phase5_gui_implementation_log.md](phase5_gui_implementation_log.md)** - Phase 5: GUI implementation

### UI & Design
- **[gui_design.md](gui_design.md)** - Original GUI design (Qt-based, now replaced)
- **[imgui_migration.md](imgui_migration.md)** - Qt → ImGui migration rationale
- **[QT_INSTALLATION_EXPLAINED.md](QT_INSTALLATION_EXPLAINED.md)** - Qt setup (deprecated)
- **[qt_setup.md](qt_setup.md)** - Qt configuration (deprecated)
- **[QUICK_START_GUI.md](QUICK_START_GUI.md)** - Qt quick start (deprecated)

## 📂 Documentation Organization

```
/docs/          ← User-facing setup and usage guides
/specs/         ← You are here - Internal project documentation
README.md       ← Main project overview
```

## 🎯 For New Contributors

**Start here:**
1. Read [NEXT_AGENT_START_HERE.md](NEXT_AGENT_START_HERE.md) for current context
2. Check [plan.md](plan.md) to see project phases and current progress
3. Review [architecture.md](architecture.md) for system design
4. Look at [NEXT_STEPS.md](NEXT_STEPS.md) for immediate tasks

## 🔄 Keeping Documentation Updated

When making significant changes:
1. Update [plan.md](plan.md) with progress
2. Document architectural decisions in [continuous_architecture_findings.md](continuous_architecture_findings.md)
3. Keep [NEXT_AGENT_START_HERE.md](NEXT_AGENT_START_HERE.md) current as the entry point
4. Create new phase reports (phase6_xxx.md) as needed
