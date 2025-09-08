#include <VapourSynth4.h>
#include <VSHelper4.h>
#include <vector>
#include <algorithm>
#include <cmath>

typedef struct TemporalDenoiseData {
    VSNode* node;
    VSVideoInfo vi;
    int radius;
    float strength;
} TemporalDenoiseData;

static const VSFrame* VS_CC temporalDenoiseGetFrame(
    int n, int activationReason, void* instanceData, void**,
    VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi)
{
    TemporalDenoiseData* d = (TemporalDenoiseData*)instanceData;

    if (activationReason == arInitial) {
        for (int i = -d->radius; i <= d->radius; i++) {
            int fn = std::clamp(n + i, 0, d->vi.numFrames - 1);
            vsapi->requestFrameFilter(fn, d->node, frameCtx);
        }
        return NULL;
    }

    if (activationReason == arAllFramesReady) {
        std::vector<const VSFrame*> refFrames(d->radius * 2 + 1);
        for (int i = -d->radius; i <= d->radius; i++) {
            int fn = std::clamp(n + i, 0, d->vi.numFrames - 1);
            refFrames[i + d->radius] = vsapi->getFrameFilter(fn, d->node, frameCtx);
        }

        const VSFrame* src = refFrames[d->radius];
        const VSVideoFormat* fi = &d->vi.format;

        VSFrame* dst = vsapi->newVideoFrame(fi, d->vi.width, d->vi.height, src, core);

        const int T = static_cast<int>(refFrames.size());

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const int h = vsapi->getFrameHeight(src, plane);
            const int w = vsapi->getFrameWidth(src, plane);
            const ptrdiff_t stride = vsapi->getStride(src, plane);

            std::vector<const uint8_t*> rp(T);
            for (int t = 0; t < T; t++) {
                rp[t] = vsapi->getReadPtr(refFrames[t], plane);
            }

            uint8_t* wp = vsapi->getWritePtr(dst, plane);

            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int sum = 0;
                    for (int t = 0; t < T; t++) {
                        sum += rp[t][x];
                    }

                    int avg = sum / T;
                    int cur = rp[d->radius][x];

                    float outv = static_cast<float>(cur) * (1.0f - d->strength) + static_cast<float>(avg) * d->strength;

                    wp[x] = static_cast<uint8_t>(std::clamp(static_cast<int>(outv + 0.5f), 0, 255));
                }

                for (int t = 0; t < T; t++)
                    rp[t] += stride;
                wp += stride;
            }
        }

        for (size_t i = 0; i < refFrames.size(); i++) {
            vsapi->freeFrame(refFrames[i]);
        }

        return dst;
    }

    return NULL;
}

static void VS_CC temporalDenoiseFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    TemporalDenoiseData* d = (TemporalDenoiseData*)instanceData;
    if (d->node)
        vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC temporalDenoiseCreate(
    const VSMap* in, VSMap* out, void* userData,
    VSCore* core, const VSAPI* vsapi)
{
    int err = 0;
    TemporalDenoiseData* d = (TemporalDenoiseData*)malloc(sizeof(TemporalDenoiseData));

    d->node = vsapi->mapGetNode(in, "clip", 0, &err);
    if (err) {
        vsapi->mapSetError(out, "TemporalDenoise: clip is required.");
        free(d);
        return;
    }

    d->vi = *vsapi->getVideoInfo(d->node);

    if (d->vi.format.bytesPerSample != 1 || d->vi.format.sampleType != stInteger) {
        vsapi->mapSetError(out, "TemporalDenoise: Only 8-bit integer clips are supported.");
        vsapi->freeNode(d->node);
        free(d);
        return;
    }

    d->radius = (int)vsapi->mapGetInt(in, "radius", 0, &err);
    if (err) {
        d->radius = 1;
    }

    d->strength = (float)vsapi->mapGetFloat(in, "strength", 0, &err);
    if (err) {
        d->strength = 0.5f;
    }
    d->strength = std::clamp(d->strength, 0.0f, 1.0f);

    VSFilterDependency deps[] = { { d->node, rpGeneral } };
    vsapi->createVideoFilter(out,
        "TemporalDenoiseO",
        &d->vi,
        temporalDenoiseGetFrame,
        temporalDenoiseFree,
        fmParallel,
        deps, 1,
        d, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(
    VSPlugin* plugin, const VSPLUGINAPI* vspapi)
{
    vspapi->configPlugin("com.example.temporaldenoise.cpu", "otdn",
        "Temporal Denoise filter (API v4)",
        VS_MAKE_VERSION(1, 0),
        VAPOURSYNTH_API_VERSION,
        0,
        plugin);

    vspapi->registerFunction("TemporalDenoiseO",
        "clip:vnode;radius:int:opt;strength:float:opt;",
        "clip:vnode;",
        temporalDenoiseCreate, NULL, plugin);
}
