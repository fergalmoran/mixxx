#include "analyzer/analyzersilence.h"

#include "analyzer/constants.h"
#include "engine/engine.h"

namespace {

constexpr float kSilenceThreshold = 0.001;
// TODO: Change the above line to:
//constexpr float kSilenceThreshold = db2ratio(-60.0f);

}  // anonymous namespace

AnalyzerSilence::AnalyzerSilence(UserSettingsPointer pConfig)
    : m_pConfig(pConfig),
      m_fThreshold(kSilenceThreshold),
      m_iFramesProcessed(0),
      m_bPrevSilence(true),
      m_iSignalStart(-1),
      m_iSignalEnd(-1) {
}

bool AnalyzerSilence::initialize(TrackPointer pTrack, int sampleRate, int totalSamples) {
    Q_UNUSED(sampleRate);
    Q_UNUSED(totalSamples);

    m_iFramesProcessed = 0;
    m_bPrevSilence = true;
    m_iSignalStart = -1;
    m_iSignalEnd = -1;

    return !isDisabledOrLoadStoredSuccess(pTrack);
}

bool AnalyzerSilence::isDisabledOrLoadStoredSuccess(TrackPointer pTrack) const {
    if (!shouldUpdateCue(pTrack->getCuePoint())) {
        return false;
    }

    CuePointer pFirstSound = pTrack->findCueByType(Cue::Type::FirstSound);
    if (!pFirstSound || pFirstSound->getSource() != Cue::Source::Manual) {
        return false;
    }

    CuePointer pLastSound = pTrack->findCueByType(Cue::Type::LastSound);
    if (!pLastSound || pLastSound->getSource() != Cue::Source::Manual) {
        return false;
    }

    CuePointer pIntroCue = pTrack->findCueByType(Cue::Type::Intro);
    if (!pIntroCue || pIntroCue->getSource() != Cue::Source::Manual) {
        return false;
    }

    CuePointer pOutroCue = pTrack->findCueByType(Cue::Type::Outro);
    if (!pOutroCue || pOutroCue->getSource() != Cue::Source::Manual) {
        return false;
    }

    return true;
}

void AnalyzerSilence::process(const CSAMPLE* pIn, const int iLen) {
    for (int i = 0; i < iLen; i += mixxx::kAnalysisChannels) {
        // Compute max of channels in this sample frame
        CSAMPLE fMax = CSAMPLE_ZERO;
        for (SINT ch = 0; ch < mixxx::kAnalysisChannels; ++ch) {
            CSAMPLE fAbs = fabs(pIn[i + ch]);
            fMax = math_max(fMax, fAbs);
        }

        bool bSilence = fMax < m_fThreshold;

        if (m_bPrevSilence && !bSilence) {
            if (m_iSignalStart < 0) {
                m_iSignalStart = m_iFramesProcessed + i / mixxx::kAnalysisChannels;
            }
        } else if (!m_bPrevSilence && bSilence) {
            m_iSignalEnd = m_iFramesProcessed + i / mixxx::kAnalysisChannels;
        }

        m_bPrevSilence = bSilence;
    }

    m_iFramesProcessed += iLen / mixxx::kAnalysisChannels;
}

void AnalyzerSilence::cleanup(TrackPointer pTrack) {
    Q_UNUSED(pTrack);
}

void AnalyzerSilence::finalize(TrackPointer pTrack) {
    if (m_iSignalStart < 0) {
        m_iSignalStart = 0;
    }
    if (m_iSignalEnd < 0) {
        m_iSignalEnd = m_iFramesProcessed;
    }

    // If track didn't end with silence, place signal end marker
    // on the end of the track.
    if (!m_bPrevSilence) {
        m_iSignalEnd = m_iFramesProcessed;
    }

    if (shouldUpdateCue(pTrack->getCuePoint())) {
        pTrack->setCuePoint(CuePosition(mixxx::kAnalysisChannels * m_iSignalStart, Cue::Source::Automatic));
    }

    CuePointer pFirstSound = pTrack->findCueByType(Cue::Type::FirstSound);
    if (pFirstSound == nullptr) {
        pFirstSound = pTrack->createAndAddCue();
        pFirstSound->setType(Cue::Type::FirstSound);
        pFirstSound->setSource(Cue::Source::Automatic);
        pFirstSound->setPosition(mixxx::kAnalysisChannels * m_iSignalStart);
    } else if (pFirstSound->getSource() != Cue::Source::Manual) {
        pFirstSound->setPosition(mixxx::kAnalysisChannels * m_iSignalStart);
    }

    CuePointer pLastSound = pTrack->findCueByType(Cue::Type::LastSound);
    if (pLastSound == nullptr) {
        pLastSound = pTrack->createAndAddCue();
        pLastSound->setType(Cue::Type::LastSound);
        pLastSound->setSource(Cue::Source::Automatic);
        pLastSound->setPosition(mixxx::kAnalysisChannels * m_iSignalEnd);
    } else if (pLastSound->getSource() != Cue::Source::Manual) {
        pLastSound->setPosition(mixxx::kAnalysisChannels * m_iSignalEnd);
    }

    CuePointer pIntroCue = pTrack->findCueByType(Cue::Type::Intro);
    if (!pIntroCue) {
        pIntroCue = pTrack->createAndAddCue();
        pIntroCue->setType(Cue::Type::Intro);
        pIntroCue->setSource(Cue::Source::Automatic);
        pIntroCue->setPosition(mixxx::kAnalysisChannels * m_iSignalStart);
        pIntroCue->setLength(0.0);
    } else if (pIntroCue->getSource() != Cue::Source::Manual) {
        pIntroCue->setPosition(mixxx::kAnalysisChannels * m_iSignalStart);
        pIntroCue->setLength(0.0);
    }

    CuePointer pOutroCue = pTrack->findCueByType(Cue::Type::Outro);
    if (!pOutroCue) {
        pOutroCue = pTrack->createAndAddCue();
        pOutroCue->setType(Cue::Type::Outro);
        pOutroCue->setSource(Cue::Source::Automatic);
        pOutroCue->setPosition(-1.0);
        pOutroCue->setLength(mixxx::kAnalysisChannels * m_iSignalEnd);
    } else if (pOutroCue->getSource() != Cue::Source::Manual) {
        pOutroCue->setPosition(-1.0);
        pOutroCue->setLength(mixxx::kAnalysisChannels * m_iSignalEnd);
    }
}

bool AnalyzerSilence::shouldUpdateCue(CuePosition cue) {
    return cue.getSource() != Cue::Source::Manual || cue.getPosition() == -1.0 || cue.getPosition() == 0.0;
}
