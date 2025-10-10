# Project Specifications & Internal Documentation

This folder contains **internal project documentation** including architecture decisions, progress tracking, and technical specifications.

## 🎯 Quick Start for New Contributors

**Essential reading (in order):**
1. **[NEXT_AGENT_START_HERE.md](NEXT_AGENT_START_HERE.md)** - Start here for current context
2. **[plan.md](plan.md)** - Project status, phases, current progress
3. **[architecture.md](architecture.md)** - Complete system architecture (with full document index)

## 📋 Document Organization

### Core Documents (Read These First)
- **[architecture.md](architecture.md)** - ⭐ **Central hub** - Complete system architecture with index to all docs
- **[plan.md](plan.md)** - ⭐ Current project plan, phases, and progress tracking
- **[phase6_completion.md](phase6_completion.md)** - Latest phase completion details

### Component Documentation
- **[transcription.md](transcription.md)** - Whisper ASR integration
- **[diarization.md](diarization.md)** - Speaker identification system
- **[application_api_design.md](application_api_design.md)** - TranscriptionController API
- **[continuous_architecture_findings.md](continuous_architecture_findings.md)** - Experimental findings

### Phase Reports (Historical Progress)
- **[phase3_report.md](phase3_report.md)** - Phase 3: Frame voting diarization
- **[phase4_application_api_summary.md](phase4_application_api_summary.md)** - Phase 4: Controller API
- **[phase5_gui_implementation_log.md](phase5_gui_implementation_log.md)** - Phase 5: GUI implementation

### Streaming & Performance
- **[STREAMING_STRATEGY.md](STREAMING_STRATEGY.md)** - Hold-and-emit strategy
- **[STREAMING_SUCCESS.md](STREAMING_SUCCESS.md)** - Implementation validation
- **[CIRCULAR_BUFFER_PROPOSAL.md](CIRCULAR_BUFFER_PROPOSAL.md)** - Buffer design

### Models & Research
- **[MODELS.md](MODELS.md)** - Model files and downloads
- **[speaker_models_onnx.md](speaker_models_onnx.md)** - ONNX embedding models
- **[speaker_identification_analysis.md](speaker_identification_analysis.md)** - Speaker ID research

### Platform Organization
- **[platform_separation.md](platform_separation.md)** - Multiplatform code structure (Windows/macOS)

## 📂 Documentation Hierarchy

```
/README.md      ← User-facing project overview
/docs/          ← User documentation (setup, usage)
/specs/         ← You are here - Internal technical documentation
  ├─ architecture.md     ← START HERE (central hub with full index)
  ├─ plan.md             ← Project progress and status
  └─ [component docs]    ← Detailed technical specs
```

## 🔍 Finding Information

**Need to understand the system?** → Read [architecture.md](architecture.md)  
**Want to know current status?** → Check [plan.md](plan.md)  
**Looking for specific component?** → See architecture.md's [Documentation Index](architecture.md#documentation-index)
4. Look at [NEXT_STEPS.md](NEXT_STEPS.md) for immediate tasks

## 🔄 Keeping Documentation Updated

When making significant changes:
1. Update [plan.md](plan.md) with progress
2. Document architectural decisions in [continuous_architecture_findings.md](continuous_architecture_findings.md)
3. Keep [NEXT_AGENT_START_HERE.md](NEXT_AGENT_START_HERE.md) current as the entry point
4. Create new phase reports (phase6_xxx.md) as needed
