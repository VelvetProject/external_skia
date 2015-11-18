/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrNinePatch.h"

#include "GrBatchFlushState.h"
#include "GrDefaultGeoProcFactory.h"
#include "GrResourceProvider.h"
#include "GrVertexBatch.h"
#include "SkBitmap.h"
#include "SkNinePatchIter.h"
#include "SkRect.h"

static const GrGeometryProcessor* create_gp(bool readsCoverage) {
    using namespace GrDefaultGeoProcFactory;
    Color color(Color::kAttribute_Type);
    Coverage coverage(readsCoverage ? Coverage::kSolid_Type : Coverage::kNone_Type);
    LocalCoords localCoords(LocalCoords::kHasExplicit_Type);
    return GrDefaultGeoProcFactory::Create(color, coverage, localCoords, SkMatrix::I());
}

class GrNonAANinePatchBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    static const int kVertsPerRect = 4;
    static const int kIndicesPerRect = 6;
    static const int kRectsPerInstance = 9; // We could skip empty rects

    struct Geometry {
        SkMatrix fViewMatrix;
        SkIRect fCenter;
        SkRect fDst;
        GrColor fColor;
    };

    GrNonAANinePatchBatch(GrColor color, const SkMatrix& viewMatrix, int imageWidth,
                          int imageHeight, const SkIRect& center, const SkRect &dst)
        : INHERITED(ClassID()) {
        Geometry& geo = fGeoData.push_back();
        geo.fViewMatrix = viewMatrix;
        geo.fColor = color;
        geo.fCenter = center;
        geo.fDst = dst;

        fImageWidth = imageWidth;
        fImageHeight = imageHeight;

        // setup bounds
        geo.fViewMatrix.mapRect(&fBounds, geo.fDst);
    }

    const char* name() const override { return "GrNonAANinePatchBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        out->setUnknownFourComponents();
    }

    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setKnownSingleComponent(0xff);
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

private:
    void onPrepareDraws(Target* target) override {
        SkAutoTUnref<const GrGeometryProcessor> gp(create_gp(fOpts.readsCoverage()));
        if (!gp) {
            SkDebugf("Couldn't create GrGeometryProcessor\n");
            return;
        }

        target->initDraw(gp, this->pipeline());

        size_t vertexStride = gp->getVertexStride();
        int instanceCount = fGeoData.count();

        SkAutoTUnref<const GrIndexBuffer> indexBuffer(
                target->resourceProvider()->refQuadIndexBuffer());
        InstancedHelper helper;
        void* vertices = helper.init(target, kTriangles_GrPrimitiveType, vertexStride,
                                     indexBuffer, kVertsPerRect,
                                     kIndicesPerRect, instanceCount * kRectsPerInstance);
        if (!vertices || !indexBuffer) {
            SkDebugf("Could not allocate vertices\n");
            return;
        }

        for (int i = 0; i < instanceCount; i++) {
            intptr_t verts = reinterpret_cast<intptr_t>(vertices) +
                             i * kRectsPerInstance * kVertsPerRect * vertexStride;

            Geometry& geo = fGeoData[i];
            SkNinePatchIter iter(fImageWidth, fImageHeight, geo.fCenter, geo.fDst);

            SkRect srcR, dstR;
            while (iter.next(&srcR, &dstR)) {
                SkPoint* positions = reinterpret_cast<SkPoint*>(verts);

                positions->setRectFan(dstR.fLeft, dstR.fTop,
                                      dstR.fRight, dstR.fBottom, vertexStride);

                SkASSERT(!geo.fViewMatrix.hasPerspective());
                geo.fViewMatrix.mapPointsWithStride(positions, vertexStride, kVertsPerRect);

                // Setup local coords
                static const int kLocalOffset = sizeof(SkPoint) + sizeof(GrColor);
                SkPoint* coords = reinterpret_cast<SkPoint*>(verts + kLocalOffset);
                coords->setRectFan(srcR.fLeft, srcR.fTop, srcR.fRight, srcR.fBottom, vertexStride);

                static const int kColorOffset = sizeof(SkPoint);
                GrColor* vertColor = reinterpret_cast<GrColor*>(verts + kColorOffset);
                for (int j = 0; j < 4; ++j) {
                    *vertColor = geo.fColor;
                    vertColor = (GrColor*) ((intptr_t) vertColor + vertexStride);
                }
                verts += kVertsPerRect * vertexStride;
            }
        }
        helper.recordDraw(target);
    }

    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);
        fOpts = opt;
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        GrNonAANinePatchBatch* that = t->cast<GrNonAANinePatchBatch>();
        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        SkASSERT(this->fImageWidth == that->fImageWidth &&
                 this->fImageHeight == that->fImageHeight);

        // In the event of two batches, one who can tweak, one who cannot, we just fall back to
        // not tweaking
        if (fOpts.canTweakAlphaForCoverage() && !that->fOpts.canTweakAlphaForCoverage()) {
            fOpts = that->fOpts;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrPipelineOptimizations fOpts;
    int fImageWidth;
    int fImageHeight;
    SkSTArray<1, Geometry, true> fGeoData;

    typedef GrVertexBatch INHERITED;
};

namespace GrNinePatch {
GrDrawBatch* CreateNonAA(GrColor color, const SkMatrix& viewMatrix, int imageWidth, int imageHeight,
                         const SkIRect& center, const SkRect& dst) {
    return new GrNonAANinePatchBatch(color, viewMatrix, imageWidth, imageHeight, center, dst);
}
};
