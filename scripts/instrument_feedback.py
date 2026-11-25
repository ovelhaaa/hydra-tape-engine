#!/usr/bin/env python3
"""Create a stage-instrumented copy of dsp/core/tape_core.cpp.

The copy lives under artifacts/ so the tracked DSP source is never modified for
diagnosis. Instrumentation only stores already-computed float values; it does
not change the arithmetic, ordering or state of any DSP stage.
"""
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
text = src.read_text(encoding="utf-8")

DIAG = '''
namespace phase64_diag {
struct Record {
  float in, cond, tape_out, fb_source, fb, rec, mag, out;
  float texture_sig, delay_sig, tape_sig;
};
Record g[8192];
int n = 0;
bool enabled = false;
}  // namespace phase64_diag
'''

anchor = "namespace hydra::dsp {\nnamespace {"
assert text.count(anchor) == 1
text = text.replace(anchor, "namespace hydra::dsp {\n" + DIAG + "namespace {")

# writeCh gains a channel index so only the left channel feeds the record.
old_sig = ("auto writeCh=[&](float input,float dryTapeOutput,const ChannelFrame& frame,"
           "float feedbackInput,float* buffer,DCBlocker& dc,TapeMagnetics& mag) "
           "HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {")
new_sig = ("auto writeCh=[&](float input,float dryTapeOutput,const ChannelFrame& frame,"
           "float feedbackInput,float* buffer,DCBlocker& dc,TapeMagnetics& mag,int ch) "
           "HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {")
assert text.count(old_sig) == 1
text = text.replace(old_sig, new_sig)

old_rec = "float recSig=dc.process(freshInput + feedbackInput); recSig=clampf(recSig,-4,4);"
new_rec = (old_rec + "\n    if(ch==0&&phase64_diag::enabled&&phase64_diag::n<8192)"
           "{phase64_diag::g[phase64_diag::n].rec=recSig;}")
assert text.count(old_rec) == 1
text = text.replace(old_rec, new_rec)

# Explicit semantic boundary: read the value already stored after magnetics,
# including F4 release gain. Never call mag.process twice or write while frozen.
mag_anchor = "    // HYDRA_TEST_POINT_MAGNETIC_WRITE: stored post-magnetics value; zero when frozen."
if text.count(mag_anchor) != 1:
    raise RuntimeError("expected exactly one post-magnetics test point")
text = text.replace(mag_anchor, mag_anchor + "\n    if(ch==0&&phase64_diag::enabled&&phase64_diag::n<8192)"
                    "{phase64_diag::g[phase64_diag::n].mag=impl_->freeze?0.f:buffer[impl_->writeHead];}")

old_calls = ("*outL=writeCh(inL,frameL.tapeOutput,frameL,feedbackL,impl_->delayLine,impl_->dcBlocker,impl_->magneticsL);\n"
             "  *outR=writeCh(inR,frameR.tapeOutput,frameR,feedbackR,impl_->delayLineR,impl_->dcBlockerR,impl_->magneticsR);")
new_calls = ("*outL=writeCh(inL,frameL.tapeOutput,frameL,feedbackL,impl_->delayLine,impl_->dcBlocker,impl_->magneticsL,0);\n"
             "  *outR=writeCh(inR,frameR.tapeOutput,frameR,feedbackR,impl_->delayLineR,impl_->dcBlockerR,impl_->magneticsR,1);")
assert text.count(old_calls) == 1
text = text.replace(old_calls, new_calls)

anchor2 = "  outLBlock[frameIdx]=sampleOutL;"
assert text.count(anchor2) == 1
text = text.replace(anchor2,
    "  if(phase64_diag::enabled&&phase64_diag::n<8192){auto&r=phase64_diag::g[phase64_diag::n++];"
    "r.in=inL;r.cond=frameL.conditionedInput;r.tape_out=frameL.tapeOutput;"
    "r.fb_source=frameL.feedbackSource;r.fb=feedbackL;r.out=sampleOutL;}\n" + anchor2)


# readCh gains a channel index; capture the pre-filter playback signal for the left channel.
old_read_sig = ("auto readCh=[&](float input,float noiseInjected,float* buffer,BiquadFilter& reproHeadBumpFilter,"
                "BiquadFilter& tr,BiquadFilter& outputGapLossFilter,AllpassFilter& az,BiquadFilter&iHP,"
                "BiquadFilter&iLP,OnePoleLP&dropoutFilter,DropoutFrame drop,float mod,float warpSig,float warpFb) "
                "HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {")
new_read_sig = old_read_sig.replace("float warpFb) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {",
                                 "float warpFb,int ch) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {")
assert text.count(old_read_sig) == 1
text = text.replace(old_read_sig, new_read_sig)

old_tape = "    float tapeSig = (textureSig * textureGain) + (delaySig * delayGain);"
new_tape = (old_tape + "\n    if(ch==0&&phase64_diag::enabled&&phase64_diag::n<8192){auto&r=phase64_diag::g[phase64_diag::n];"
            "r.texture_sig=textureSig;r.delay_sig=delaySig;r.tape_sig=tapeSig;}")
assert text.count(old_tape) == 1
text = text.replace(old_tape, new_tape)

old_read_calls = ("  ChannelFrame frameL=readCh(inL,hissL,impl_->delayLine,impl_->reproHeadBump,impl_->tapeRolloff,"
                  "impl_->gapLossLPF,impl_->azimuthFilter,impl_->inputHPF,impl_->inputLPF,impl_->dropoutLPF,"
                  "dropoutL,modL,warpSigL,warpFbL);\n"
                  "  ChannelFrame frameR=readCh(inR,hissR,impl_->delayLineR,impl_->reproHeadBumpR,impl_->tapeRolloffR,"
                  "impl_->gapLossLPFR,impl_->azimuthFilterR,impl_->inputHPFR,impl_->inputLPFR,impl_->dropoutLPFR,"
                  "dropoutR,modR,warpSigR,warpFbR);")
new_read_calls = old_read_calls.replace("warpFbL)", "warpFbL,0)").replace("warpFbR)", "warpFbR,1)")
assert text.count(old_read_calls) == 1, text.count(old_read_calls)
text = text.replace(old_read_calls, new_read_calls)

text += '''
extern "C" void phase64_diag_enable(int on) { hydra::dsp::phase64_diag::enabled = (on != 0); }
extern "C" int phase64_diag_count() { return hydra::dsp::phase64_diag::n; }
extern "C" const float* phase64_diag_data() {
  return reinterpret_cast<const float*>(hydra::dsp::phase64_diag::g);
}
extern "C" int phase64_diag_stride() {
  return static_cast<int>(sizeof(hydra::dsp::phase64_diag::Record) / sizeof(float));
}
'''

dst.write_text(text, encoding="utf-8")
print(f"wrote {dst} ({len(text)} bytes)")

